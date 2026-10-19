--@rule TL-SIM-020
--@mods rathena.base,legacy.npc
--@npc npc/pre-re/other/resetskill.txt
-- A real legacy NPC script (npc/pre-re/other/resetskill.txt, the Prontera "Hypnotist") driven end to end
-- through a SimPlayer: CZ_CONTACTNPC, ZC_SAY_DIALOG + ZC_WAIT_DIALOG for every mes/next, ZC_MENU_LIST
-- for select(), CZ_CHOOSE_MENU as the answer, ZC_CLOSE_DIALOG at the end. The script runs in the
-- legacy.npc mod (the Luau-hosted interpreter) as a script-host dialogue coroutine.
local npc = "Hypnotist#2"
sim.npc_place(npc, 146, 232)
local p = sim.player { name = "neo", x = 146, y = 228 }
p:expect("appear", 100)

p:click_npc(npc)
local first = p:expect("say", 500)
sim.assert_true(string.find(first.message, "Greetings, adventurer.", 1, true) ~= nil, "first page of the script")
sim.assert_eq(p:expect("next", 100).NpcID, first.NpcID, "ZC_WAIT_DIALOG names the same NPC")
p:next()
local second = p:expect("say", 500)
sim.assert_true(string.find(second.message, "Base Level 40", 1, true) ~= nil, "second page")
p:expect("next", 100)
p:next()
local third = p:expect("say", 500)
sim.assert_true(string.find(third.message, "do you have any questions", 1, true) ~= nil, "third page")
p:expect("next", 100)
p:next()

-- select("Skill Reset?:I want a Skill Reset:Nothing")
local menu = p:expect("menu", 500)
local _, entries = string.gsub(menu.menu, ":", ":")      -- every option is followed by a colon (clif_scriptmenu)
sim.assert_eq(entries, 3, "three menu entries")
sim.assert_eq(menu.menu, "Skill Reset?:I want a Skill Reset:Nothing:", "menu text")
p:answer(1)                       -- case 1: "Skill Reset?" explains the service over three pages ending in close
local page = p:expect("say", 500)
sim.assert_true(string.find(page.message, "redistribute", 1, true) ~= nil, "answer 1 leads to the explanation")
p:expect("next", 100)
p:next()
p:expect("say", 500)
p:expect("next", 100)
p:next()
local last = p:expect("say", 500)
sim.assert_true(string.find(last.message, "Pushcart", 1, true) ~= nil, "last page")
p:expect("next", 100)
p:next()
p:expect("close", 500)
sim.assert_eq(sim.dialogue_of(p).active, false, "the dialogue is over")

-- a second visit, answered with "Nothing" (3): one parting page, then the dialogue ends
p:click_npc(npc)
for _ = 1, 3 do
	p:expect("say", 500)
	p:expect("next", 100)
	p:next()
end
p:expect("menu", 500)
p:answer(3)
p:expect("say", 500)             -- a parting line, then the Close button
p:expect("next", 100)
p:next()
p:expect("close", 500)

-- an answer outside 1..3 is rejected by the dialogue manager and leaves the dialogue open
p:click_npc(npc)
for _ = 1, 3 do
	p:expect("say", 500)
	p:expect("next", 100)
	p:next()
end
p:expect("menu", 500)
p:answer(9)
sim.run_for(100)
sim.assert_eq(sim.dialogue_of(p).active, true, "an out-of-range choice does not end the dialogue")
-- cancel (255 is what the client sends for the Cancel button)
p:answer(255)
p:expect("close", 500)
sim.assert_eq(sim.dialogue_of(p).active, false)
