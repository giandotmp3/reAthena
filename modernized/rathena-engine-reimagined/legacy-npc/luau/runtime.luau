--!nonstrict
-- The legacy-NPC runtime: ties the lazy loader, the interpreter, the variable backend, the blocking adapters, the event system (labels,
-- 2-slot queue, timers) and the report together, against the host seams script-host provides.
--
-- Host seams (cfg.host; every field optional, the defaults make the runtime self-contained for tests and the Luau CLI):
--   storage   script-host `storage` global (character/account/world scopes)           default: in-memory adapter (storage_mem.luau)
--   timers    script-host `timers` global: after(ms, fn); owned by (mod, generation)  default: internal queue driven by Rt:advance(ms)
--   events    script-host `events` global: on(event, spec, fn); load-time only        used by Rt:registerEvents
--   dialogue  say/choose/input/wait (script-host `dialogue` global or the bare globals); valid only directly inside the dialogue
--             coroutine started by DialogueManager::start                              used by Rt:run
--   log       function(string)                                                          default: print
--   hooks     { atcommand=fn, query_sql=fn, setunitdata=fn }: the effects of the permission-gated commands
-- Engine-side seams that do not exist yet are plain Rt methods the engine (or a fake) calls: Rt:fireAll(label, player) for the
-- engine-fired events, Rt:clockTick(date), Rt:advance(ms) / Rt:setClock(ms), Rt:player(rid, fields) / Rt:dropPlayer(rid).
local Loader = require("./loader")
local Interp = require("./interp")
local World = require("./world")
local Vars = require("./vars")
local Cmds = require("./commands_rt")
local Blocking = require("./blocking")
local Events = require("./events")
local Report = require("./report")

local lower = string.lower

local Rt = {}
Rt.__index = Rt

local MAX_EVENT_TIMERS = 32 -- pc.hpp MAX_EVENTTIMER
local MAX_NESTED_EVENTS = 16 -- donpcevent -> donpcevent ... host-stack guard (the legacy VM recurses through run_script as well)

function Rt.new(cfg)
	cfg = cfg or {}
	local host = cfg.host or {}
	local self = setmetatable({}, Rt)
	self.host = host
	self.log = host.log or function(msg) print("[legacy.npc] " .. msg) end
	self.loader = Loader.new{ builtins = cfg.builtins, readText = cfg.readText }
	local storage = host.storage
	if not storage then
		self.mem = require("./storage_mem").new(cfg.storage or {})
		storage = self.mem.api
	end
	self.backend = Vars.newBackend(storage)
	self.granted = {}
	for _, p in cfg.granted or {} do self.granted[p] = true end
	self.limits = { max_commands = (cfg.limits and cfg.limits.max_commands) or 65535, max_gotos = (cfg.limits and cfg.limits.max_gotos) or 2048 }
	self.limits.max_depth = cfg.limits and cfg.limits.max_depth or nil
	local compat = cfg.compat or {}
	self.compat = { two_slot_event_queue = compat.two_slot_event_queue ~= false, legacy_quirks = compat.legacy_quirks ~= false }
	self.disableUnsupported = cfg.disable_unsupported ~= false
	self.dialogue = host.dialogue
	self.trace = cfg.trace -- function(name, ...): every recorded command call with its evaluated arguments
	self.data = cfg.data
	self.players = {}
	self.clock = 0
	self.pending = {} -- internal timer queue when the host has no timers
	self.depth = 0
	self.stats = { runs = 0, events = 0, queued = 0, dropped = 0, dequeued = 0 }
	self.env = World.newEnv(self.loader:funcRegistry(), cfg.seed or 12345, cfg.data, self.backend)
	self.env.npcvars = self.loader:npcVarsRegistry()
	self.env.rt = self
	self.env.hooks = host.hooks
	self.nobody = self:newPlayerObject(0, { name = "", charid = 0, accountid = 0, gameid = 0 })
	self.nobody.rid = 0
	return self
end

---------------------------------------------------------------------------------------------------------------------
-- corpus
---------------------------------------------------------------------------------------------------------------------
-- Indexes a file (no parsing). text may be nil when cfg.readText is set.
function Rt:addFile(path: string, text: string?)
	return self.loader:addFile(path, text)
end

-- Registers one bus hook per On* label of every NPC (call while the mod chunk loads: events.on is only valid then).
-- Returns the number of hooks. eventsApi defaults to host.events.
function Rt:registerEvents(eventsApi)
	eventsApi = eventsApi or self.host.events
	assert(eventsApi, "registerEvents needs the script-host events API")
	local n = 0
	local rt = self
	for _, entry in self.loader.npcList do
		if entry.events then
			for _, label in entry.events do
				local e = entry
				local lab = label
				eventsApi.on(Events.busName(label), { id = e.exname .. "::" .. label, phase = "on", mode = "observe" }, function(ctx)
					-- the engine puts the attached player id in the numeric context (0 = none)
					local rid = ctx:get()
					rt:dispatchBus(e, lab, rid)
				end)
				n += 1
			end
		end
	end
	return n
end

function Rt:dispatchBus(entry, label, rid)
	local player = (rid and rid ~= 0) and self.players[rid] or nil
	self:runLabel(entry, label, player)
end

---------------------------------------------------------------------------------------------------------------------
-- players
---------------------------------------------------------------------------------------------------------------------
function Rt:newPlayerObject(rid, fields)
	fields = fields or {}
	local p = World.newPlayer(fields.profile or "poor", self.backend, { charid = fields.charid or rid, accountid = fields.accountid or (rid + 2000000) })
	for k, v in fields do
		if k == "params" then
			for pk, pv in v do p.params[pk] = pv end
		elseif k ~= "profile" then
			p[k] = v
		end
	end
	p.rid = rid
	p.gameid = fields.gameid or rid
	p.busy = 0
	p.queue = Events.newQueue(self.compat.two_slot_event_queue)
	p.timers = {}
	p.timerSeq = 0
	return p
end
function Rt:player(rid: number, fields)
	local p = self.players[rid]
	if p then return p end
	p = self:newPlayerObject(rid, fields)
	self.players[rid] = p
	return p
end
-- logout / disconnect: temp variables die, event timers and the queue are discarded (the dialogue itself is cancelled by the host)
function Rt:dropPlayer(rid: number)
	local p = self.players[rid]
	if not p then return end
	Vars.flush(p.perm, p.acc, p.acc2)
	p.timerSeq += 1
	p.timers = {}
	p.queue = Events.newQueue(self.compat.two_slot_event_queue)
	self.players[rid] = nil
end

---------------------------------------------------------------------------------------------------------------------
-- scheduling (script-host timers, or an internal queue)
---------------------------------------------------------------------------------------------------------------------
function Rt:setClock(ms: number)
	if ms > self.clock then self.clock = ms end
end
-- run `fn` after ms of game time
function Rt:after(ms: number, fn)
	ms = math.max(0, ms)
	local due = self.clock + ms
	local timers = self.host.timers
	if timers then
		timers.after(ms, function()
			if due > self.clock then self.clock = due end
			fn()
		end)
	else
		local q = self.pending
		q[#q + 1] = { due = due, fn = fn, seq = (self.seq or 0) + 1 }
		self.seq = (self.seq or 0) + 1
	end
end
-- internal queue only (no host timers): advance game time, firing due callbacks in (due, creation) order
function Rt:advance(ms: number)
	local target = self.clock + ms
	while true do
		local best, bi
		for i, t in self.pending do
			if t.due <= target and (not best or t.due < best.due or (t.due == best.due and t.seq < best.seq)) then best, bi = t, i end
		end
		if not best then break end
		table.remove(self.pending, bi)
		if best.due > self.clock then self.clock = best.due end
		best.fn()
	end
	self.clock = target
end

---------------------------------------------------------------------------------------------------------------------
-- running scripts
---------------------------------------------------------------------------------------------------------------------
local function errTag(err)
	if type(err) ~= "table" then return tostring(err) end
	local s = tostring(err.type)
	if err.cmd then s ..= ":" .. tostring(err.cmd) end
	return s
end

local function mkOutcome(status, err, extra)
	local o = extra or {}
	o.status = status
	o.err = err
	return o
end

-- static check before an NPC starts: an unsupported command disables that NPC (and only it)
function Rt:supported(entry)
	local root = self.loader:rootOf(entry) or entry
	if root.supportChecked == nil then
		local a, err = Report.analyze(self.loader, root)
		if not a then
			root.supportChecked = false
			root.supportError = err
		else
			local list = {}
			for name in a.unsupported do list[#list + 1] = name end
			table.sort(list)
			root.supportChecked = #list == 0
			root.unsupportedCommands = list
		end
	end
	return root.supportChecked, root
end

local function makeState(self, entry, prog, player, block)
	local vars = self.loader:varsOf(entry)
	entry.vars = vars
	return Interp.newState{
		player = player or self.nobody, env = self.env, npc = entry, commands = Cmds.commands, granted = self.granted,
		limit = self.limits.max_commands, glimit = self.limits.max_gotos, block = block, counts = {}, trace = self.trace, maxCallDepth = self.limits.max_depth,
	}
end

-- Starts `entry` at pc/label on the CALLING coroutine with the dialogue adapter (valid inside a DialogueManager coroutine).
-- Returns an outcome { status = "end"|"error"|"unsupported"|"missing"|"load_error"|"disabled", err = typed error table, npc = name }.
function Rt:run(npcName: string, rid: number?, label: string?)
	local entry = self.loader.npcs[npcName] or self.loader.npcsLower[lower(npcName)]
	if not entry then return mkOutcome("missing", { type = "NO_SUCH_NPC", npc = npcName }, { npc = npcName }) end
	local player = rid and self:player(rid) or nil
	return self:start(entry, label, player, self.dialogue and Blocking.dialogue(self.dialogue) or nil, true)
end

-- Core start. block = dialogue adapter (interactive) or nil (coroutine driven; blocking dialogue commands end the script).
function Rt:start(entry, label, player, block, interactive)
	local prog, lerr = self.loader:program(entry)
	if not prog then return mkOutcome("load_error", { type = "LOAD_ERROR", cause = lerr and lerr.cause, line = lerr and lerr.line }, { npc = entry.name }) end
	if self.disableUnsupported then
		local ok, root = self:supported(entry)
		if not ok then
			if root.supportError then return mkOutcome("load_error", { type = "LOAD_ERROR", cause = root.supportError.cause }, { npc = entry.name }) end
			return mkOutcome("unsupported", { type = "UNSUPPORTED", cmd = root.unsupportedCommands[1], cmds = root.unsupportedCommands }, { npc = entry.name })
		end
	end
	local pc = 1
	if label then
		pc = prog.labels[lower(label)]
		if not pc then return mkOutcome("missing", { type = "NO_SUCH_LABEL", label = label }, { npc = entry.name }) end
	end
	-- Event runs started from inside another run nest on the host stack (the legacy VM recurses through run_script as well): bounded.
	-- Dialogue runs (block ~= nil) start on their own coroutine and are not counted: a cancelled one never gets to decrement.
	if not block and self.depth >= MAX_NESTED_EVENTS then
		self.log("event nesting too deep at " .. entry.name)
		return mkOutcome("error", { type = "NESTED_EVENTS" }, { npc = entry.name })
	end
	local st = makeState(self, entry, prog, player, block)
	local pl = st.player
	pl.busy += 1
	self.stats.runs += 1
	local outcome
	if block then
		local status, err = Interp.run(st, prog, pc, {})
		outcome = mkOutcome(status, err, { npc = entry.name })
		self:finish(st, pl)
	else
		self.depth += 1
		local co = Interp.spawn(st, prog, pc, {})
		outcome = self:drive(co, st, entry, pl)
		self.depth -= 1
	end
	return outcome
end

-- The host cancelled a dialogue run (disconnect, idle timeout, reload drain): none of this package's code runs after a cancel, so the host says
-- so. The player stops being "inside a script" and the queue drains, as at the end of a script.
function Rt:endDialogue(rid: number)
	local p = self.players[rid]
	if not p or p.busy == 0 then return end
	p.busy = 0
	self:dequeue(p)
end

function Rt:finish(st, pl)
	pl.busy -= 1
	if pl.busy == 0 and pl.rid ~= 0 then self:dequeue(pl) end
end

-- coroutine-driven (non-interactive) execution: sleeps are scheduled on the host timer, dialogue requests end the script
function Rt:drive(co, st, entry, pl, answer)
	while true do
		local ok, kind, payload = coroutine.resume(co, answer)
		answer = nil
		if not ok then
			self:finish(st, pl)
			return mkOutcome("error", { type = "HOST_ERROR", msg = tostring(kind) }, { npc = entry.name })
		end
		if coroutine.status(co) == "dead" then
			self:finish(st, pl)
			return mkOutcome(kind, payload, { npc = entry.name })
		end
		if kind == "sleep" or kind == "sleep2" or kind == "progressbar" or kind == "progressbar_npc" then
			-- continue later; the busy flag stays up while the script sleeps (sd->npc_id stays set in the legacy server)
			self:after(payload or 0, function()
				self.depth += 1
				self:drive(co, st, entry, pl)
				self.depth -= 1
			end)
			return mkOutcome("pending", nil, { npc = entry.name })
		end
		-- menu/select/prompt/input/next/close2: no client to answer. The script is abandoned; the event needs a dialogue
		-- (start it with Rt:run from a DialogueManager coroutine instead).
		self.log(string.format("%s: %s needs a dialogue; event run ended", entry.name, tostring(kind)))
		self:finish(st, pl)
		return mkOutcome("needs_dialogue", { type = "NEEDS_DIALOGUE", kind = kind }, { npc = entry.name })
	end
end

-- label run (events): player may be nil
function Rt:runLabel(entry, label, player)
	if entry.disabledByOperator then return mkOutcome("disabled", nil, { npc = entry.name }) end
	self.stats.events += 1
	return self:start(entry, label, player, nil, false)
end

---------------------------------------------------------------------------------------------------------------------
-- events: npc_event / npc_event_do / doall, the 2-slot queue and its 100 ms dequeue
---------------------------------------------------------------------------------------------------------------------
local function splitEvent(name: string)
	local npc, label = string.match(name, "^(.-)::(.+)$")
	return npc, label
end

function Rt:hasEvent(entry, llabel: string): boolean
	if not entry.events then return false end
	for _, l in entry.events do
		if lower(l) == llabel then return true end
	end
	return false
end

-- "Npc::OnLabel". player = nil: run now with no player attached (donpcevent); player: npc_event(sd, ..), queued when busy.
-- Returns true when the event exists.
function Rt:fireEvent(name: string, player, _st)
	local npcName, label = splitEvent(name)
	if not npcName then
		self.log("NPC event parameter deprecated, use 'NPCNAME::OnEVENT': " .. name)
		return false
	end
	local entry = self.loader.npcsLower[lower(npcName)]
	if not entry or not self:hasEvent(entry, lower(label)) then return false end
	if player then
		self:eventForPlayer(entry, label, player)
	else
		self:runLabel(entry, label, nil)
	end
	return true
end

-- npc_event_sub: a busy player (inside a script) gets the event queued; a disabled NPC does not trigger
function Rt:eventForPlayer(entry, label, player)
	if player.busy > 0 then
		if player.queue:push(entry.exname .. "::" .. label) then
			self.stats.queued += 1
		else
			self.stats.dropped += 1
			self.log(string.format("player's event queue is full, can't add event '%s::%s'", entry.exname, label))
		end
		return
	end
	if entry.hidden then
		self:dequeue(player)
		return
	end
	self:runLabel(entry, label, player)
end

-- npc_event_dequeue: the oldest queued event becomes a 100 ms player timer
function Rt:dequeue(player)
	local name = player.queue:pop()
	if not name then return end
	self.stats.dequeued += 1
	self:addPlayerTimer(player, Events.DEQUEUE_MS, name)
end

-- engine-fired broadcast (OnInit, OnPCLoginEvent, OnClock0830, ...): every NPC that has the label, in load order. With a player the
-- events go through the queue like the legacy npc_script_event.
function Rt:fireAll(label: string, player)
	local list = self.loader.byLabel[lower(label)]
	if not list then return 0 end
	local n = 0
	for _, entry in list do
		-- iterate a snapshot: running a label may add NPCs
		n += 1
		if player then self:eventForPlayer(entry, label, player) else self:runLabel(entry, label, nil) end
	end
	return n
end

-- the clock family of npc_event_do_clock: previous and current local time as { min, hour, mday, month (1..12), wday (0 = Sunday) }
local WDAY = { [0] = "OnSun", "OnMon", "OnTue", "OnWed", "OnThu", "OnFri", "OnSat" }
function Rt:clockTick(now, prev)
	local c = 0
	if not prev or now.min ~= prev.min then
		c += self:fireAll(string.format("OnMinute%02d", now.min))
		c += self:fireAll(string.format("OnClock%02d%02d", now.hour, now.min))
		c += self:fireAll(string.format("%s%02d%02d", WDAY[now.wday], now.hour, now.min))
	end
	if not prev or now.hour ~= prev.hour then c += self:fireAll(string.format("OnHour%02d", now.hour)) end
	if not prev or now.mday ~= prev.mday then c += self:fireAll(string.format("OnDay%02d%02d", now.month, now.mday)) end
	return c
end

-- the start of the world: OnInit on every NPC, in load order (npc_event_do_oninit)
function Rt:init()
	return self:fireAll("OnInit")
end

function Rt:setEnabled(name: string, on: boolean): boolean
	local entry = self.loader.npcs[name] or self.loader.npcsLower[lower(name)]
	if not entry then return false end
	entry.hidden = not on or nil
	return true
end

---------------------------------------------------------------------------------------------------------------------
-- player event timers (addtimer / deltimer / addtimercount): pc_addeventtimer
---------------------------------------------------------------------------------------------------------------------
function Rt:addPlayerTimer(player, ticks: number, name: string): boolean
	if #player.timers >= MAX_EVENT_TIMERS then return false end
	player.timerSeq += 1
	local t = { name = name, id = player.timerSeq, due = self.clock + ticks }
	player.timers[#player.timers + 1] = t
	self:after(ticks, function()
		for i, x in player.timers do
			if x == t then
				table.remove(player.timers, i)
				if self.players[player.rid] == player or player == self.nobody then self:fireEvent(name, player) end
				return
			end
		end
	end)
	return true
end
function Rt:delPlayerTimer(player, name: string)
	for i = #player.timers, 1, -1 do
		if lower(player.timers[i].name) == lower(name) then table.remove(player.timers, i) end
	end
end
function Rt:addPlayerTimerCount(player, name: string, ticks: number)
	for _, t in player.timers do
		if lower(t.name) == lower(name) then
			-- a pending timer is moved: the old callback finds no entry equal to t, so re-register it
			t.due += ticks
			local copy = { name = t.name, id = t.id, due = t.due }
			for i, x in player.timers do if x == t then player.timers[i] = copy end end
			self:after(math.max(0, copy.due - self.clock), function()
				for i, x in player.timers do
					if x == copy then
						table.remove(player.timers, i)
						self:fireEvent(copy.name, player)
						return
					end
				end
			end)
			return
		end
	end
end

---------------------------------------------------------------------------------------------------------------------
-- NPC timers (initnpctimer / startnpctimer / stopnpctimer / getnpctimer / setnpctimer / attach): npc.cpp npc_timerevent_*
---------------------------------------------------------------------------------------------------------------------
function Rt:timerState(entry)
	local tm = entry.tm
	if not tm then
		tm = { timer = 0, running = false, startedAt = 0, token = 0, pending = false }
		entry.tm = tm
	end
	return tm
end
-- OnTimer<ms> labels of an NPC, ascending (npc_timerevent_export keeps the list sorted by time)
function Rt:timerEvents(entry)
	local root = self.loader:rootOf(entry) or entry
	local te = root.timerEvents
	if te == nil then
		te = {}
		for _, label in root.events or {} do
			local c = Events.classify(label)
			if c and c.kind == "timer" then te[#te + 1] = { ms = c.arg, label = label } end
		end
		table.sort(te, function(a, b) return a.ms < b.ms end)
		root.timerEvents = te
	end
	return te
end
function Rt:timerStart(entry, player)
	local tm = self:timerState(entry)
	if player then tm.player = player end
	if tm.running then return end
	local evs = self:timerEvents(entry)
	local j
	for i, e in evs do if e.ms > tm.timer then j = i; break end end
	tm.running = true
	tm.startedAt = self.clock
	if j then
		tm.token += 1
		tm.pending = true
		self:scheduleTimerEvent(entry, tm, evs, j, evs[j].ms - tm.timer)
	end
end
function Rt:scheduleTimerEvent(entry, tm, evs, j, delay)
	local token = tm.token
	self:after(delay, function()
		if tm.token ~= token or not tm.running then return end
		local ev = evs[j]
		tm.timer = ev.ms -- nd->u.scr.timer = ted->time
		tm.startedAt = self.clock
		if j < #evs then
			self:scheduleTimerEvent(entry, tm, evs, j + 1, evs[j + 1].ms - ev.ms)
		else
			tm.pending = false
		end
		local p = tm.player
		if p and not self.players[p.rid] and p ~= self.nobody then
			self.log("npc_timerevent: Attached player not found.")
			return
		end
		self:runLabel(entry, ev.label, p)
	end)
end
function Rt:timerStop(entry, detach)
	local tm = self:timerState(entry)
	if detach then tm.player = nil end
	tm.token += 1
	tm.pending = false
	if tm.running then
		tm.timer += self.clock - tm.startedAt
		tm.running = false
	end
end
function Rt:timerSet(entry, ticks)
	local tm = self:timerState(entry)
	local flag = tm.running
	if flag then self:timerStop(entry, false) end
	tm.timer = ticks
	if flag then self:timerStart(entry, nil) end
end
function Rt:timerInit(entry, player)
	local tm = self:timerState(entry)
	tm.token += 1
	tm.running = false
	tm.pending = false
	tm.timer = 0
	self:timerStart(entry, player)
end
function Rt:timerGet(entry, kind)
	local tm = self:timerState(entry)
	if kind == 0 then return tm.timer + (tm.running and (self.clock - tm.startedAt) or 0)
	elseif kind == 1 then return tm.pending and 1 or 0
	elseif kind == 2 then return #self:timerEvents(entry) end
	return 0
end
function Rt:timerAttach(entry, player)
	self:timerState(entry).player = player
end

---------------------------------------------------------------------------------------------------------------------
-- report
---------------------------------------------------------------------------------------------------------------------
function Rt:report(opts)
	return Report.run(self.loader, opts)
end

return Rt
