--@rule TL-SIM-005
--@mods rathena.base,legacy.npc
--@npc npc/pre-re/other/resetskill.txt
-- Architecture section 5 (hot reload) required test: reload while a legacy NPC dialogue is parked on a
-- select() menu. The dialogue finishes in generation N (it holds a lease on it); a dialogue started
-- after the reload runs in generation N+1; the retired generation is released when its last dialogue ends.
local npc = "Hypnotist#2"
sim.npc_place(npc, 146, 232)
local p = sim.player { name = "neo", x = 146, y = 228 }
local q = sim.player { name = "trinity", x = 147, y = 228 }

local function to_menu(player)
	player:click_npc(npc)
	for _ = 1, 3 do
		player:expect("say", 500)
		player:expect("next", 100)
		player:next()
	end
	return player:expect("menu", 500)
end

sim.assert_eq(sim.generation("legacy.npc"), 1, "first generation")
to_menu(p)
local d = sim.dialogue_of(p)
sim.assert_true(d.active, "parked on the select() menu")
sim.assert_eq(d.generation, 1, "the dialogue runs in generation 1")
sim.assert_true(sim.registrations("legacy.npc", 1) >= 1, "generation 1 holds the dialogue lease")

-- hot reload at a tick boundary
local r = sim.reload("legacy.npc")
sim.assert_true(r.ok, "reload accepted: " .. tostring(r.error))
sim.assert_eq(r.generation, 2, "generation 2 is active")
sim.assert_eq(sim.generation("legacy.npc"), 2)
sim.assert_eq(sim.dialogue_of(p).generation, 1, "the pending dialogue stays in generation 1")
sim.assert_true(sim.registrations("legacy.npc", 1) >= 1, "generation 1 is kept alive by the lease")

-- the pending select() is answered after the reload and completes in generation 1
p:answer(3)
p:expect("say", 500)
p:expect("next", 100)
p:next()
p:expect("close", 500)
sim.assert_eq(sim.dialogue_of(p).active, false, "dialogue finished")
sim.assert_eq(sim.registrations("legacy.npc", 1), 0, "generation 1 is fully released after its last dialogue")

-- a dialogue started now runs in generation 2 (a fresh interpreter state)
to_menu(q)
sim.assert_eq(sim.dialogue_of(q).generation, 2, "new dialogues use generation 2")
q:answer(3)
q:expect("say", 500)
q:expect("next", 100)
q:next()
q:expect("close", 500)

-- a parked dialogue that nobody answers is cancelled when the drain timeout (60 s of game time) passes
to_menu(p)
sim.assert_eq(sim.dialogue_of(p).generation, 2)
sim.assert_true(sim.reload("legacy.npc").ok, "second reload")
sim.assert_eq(sim.generation("legacy.npc"), 3)
sim.run_for(61000)
p:expect("close", 100)                     -- the world closed the dialogue window
sim.assert_eq(sim.dialogue_of(p).active, false, "cancelled by the drain timeout")
p:answer(3)                                  -- a late answer is ignored, nothing resumes
sim.run_for(100)
p:expect_none("say", 100)
