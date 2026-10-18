--@rule TL-SIM-006
--@mods sim.status.tick
--@mods-dir mods
-- Architecture section 5 (hot reload) required test: reload while a status tick is pending. Timers are
-- owned by (mod, generation): the old generation's closure is cancelled, the new generation schedules
-- its own, and the tick fires exactly once per period (never twice, never from the retired state).
local function ticks() return sim.storage("world", "world", "sim.status.tick", "ticks") or 0 end
local function last_gen() return sim.storage("world", "world", "sim.status.tick", "last_gen") end

local SOURCE = [[
local function tick()
	local w = storage.world()
	w:set("ticks", (w:get("ticks") or 0) + 1)
	w:set("last_gen", GEN)
	timers.after(1000, tick)
end
GEN = %d
timers.after(1000, tick)
]]

sim.run_for(400)
sim.assert_eq(ticks(), 0, "the first tick is pending (due at 1000 ms)")
sim.assert_eq(sim.generation("sim.status.tick"), 1)

-- reload at t = 400: generation 1's tick (due at 1000) must never fire
local r = sim.reload("sim.status.tick", string.format(SOURCE, 2))
sim.assert_true(r.ok, "reload: " .. tostring(r.error))
sim.assert_eq(r.generation, 2)
sim.run_for(3100)                  -- t = 3500: generation 2 ticks at 1400, 2400 and 3400
sim.assert_eq(ticks(), 3, "exactly one tick per period: the old closure did not fire")
sim.assert_eq(last_gen(), 2, "and every tick came from generation 2")

-- reload again with generation 2's next tick (4400) pending
sim.run_for(500)                   -- t = 4000
local r3 = sim.reload("sim.status.tick", string.format(SOURCE, 3))
sim.assert_true(r3.ok)
sim.run_for(2500)                  -- t = 6500: generation 3 ticks at 5000 and 6000
sim.assert_eq(ticks(), 5, "two more ticks, none from generation 2's cancelled timer at 4400")
sim.assert_eq(last_gen(), 3)

-- a reload that fails to load keeps the running generation and its pending tick
local bad = sim.reload("sim.status.tick", "this is not luau (")
sim.assert_eq(bad.ok, false, "a syntax error is refused")
sim.assert_eq(sim.generation("sim.status.tick"), 3, "generation 3 stays active")
sim.run_for(1000)
sim.assert_eq(ticks(), 6, "its tick still fires")
