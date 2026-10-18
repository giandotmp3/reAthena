--!nonstrict
-- Runtime command layer: the commands whose behaviour needs the runtime (dialogue text, NPC state, event queue and timers) rather than
-- the empty-world model of commands_ext. It extends commands_ext (same table, metatable __index), so everything the replay-validated
-- layer implements is unchanged; each override here falls back to the replay-validated behaviour when the environment has no runtime
-- (`st.env.rt == nil`). Handlers record through `rec` first, exactly like the base layer, so traces keep their shape.
local I = require("./interp")
local Ext = require("./commands_ext")
local Base = require("./commands")
local rec, tostr, END = Base.rec, I.tostr, I.END
local cmdFail = I.cmdFail

local C = setmetatable({}, { __index = Ext.commands })
local M = { commands = C }

local function rtOf(st) return st.env.rt end

-- ---- dialogue text ---------------------------------------------------------------------------------------------------------
-- legacy: every argument of `mes` is one more line (BUILDIN_FUNC(mes))
C.mes = function(st, ...)
	rec(st, "mes", ...)
	if st.block then
		local buf = st.mesbuf
		if not buf then buf = {}; st.mesbuf = buf end
		for i = 1, select("#", ...) do buf[#buf + 1] = tostr((select(i, ...))) end
	end
end
C.clear = function(st)
	rec(st, "clear")
	if st.mesbuf then table.clear(st.mesbuf) end
end
-- close = send the close button and end the script; the buffered text is delivered with it
C.close = function(st)
	rec(st, "close")
	if st.block then I.yield(st, "close", nil) end
	error(END, 0)
end
C.close3 = C.close
-- progressbar: STOP until the client reports the bar finished; progressbar_npc: the script sleeps for the duration (RERUNLINE)
C.progressbar = function(st, color, seconds)
	rec(st, "progressbar", color, seconds)
	I.yield(st, "progressbar", (seconds or 0) * 1000)
end
C.progressbar_npc = function(st, color, seconds, name)
	rec(st, "progressbar_npc", color, seconds, name)
	if (seconds or 0) < 0 then cmdFail(st); return end
	I.yield(st, "progressbar_npc", (seconds or 0) * 1000)
end

-- ---- permission-gated commands (interp.luau refuses them before this point unless the permission is granted) ----------------
-- The effect itself belongs to the host: env.hooks.<command>(st, ...) -> value. Without a hook the command fails like a command that
-- found nothing to do (SCRIPT_CMD_FAILURE) and returns 0.
local function hosted(name)
	C[name] = function(st, ...)
		rec(st, name, ...)
		local hooks = st.env.hooks
		local h = hooks and hooks[name]
		if h then return h(st, ...) end
		cmdFail(st)
		return 0
	end
end
hosted("atcommand"); hosted("query_sql"); hosted("setunitdata")

-- ---- NPC events ---------------------------------------------------------------------------------------------------------------
-- donpcevent "Npc::OnLabel": runs the label NOW with no player attached (npc_event_do); 1 when the event exists, else 0
C.donpcevent = function(st, name)
	rec(st, "donpcevent", name)
	local rt = rtOf(st)
	if not rt then return 0 end
	return rt:fireEvent(tostr(name), nil, st) and 1 or 0
end
-- doevent "Npc::OnLabel": the event for the attached player (npc_event(sd, ..)); queued when the player is inside a script, which is
-- always the case for a command executed by a script of that player
C.doevent = function(st, name)
	rec(st, "doevent", name)
	local rt = rtOf(st)
	if not rt then return 0 end
	rt:fireEvent(tostr(name), st.player, st)
	return nil
end
C.enablenpc = function(st, name)
	rec(st, "enablenpc", name)
	local rt = rtOf(st)
	if not rt then return end
	if not rt:setEnabled(tostr(name), true) then cmdFail(st) end
end
C.disablenpc = function(st, name)
	rec(st, "disablenpc", name)
	local rt = rtOf(st)
	if not rt then return end
	if not rt:setEnabled(tostr(name), false) then cmdFail(st) end
end

-- ---- NPC timers (OnTimer<ms> labels): npc.cpp npc_timerevent_* ------------------------------------------------------------
-- argument forms of initnpctimer/startnpctimer/stopnpctimer: ({"npc"}{,flag}) | (flag)
local function timerTarget(st, a, b)
	local rt = rtOf(st)
	local npc, flag = st.npc, 0
	if b ~= nil then npc = rt.loader:find(tostr(a)); flag = b
	elseif a ~= nil then
		if type(a) == "string" then npc = rt.loader:find(a) else flag = a end
	end
	return npc, flag, rt
end
C.initnpctimer = function(st, a, b)
	rec(st, "initnpctimer", a, b)
	if not rtOf(st) then return end
	local npc, flag, rt = timerTarget(st, a, b)
	if not npc then return end
	rt:timerInit(npc, flag ~= 0 and st.player or nil)
end
C.startnpctimer = function(st, a, b)
	rec(st, "startnpctimer", a, b)
	if not rtOf(st) then return end
	local npc, flag, rt = timerTarget(st, a, b)
	if not npc then return end
	rt:timerStart(npc, flag ~= 0 and st.player or nil)
end
C.stopnpctimer = function(st, a, b)
	rec(st, "stopnpctimer", a, b)
	if not rtOf(st) then return end
	local npc, flag, rt = timerTarget(st, a, b)
	if not npc then return end
	rt:timerStop(npc, flag ~= 0)
end
C.getnpctimer = function(st, kind, name)
	rec(st, "getnpctimer", kind, name)
	local rt = rtOf(st)
	if not rt then return 0 end
	local npc = (name ~= nil) and rt.loader:find(tostr(name)) or st.npc
	if not npc then cmdFail(st); return 0 end
	return rt:timerGet(npc, kind)
end
C.setnpctimer = function(st, tick, name)
	rec(st, "setnpctimer", tick, name)
	local rt = rtOf(st)
	if not rt then return 0 end
	local npc = (name ~= nil) and rt.loader:find(tostr(name)) or st.npc
	if not npc then cmdFail(st); return 1 end
	rt:timerSet(npc, tick)
	return 0
end
C.attachnpctimer = function(st)
	rec(st, "attachnpctimer")
	local rt = rtOf(st)
	if not rt then return 0 end
	rt:timerAttach(st.npc, st.player)
	return 0
end
C.detachnpctimer = function(st, name)
	rec(st, "detachnpctimer", name)
	local rt = rtOf(st)
	if not rt then return 0 end
	local npc = (name ~= nil) and rt.loader:find(tostr(name)) or st.npc
	if not npc then cmdFail(st); return 1 end
	rt:timerAttach(npc, nil)
	return 0
end

-- ---- player event timers (pc_addeventtimer) ---------------------------------------------------------------------------------
C.addtimer = function(st, ticks, name)
	rec(st, "addtimer", ticks, name)
	local rt = rtOf(st)
	if not rt then return 0 end
	if not rt:addPlayerTimer(st.player, ticks, tostr(name)) then cmdFail(st) end -- table of 32 timers is full
end
C.deltimer = function(st, name)
	rec(st, "deltimer", name)
	local rt = rtOf(st)
	if not rt then return 0 end
	rt:delPlayerTimer(st.player, tostr(name))
end
C.addtimercount = function(st, ticks, name)
	rec(st, "addtimercount", ticks, name)
	local rt = rtOf(st)
	if not rt then return 0 end
	rt:addPlayerTimerCount(st.player, tostr(name), ticks)
end

return M
