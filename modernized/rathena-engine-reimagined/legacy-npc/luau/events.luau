--!nonstrict
-- Event labels of the legacy dialect: classification (the table of script.cpp `script_config`), bus names, and the per-player
-- 2-slot event queue.
--
-- Bus naming (decision, spec silent): every On* label of an NPC registers one hook on the engine event bus; the bus event is
-- "legacy:<Label>" with the label spelled as in the legacy configuration (OnInit, OnPCLoginEvent, OnTimer5000, OnClock0830,
-- OnMinute30, OnSun2359, ... or the script's own spelling for user-defined labels); the hook id is "<NPC unique name>::<Label>".
--
-- Q-J (is the exact 2-slot queue drop and the 100 ms dequeue a contract?), answered from npc.cpp:
--   * An event aimed at a player that is inside a script (`sd->npc_id != 0`) is not run, it is queued: the first free slot of
--     `sd->eventqueue[MAX_EVENTQUEUE]` takes the name (npc.cpp:1751-1756 `npc_event_sub`, :5110-5114 `npc_event`); MAX_EVENTQUEUE is 2
--     (map.hpp:73).
--   * When both slots are taken the NEW event is dropped: it is not stored, nothing older is displaced, a warning is logged and the
--     call returns 1 (npc.cpp:1758-1759, :5116-5117). So of three events A, B, C arriving while the player is busy, A and B run and
--     C is lost.
--   * When the player's script ends, `npc_event_dequeue` (npc.cpp:1176-1204; called from script.cpp:4331) takes slot 0, schedules it as a
--     player event timer of 100 ms (`pc_addeventtimer(sd, 100, ...)`), shifts slot 1 to slot 0 and clears slot 1; the 100 ms timer
--     then calls `npc_event` again, which queues the event again if the player is busy once more.
--   The behaviour is a contract only in the sense that scripts have been written against it; it is kept behind the compat flag
--   `compat.two_slot_event_queue` (default on). With the flag off the queue is unbounded and nothing is dropped.
local M = {}

M.QUEUE_SLOTS = 2
M.DEQUEUE_MS = 100

-- engine-fired player events (script.cpp:263-271), by label; value = npce_event name
M.PLAYER_EVENTS = {
	onpcdieevent = "die", onpckillevent = "killpc", onnpckillevent = "killnpc",
	onpcloginevent = "login", onpclogoutevent = "logout", onpcloadmapevent = "loadmap", onpcbaselvupevent = "baselvup",
	onpcjoblvupevent = "joblvup", onpcidentifyevent = "identify",
}
local FIXED = {
	oninit = "init", oninterifinit = "interif", oninterifinitonce = "interif",
	ontouch = "touch", ontouch_ = "touch", ontouchnpc = "touchnpc", onwhisperglobal = "whisper", oncommand = "command",
	onbuyitem = "shop", onsellitem = "shop", onguildbreak = "guild", ontimerquit = "timerquit",
	oninstanceinit = "instance", oninstancedestroy = "instance", onnavigenerate = "navi",
	onagitstart = "agit", onagitinit = "agit", onagitend = "agit", onagitstart2 = "agit", onagitinit2 = "agit", onagitend2 = "agit",
	onagitstart3 = "agit", onagitinit3 = "agit", onagitend3 = "agit",
}
-- prefix + digits families: kind, number of digits expected (0 = any count for OnTimer, whose argument is milliseconds)
local DIGITS = {
	{ "ontimer", "timer", nil }, { "onclock", "clock", 4 }, { "onminute", "minute", 2 }, { "onhour", "hour", 2 }, { "onday", "day", 4 },
	{ "onsun", "weekday", 4 }, { "onmon", "weekday", 4 }, { "ontue", "weekday", 4 }, { "onwed", "weekday", 4 },
	{ "onthu", "weekday", 4 }, { "onfri", "weekday", 4 }, { "onsat", "weekday", 4 },
}

-- Classifies a label. Returns nil when it does not start with "On" (case-insensitive); else { kind, label, arg? } with kind one of:
--   init interif guild agit instance navi touch touchnpc whisper command shop timer timerquit clock minute hour day weekday player user
function M.classify(label: string)
	local l = string.lower(label)
	if string.sub(l, 1, 2) ~= "on" then return nil end
	local fixed = FIXED[l]
	if fixed then return { kind = fixed, label = label } end
	if M.PLAYER_EVENTS[l] then return { kind = "player", label = label, event = M.PLAYER_EVENTS[l] } end
	for _, fam in DIGITS do
		local prefix = fam[1]
		if string.sub(l, 1, #prefix) == prefix then
			local rest = string.sub(l, #prefix + 1)
			if rest ~= "" and string.find(rest, "^%d+$") and (fam[3] == nil or #rest == fam[3]) and #rest <= 9 then
				-- (npc.cpp:1437 reads OnTimer with %11d: any digit run; the clock families print %02d%02d)
				return { kind = fam[2], label = label, arg = tonumber(rest) }
			end
		end
	end
	return { kind = "user", label = label }
end

function M.busName(label: string): string
	return "legacy:" .. label
end

-- Per-player queue. compat = true: MAX 2 slots, a push into a full queue is refused (the new event is the one lost).
local Queue = {}
Queue.__index = Queue
function M.newQueue(compat: boolean)
	return setmetatable({ compat = compat, items = {}, dropped = 0 }, Queue)
end
function Queue:push(name: string): boolean
	if self.compat and #self.items >= M.QUEUE_SLOTS then
		self.dropped += 1
		return false
	end
	self.items[#self.items + 1] = name
	return true
end
function Queue:pop(): string?
	return table.remove(self.items, 1)
end
function Queue:size(): number
	return #self.items
end

return M
