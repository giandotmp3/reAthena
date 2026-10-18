--@rule BR-RULESET-020 BR-RULESET-031 BR-RULESET-037 BR-RULESET-115 BR-RULESET-119
--@mods rathena.base,ruleset.prerenewal
--@golden subset
--@seed 7
-- The pre-renewal damage pipeline called through the ruleset package (loaded by the mod loader, run in
-- the mod's own state) with GOLDEN inputs: every worked example of BR_RULESET.md section 7 is a case
-- recorded from the REAL legacy battle code (ruleset-prerenewal conformance/golden). The package's own
-- golden runner replays the recorded RNG draws in order and compares damage, damage2, type, div, flag,
-- dmg_lv and the number, kind and bounds of the draws.
local recs = sim.records(sim.eval("ruleset.prerenewal", [[
	local runner = SIM_IMPORT('tests/support/golden_runner')
	for _, c in ipairs(runner.load()) do
		if c.cat == 'worked_example' then
			local r = runner.run(c)
			print(table.concat({ '@case', c.id, tostring(r.ok), tostring(r.result and r.result.damage), tostring(r.result and r.result.damage2),
				tostring(c.out.damage), tostring(c.out.damage2), #c.out.draws, tostring(r.detail) }, '\t'))
		end
	end
]]), "@case")
sim.assert_true(#recs >= 4, "the worked examples are in the golden data (" .. #recs .. ")")
local by_damage = {}
for _, f in ipairs(recs) do
	sim.assert_eq(f[2], "true", "case " .. f[1] .. " matches the legacy record (" .. tostring(f[8]) .. ")")
	by_damage[tonumber(f[5])] = f
end
-- E-1 (BR-RULESET-031): Water 1000 vs Fire lv1, Deluge 15 -> 1150 -> x150% = 1725
sim.assert_eq(by_damage[1725] ~= nil and by_damage[1725][3], "1725", "E-1: Luau result")
-- E-3 (BR-RULESET-020): race +20% and size +15% multiply: 1380, not 1350
sim.assert_eq(by_damage[1380] ~= nil and by_damage[1380][3], "1380", "E-3: Luau result")
-- E-6 (BR-RULESET-037): both hands, right 1000 and left 800
local e6
for _, f in ipairs(recs) do if f[5] == "1000" and f[6] == "800" then e6 = f end end
sim.assert_eq(e6 ~= nil and e6[3] .. "/" .. e6[4], "1000/800", "E-6: Luau result")

-- The same inputs with the sim's SEEDED stream instead of the recorded draws: the E-3 case is
-- deterministic whatever the draws are (no ATK variance, no crit, always hit), and the pipeline's
-- draws are counted on the main stream (BR-RULESET-115).
local pair = sim.records(sim.eval("ruleset.prerenewal", sim.LIT_SRC .. [[
	for _, c in ipairs(SIM_IMPORT('build/golden_weapon_pre')) do
		if c.cat == 'worked_example' and c.out.damage == 1380 then print('@u\t' .. lit(c.a) .. '\t' .. lit(c.t)) end
	end
]]), "@u")[1]
local a = sim.dummy { name = "attacker", x = 10, y = 10, unit = pair[1] }
local t = sim.dummy { name = "target", x = 11, y = 10, unit = pair[2] }
local before = sim.rng().main
local first = sim.attack(a, t)
sim.assert_true(first.ok, tostring(first.error))
sim.assert_eq(first.damage, 1380, "E-3 with seeded draws")
sim.assert_true(first.draws >= 1, "the pipeline drew from the stream")
sim.assert_eq(sim.rng().main - before, first.draws, "and every draw is counted on the main stream")
