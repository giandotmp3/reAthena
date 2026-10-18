--@rule BR-RULESET-115
--@mods sim.hook.rng.a,sim.hook.rng.b
--@mods-dir mods
--@seed 99
-- BR-RULESET-115 / architecture section 5, RNG: every third-party hook draws from its OWN derived stream,
-- so adding a hook never shifts the main stream or another hook's numbers; the main stream is the one
-- the default pipelines use and it counts its draws.
local function rolls(result)
	local out = {}
	for _, e in ipairs(result.effects) do out[#out + 1] = e.kind .. "=" .. e.payload end
	return out
end
local before = sim.rng()
local first = sim.dispatch("sim:roll", 0)
local second = sim.dispatch("sim:roll", 0)
sim.assert_eq(first.errors[1], nil, "no hook failed")
sim.assert_eq(#first.effects, 3, "a rolled once, b rolled twice")
sim.assert_ne(table.concat(rolls(first), ","), table.concat(rolls(second), ","), "each dispatch advances the hooks' streams")
sim.assert_eq(sim.rng().main, before.main, "hook draws never touch the main stream")
