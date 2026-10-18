--!nonstrict
-- Luau counterpart of the legacy record hook (replay/patch/replay_hook.inc): runs ONE NPC of the legacy
-- dialect through legacy_interp with scripted answers and emits the same trace format.
-- Does not modify legacy_interp: commands are wrapped, the driver loop is its own.
-- Needs the legacy_interp modules (and the prep.py data modules) next to it: see replay/run_replay.sh.
local I = require("./interp")
local Base = require("./commands")
local Ext = require("./commands_ext")
local W = require("./world")
local C = require("./corpus")
local parser = require("./parser")
local lower = require("./lower")
local R = require("./npcrun")
local K = parser.K

-- world data recorded from the REAL legacy VM by the harness (run_replay.py writes world_data.luau next to this file), exposed to
-- legacy_interp through its data-provider interface (world.luau: env.data). Absent file = default provider (unknown data).
local WORLD = nil
local LOOKUP = {}
local MISSES = {}
local T_FMT
do
	local ok, wd = pcall(require, "./world_data")
	if ok and type(wd) == "table" then
		LOOKUP = wd.lookup or {}
		local names, bflag, castle, shop, maps, aegis = wd.itemname or {}, wd.battleflag or {}, wd.castle or {}, wd.shop or {}, wd.map or {}, wd.aegis or {}
		WORLD = {
			itemname = function(key)
				if type(key) == "string" then return names[key] or names[string.upper(key)] end
				return names[key]
			end,
			itemid = function(key)
				if type(key) == "string" then return aegis[key] end
				if names[key] ~= nil then return key end
				return nil
			end,
			-- static game-data answers recorded from the real VM for exactly the queries this run made (demand-driven probe, see
			-- run_replay.py); a query without a recorded answer is logged as a miss and answered "unknown"
			lookup = function(cmd, ...)
				local n = select("#", ...)
				local parts = table.create(n)
				for i = 1, n do parts[i] = T_FMT(select(i, ...)) end
				local key = cmd .. "(" .. table.concat(parts, ",") .. ")"
				local v = LOOKUP[key]
				if v == nil then MISSES[key] = true end
				return v
			end,
			battleflag = function(name) return bflag[name] end,
			castle = function(map) return castle[map] == true end,
			shop = function(name) return shop[name] == true end,
			mapexists = function(map) return maps[map] == true end,
		}
	end
end

local T = {}
T.misses = MISSES

-- core VM operations that the legacy side does not record either (they are compiled to jumps / locals there)
local SKIP = { set = true, setr = true, jump_zero = true, ["goto"] = true, callsub = true, callfunc = true, ["return"] = true,
	getarg = true, getargcount = true, ["end"] = true, getelementofarray = true, getd = true, setd = true }
-- commands that block: the legacy hook records them when they are called, before the client answers
local PRE = { next = true, close2 = true, sleep = true, sleep2 = true }

local function fmtStr(s: string): string
	local out = { '"' }
	for i = 1, #s do
		local c = string.byte(s, i)
		if c == 92 then out[#out + 1] = "\\\\"
		elseif c == 34 then out[#out + 1] = '\\"'
		elseif c == 10 then out[#out + 1] = "\\n"
		elseif c == 13 then out[#out + 1] = "\\r"
		elseif c == 9 then out[#out + 1] = "\\t"
		elseif c < 32 or c == 127 then out[#out + 1] = string.format("\\x%02x", c)
		else out[#out + 1] = string.char(c) end
	end
	out[#out + 1] = '"'
	return table.concat(out)
end
local function fmtNum(v: number): string
	if v == math.floor(v) and math.abs(v) < 2^53 then return string.format("%d", v) end
	return string.format("%.14g", v)
end
local function refName(ref): string
	if ref[1] == "param" then return string.lower(ref[2]) end
	local n = string.lower(ref[1] .. ref[2])
	local idx = ref[3]
	if idx and idx ~= 0 then n ..= "[" .. fmtNum(idx) .. "]" end
	return n
end
local function fmtVal(v): string
	local t = type(v)
	if t == "number" then return fmtNum(v)
	elseif t == "string" then return fmtStr(v)
	elseif t == "table" then return "&" .. refName(v)
	end
	return "<" .. t .. ">"
end

T_FMT = fmtVal

-- is argument position k (1-based) a reference position of the builtin spec? (same walk as the C++ side)
local function specIsRef(spec: string?, k: number): boolean
	if not spec then return false end
	local n = #spec
	local c
	local i = k
	if i <= n and string.sub(spec, i, i) ~= "?" and string.sub(spec, i, i) ~= "*" then
		c = string.sub(spec, i, i)
	else
		local j = math.min(i - 1, n)
		while j > 0 and (string.sub(spec, j, j) == "?" or string.sub(spec, j, j) == "*") do j -= 1 end
		c = j > 0 and string.sub(spec, j, j) or ""
	end
	return c == "r"
end

-- the legacy hook overwrites the result of a successful rand() with this sequence (LCG over a call counter); the interpreter's own
-- argument validation (failure + end of script for an empty range) still applies because the host only supplies the random source
local function scriptedRand(env, lo, hi)
	env.randN = (env.randN or 0) + 1
	local r = (1103515245 * env.randN + 12345) % 2147483648
	return lo + r % (hi - lo + 1)
end

-- wrap a command table so that every call is recorded (after completion, as on the legacy side)
local function traced(cmdTable)
	local out = {}
	local names = {}
	local seen = {}
	local function collect(tbl)
		for k in tbl do if not seen[k] then seen[k] = true; names[#names + 1] = k end end
		local mt = getmetatable(tbl)
		if mt and type(mt.__index) == "table" then collect(mt.__index) end
	end
	collect(cmdTable)
	for _, name in names do
		local h = cmdTable[name]
		if SKIP[name] then
			out[name] = h
		else
			out[name] = function(st, ...)
				local n = select("#", ...)
				local args = table.pack(...)
				local ok, r
				local lineIdx
				if PRE[name] then lineIdx = #st.replayOut + 1; st.replayOut[lineIdx] = name .. "(" .. table.concat((function() local t = {} for i = 1, n do t[i] = fmtVal(args[i]) end return t end)(), ",") .. ")" end
				if name == "gettime" then
					local a = ...
					ok, r = true, (a >= 1 and a <= 8) and ({ 56, 34, 12, 0, 18, 10, 2026, 291 })[a] or -1
				elseif name == "gettimetick" then
					local a = ...
					ok, r = true, a == 0 and 1000000 or a == 1 and 1792326896 or a == 2 and 45296 or 0
				else
					ok, r = pcall(h, st, ...)
				end
				local parts = table.create(n)
				for i = 1, n do parts[i] = fmtVal(args[i]) end
				local line = name .. "(" .. table.concat(parts, ",") .. ")"
				if ok and r ~= nil and type(r) ~= "table" then line ..= " -> " .. fmtVal(r) end
				if st.cmdFailed then line ..= " !F"; st.cmdFailed = false end -- legacy SCRIPT_CMD_FAILURE
				if not lineIdx then st.replayOut[#st.replayOut + 1] = line end
				if not ok then error(r, 0) end
				return r
			end
		end
	end
	return out
end
local TRACED_EXT = traced(Ext.commands)

local function softHandler(st, name, argNodes)
	local spec = C.builtins[name]
	local parts = {}
	for i, node in argNodes do
		local ok, v
		if node[1] == K.E_VAR and specIsRef(spec, i) then
			ok, v = pcall(function()
				local idx = node[4]
				if idx then idx = I.eval(st, idx) end
				return { node[2], node[3], idx }
			end)
		else
			ok, v = pcall(I.eval, st, node)
		end
		if not ok and v == I.END then error(v, 0) end -- e.g. a failing rand() in the arguments ends the script, as in the legacy VM
		parts[i] = ok and fmtVal(v) or "<err>"
	end
	st.replayOut[#st.replayOut + 1] = name .. "(" .. table.concat(parts, ",") .. ") !U"
	return 0
end

local function snapshotInto(snap, prefix, store)
	for name, v in store do
		if type(v) == "table" then
			for idx, x in v do
				local key = prefix .. name .. (idx ~= 0 and ("[" .. idx .. "]") or "")
				snap[key] = type(x) == "number" and fmtNum(x) or fmtStr(x)
			end
		else
			snap[prefix .. name] = type(v) == "number" and fmtNum(v) or fmtStr(v)
		end
	end
end
local function snapshot(player, npcVars, env)
	local s = {}
	snapshotInto(s, "", player.perm); snapshotInto(s, "@", player.temp); snapshotInto(s, "#", player.acc)
	snapshotInto(s, "##", player.acc2); snapshotInto(s, ".", npcVars); snapshotInto(s, "$", env.global)
	snapshotInto(s, "$@", env.gtemp); snapshotInto(s, "'", env.instance)
	return s
end

local PARAMS = { "zeny", "baselevel", "joblevel", "class", "statuspoint", "skillpoint", "baseexp", "jobexp", "karma", "manner" }

local function newPlayer(cfg, map, x, y)
	local p = W.newPlayer("poor")
	p.name = cfg.name or "Tester"; p.party = ""; p.guild = ""; p.map = (map == "-") and "prontera" or map
	p.x, p.y = x or 150, y or 150
	for k in p.params do p.params[k] = 0 end
	p.params.baselevel = 1; p.params.joblevel = 1; p.params.sex = 1; p.params.maxweight = 20000 -- as the legacy synthetic player
	for _, k in PARAMS do if cfg[k] then p.params[k] = cfg[k] end end
	if cfg.sex then p.params.sex = cfg.sex end
	p.defaultItemCount = 0
	return p
end

-- registries of function scripts (shared by all cases of a process), one per load set
local registries = {}
local function loadFuncs(uf, setname)
	local funcs, n, failed = {}, 0, 0
	for _, fi in C.setfiles(setname) do
		local text = C.text(fi)
		if string.find(text, "function\tscript", 1, true) then
			local r = C.ds.split(text)
			for _, d in r.decls do
				if d.kind == "function" then
					local ast = parser.parse(d.body, C.builtins, uf)
					local prog = ast and lower.lower(ast, { name = d.name, builtins = C.builtins, userfuncs = uf })
					if prog then funcs[string.lower(d.name)] = prog; n += 1 else failed += 1 end
				end
			end
		end
	end
	return funcs, n, failed
end
local function registry(setname)
	local r = registries[setname]
	if not r then
		local uf = C.userfuncs()
		local funcs, n, failed = loadFuncs(uf, setname)
		r = { uf = uf, funcs = funcs, n = n, failed = failed }
		registries[setname] = r
	end
	return r
end
function T.setup(setname)
	local r = registry(setname or "re")
	return r.n, r.failed
end

-- find and lower one NPC. returns prog, mapname, line or nil, err
function T.loadNpc(path: string, npcName: string, setname: string)
	local registry = registry(setname or "re")
	local text
	if path == "synthetic" then
		text = require("./synthetic_src")[1]
		if not registry.synthLoaded then
			-- function scripts declared in the synthetic file join the registry of the load set
			registry.synthLoaded = true
			local r0 = C.ds.split(text)
			for _, d in r0.decls do
				if d.kind == "function" then registry.uf[string.lower(d.name)] = true end
			end
			for _, d in r0.decls do
				if d.kind == "function" then
					local ast = parser.parse(d.body, C.builtins, registry.uf)
					local prog = ast and lower.lower(ast, { name = d.name, builtins = C.builtins, userfuncs = registry.uf })
					if prog then registry.funcs[string.lower(d.name)] = prog end
				end
			end
		end
	else
		local fi = R.fileIndex(path)
		if not fi then return nil, "file-not-in-manifest" end
		text = C.text(fi)
	end
	local r = C.ds.split(text)
	for _, d in r.decls do
		if d.kind == "script" and d.name == npcName then
			local ast, info = parser.parse(d.body, C.builtins, registry.uf)
			if not ast then return nil, "parse:" .. tostring(info.cause) end
			local prog, lerr = lower.lower(ast, { name = d.name, file = path, builtins = C.builtins, userfuncs = registry.uf })
			if not prog then return nil, "lower:" .. tostring(lerr.cause) end
			prog.baseLine = C.ds.lineof(text, d.pos)
			local map = string.match(d.w1 or "", "^([^,]*)") or "prontera"
			
			return prog, map
		end
	end
	return nil, "npc-not-found"
end

local function errTag(e)
	local s = tostring(e.type)
	if e.cmd then s ..= ":" .. e.cmd end
	return s
end

-- Run one case; returns array of trace lines (same format as the legacy hook writes).
-- case: { path, npc, answers = {string}, player = {..}, cap = 60 }
function T.runCase(case)
	local setname = case.set or "re"
	local prog, map = T.loadNpc(case.path, case.npc, setname)
	if not prog then return { "#end npc-not-found" }, nil, map end
	local env = W.newEnv(registry(setname).funcs, 12345, WORLD)
	env.rand = function(lo, hi) return scriptedRand(env, lo, hi) end
	local npcVars = {}
	env.npcvars[prog.name] = npcVars
	local npc = { vars = npcVars, name = prog.name, path = (case.path == "synthetic") and "npc/replay_synth.txt" or case.path, map = (map == "-") and "" or map, x = (map == "-") and 0 or case.x, y = (map == "-") and 0 or case.y }
	local function mkState(player, out)
		local st = I.newState{ player = player, env = env, npc = npc, commands = TRACED_EXT, unsupported = softHandler,
			counts = {}, limit = 500000, glimit = 2048 }
		st.baseLimit, st.baseGlimit = st.limit, st.glimit
		st.replayOut = out
		return st
	end
	-- start state: either seeded from the legacy VM (case.init = { {key, value}, ... }: '$', '$@' and this NPC's '.' variables as the
	-- real server left them after its OnInit run), or by running this NPC's own OnInit here with no player (untraced)
	local initLabel = not case.init and prog.labels["oninit"]
	if case.init then
		for _, kv in case.init do
			local scope, name, idx = I.parseVarName(kv[1])
			local store = scope == "$" and env.global or scope == "$@" and env.gtemp or scope == "." and npcVars or nil
			if store then I.setvarIn(store, name, idx, kv[2]) end
		end
	end
	if initLabel then
		local scratch = mkState(newPlayer({}, map, case.x, case.y), {})
		local co = I.spawn(scratch, prog, initLabel, {})
		local guard = 0
		while true do
			local ok, kind, payload = coroutine.resume(co, nil)
			if not ok or coroutine.status(co) == "dead" then break end
			guard += 1
			if guard > 50 then break end
		end
	end
	local player = newPlayer(case.player or {}, map, case.x, case.y)
	local before = snapshot(player, npcVars, env)
	local lines = {}
	local st = mkState(player, lines)
	local entry = 1
	if case.label then entry = prog.labels[string.lower(case.label)]; if not entry then return { "#end label-not-found" }, nil, prog end end
	local co = I.spawn(st, prog, entry, {})
	local answers = case.answers or {}
	local ai = 0
	local cap = case.cap or 60
	local inter = 0
	local reason
	local answer
	while true do
		local ok, kind, payload = coroutine.resume(co, answer)
		if not ok then reason = "error:HOST:" .. tostring(kind); break end
		if coroutine.status(co) == "dead" then
			if kind == "end" then reason = "end" else reason = "error:" .. errTag(payload or {}); st.replayErr = payload end
			break
		end
		inter += 1
		if inter > cap then reason = "cap"; break end
		answer = nil
		local tok
		if kind == "menu" or kind == "select" or kind == "prompt" or kind == "input" then
			ai += 1
			tok = answers[ai]
		end
		if kind == "menu" then
			-- label-form menu: recorded here (completion), visible option texts only
			local parts = {}
			for i, s in payload do parts[i] = fmtStr(s) end
			lines[#lines + 1] = "menu(" .. table.concat(parts, ",") .. ")"
		end
		if kind == "menu" or kind == "select" or kind == "prompt" then
			local n = #payload
			local pick = tok and tonumber(tok) or 1
			if pick <= 0 then answer = nil
			else answer = math.min(math.floor(pick), n) end
		elseif kind == "input" then
			if payload.str then answer = tok or "test" else answer = tok and tonumber(tok) or 1 end
		elseif kind == "sleep" or kind == "sleep2" then
			env.clock += payload
		end
	end
	lines[#lines + 1] = "#end " .. reason
	-- final state: delta of variable stores + absolute params / items
	local after = snapshot(player, npcVars, env)
	local state = {}
	for k, v in after do if before[k] ~= v then state[#state + 1] = "=" .. k .. "=" .. v end end
	for k in before do if after[k] == nil then state[#state + 1] = "=" .. k .. "=<unset>" end end
	for _, k in PARAMS do
		local v = player.params[k]
		if v and v ~= 0 then state[#state + 1] = "%" .. k .. "=" .. fmtNum(v) end
	end
	for id, amount in player.inv do
		if amount ~= 0 then state[#state + 1] = "+item " .. fmtNum(id) .. "=" .. fmtNum(amount) end
	end
	table.sort(state)
	for _, s in state do lines[#lines + 1] = s end
	return lines, st, prog
end

return T
