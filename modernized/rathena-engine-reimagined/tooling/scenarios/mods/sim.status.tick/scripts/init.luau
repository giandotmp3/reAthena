--!nonstrict
-- A status tick: every 1000 ms of game time the tick runs once and re-arms itself. The tick count is
-- kept in world storage so it survives the sandbox swap of a hot reload.
local function tick()
	local w = storage.world()
	local n = w:get("ticks") or 0
	w:set("ticks", n + 1)
	w:set("last_gen", GEN)
	timers.after(1000, tick)
end
GEN = 1
timers.after(1000, tick)
