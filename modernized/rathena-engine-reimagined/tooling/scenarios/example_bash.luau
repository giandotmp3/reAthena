--@rule BR-RULESET-020
--@mods rathena.base,ruleset.prerenewal
--@golden subset
--@seed 1
-- BR-RULESET-020, worked example E-3: card fixes multiply (race +20%, size +15% on 1000 -> 1380, not 1350).
--
-- The whole stack: a SimPlayer sends CZ_REQUEST_ACT2 (netproto frame), the world/session boundary
-- runs the loaded ruleset's weapon pipeline (pre-renewal package, loaded by the mod loader) with the
-- sim's seeded RNG, and the client receives ZC_NOTIFY_ACT with the damage. The two units are the ones
-- the REAL legacy code was recorded with (ruleset-prerenewal conformance/golden, worked example E-3).
--
-- The original scaffold example cast Bash (sm_bash). The ruleset packages implement the basic weapon
-- attack only (no skill formulas), so the cast version is the pending test TL-SIM-003; this scenario
-- is the same shape with the attack the packages do implement.
local recs = sim.records(sim.eval("ruleset.prerenewal", sim.LIT_SRC .. [[
	local golden = SIM_IMPORT('build/golden_weapon_pre')
	for _, c in ipairs(golden) do
		if c.cat == 'worked_example' and c.a.bonus and c.a.bonus.add_race and c.out.damage == 1380 then
			print('@e3\t' .. lit(c.a) .. '\t' .. lit(c.t) .. '\t' .. c.out.damage)
		end
	end
]]), "@e3")
sim.assert_eq(#recs, 1, "one recorded E-3 case")
local attacker_unit, target_unit, legacy_damage = recs[1][1], recs[1][2], tonumber(recs[1][3])
sim.assert_eq(legacy_damage, 1380, "the REAL legacy code recorded 1380")

local knight = sim.player { name = "knight", x = 100, y = 100, job = "knight", level = 50, unit = attacker_unit }
local mob = sim.dummy { name = "dummy", x = 101, y = 100, unit = target_unit }
knight:expect("appear", 100)          -- the dummy is in view
knight:attack(mob)
local hit = knight:expect("damage", 500)
sim.assert_eq(hit.damage, 1380, "damage in ZC_NOTIFY_ACT: race +20% then size +15% multiply on 1000")
sim.assert_eq(hit.src, knight.gid, "source")
sim.assert_eq(hit.dst, mob, "target")
sim.assert_eq(sim.last_damage(mob), 1380, "recorded on the unit")
sim.assert_true(sim.rng().main >= 1, "the pipeline drew from the sim's main RNG stream")
