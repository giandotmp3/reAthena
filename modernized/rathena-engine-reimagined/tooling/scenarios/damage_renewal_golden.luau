--@rule BR-RULESET-020 BR-RULESET-115 BR-RULESET-119
--@mods rathena.base,ruleset.renewal
--@golden subset
--@seed 7
-- The renewal pipeline through the same harness: the same sim, a different ruleset package behind the
-- mod loader (mutually exclusive with the pre-renewal one). Golden inputs are the renewal cases recorded
-- from the REAL renewal code; E-3 gives 1380 in both rulesets.
local recs = sim.records(sim.eval("ruleset.renewal", [[
	local runner = SIM_IMPORT('tests/support/golden_runner')
	for _, c in ipairs(runner.load()) do
		if c.cat == 'worked_example' then
			local r = runner.run(c)
			print(table.concat({ '@case', c.id, tostring(r.ok), tostring(r.result and r.result.damage), tostring(c.out.damage), #c.out.draws }, '\t'))
		end
	end
]]), "@case")
sim.assert_true(#recs >= 5, "worked examples in the renewal golden data (" .. #recs .. ")")
local seen_1380 = false
for _, f in ipairs(recs) do
	sim.assert_eq(f[2], "true", "case " .. f[1] .. " matches the renewal legacy record")
	if f[4] == "1380" then
		seen_1380 = true
		sim.assert_eq(f[3], "1380", "E-3 under renewal")
	end
end
sim.assert_true(seen_1380, "E-3 is among them")

local pair = sim.records(sim.eval("ruleset.renewal", sim.LIT_SRC .. [[
	for _, c in ipairs(SIM_IMPORT('build/golden_weapon_re')) do
		if c.cat == 'worked_example' and c.out.damage == 1380 then print('@u\t' .. lit(c.a) .. '\t' .. lit(c.t)) end
	end
]]), "@u")[1]
local a = sim.dummy { name = "attacker", x = 10, y = 10, unit = pair[1] }
local t = sim.dummy { name = "target", x = 11, y = 10, unit = pair[2] }
local r = sim.attack(a, t)
sim.assert_true(r.ok, tostring(r.error))
sim.assert_eq(r.damage, 1380, "E-3 with the sim's seeded draws")
