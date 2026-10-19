--!nonstrict
-- The status layer inside the engine's attribute contract (engine-core attrs.hpp) and its extension points: a mod adds a status with a position, the
-- ruleset's stage list is the only thing that knows how contributions combine.
local T = import("tests/support/harness")
local S = import("scripts/status/init")
local model = import("scripts/status/engine_model")
local status = S.status
local runner = import("tests/support/status_runner")
local golden = runner.load()

local function active_of(case)
	local active = {}
	for name, vals in pairs(case.sc) do
		local key = status.by_sc(name)
		if key then active[key] = vals end
	end
	return active
end

T.run("BR-RULESET-065", "the aggregation is expressible in engine-core's AttributeSystem: stage lists + (source, tag, value) contributions reproduce the legacy output", false, function()
	local sys = model.new()
	local checked = 0
	for i, case in ipairs(golden.cases) do
		if i % 7 == 0 and case.cat ~= "states" and case.cat ~= "states_combo" then
			local active = active_of(case)
			for fn, base in pairs(case["in"]) do
				local attr = status.attribute(fn)
				local act = (case.unit.nosc and not attr.reads_unit_sc) and {} or active
				local F = S.aggregate.facts_for(case.unit, act)
				sys:define_stages(fn, attr.stages)
				for _, c in ipairs(S.aggregate.contributions(fn, base, case.unit, act, F)) do
					assert(type(c.source) == "string" and type(c.tag) == "string" and type(c.value) == "number", "a contribution is exactly (source, tag, value)")
					local n = 0
					for _ in pairs(c) do n += 1 end
					assert(n == 3, "no hidden channel next to source / tag / value")
					sys:add_contribution(case.id, fn, c)
				end
				local got = sys:get(case.id, fn)
				assert(got == case.out[fn], ("case %d %s: engine model %s, legacy %s"):format(case.id, fn, tostring(got), tostring(case.out[fn])))
				assert(sys:recompute_from_scratch(case.id, fn) == got)
				checked += 1
			end
		end
	end
	assert(checked > 8000, checked .. " outputs checked")
end)

T.run("BR-RULESET-065", "AttributeSystem semantics: reads 0 without a stage list, caches until touched, remove_source drops one status' contributions", false, function()
	local sys = model.new()
	assert(sys:get(1, "str") == 0, "an attribute without a stage list reads 0")
	sys:define_stages("str", status.attribute("str").stages)
	local F = S.aggregate.facts_for({ kind = "mob" }, { cheerup = { 1, 0, 0, 0 }, loud = { 1, 0, 0, 0 } })
	for _, c in ipairs(S.aggregate.contributions("str", 50, { kind = "mob" }, { cheerup = { 1, 0, 0, 0 }, loud = { 1, 0, 0, 0 } }, F)) do sys:add_contribution(7, "str", c) end
	assert(sys:get(7, "str") == 57) -- 50 + 3 (Cheer Up) + 4 (Loud)
	assert(sys:remove_source(7, "loud") == 1)
	assert(sys:get(7, "str") == 53)
	sys:add_contribution(7, "str", { source = "mod", tag = "ops|99999|add|", value = 10 })
	assert(sys:get(7, "str") == 63, "adding a contribution invalidates the cached value")
	assert(sys:get(8, "str") == 0 or sys:get(8, "str") == model.fold(status.attribute("str").stages, {}), "entities do not share contributions")
end)

T.run("BR-RULESET-065", "a mod can define a status with a position between two legacy statements; the order key `at` decides add versus rate (legacy numbers 1 / 2 / 3)", false, function()
	-- Cheer Up adds 3 at 6826, Full Throttle adds str * val3 / 100 at 6824: base 100 -> (100 + 20%) + 3 = 123 (worked example). A new buff at 6825 lands between them.
	status.define("zz_mod_buff", {
		fields = { val1 = "bonus" },
		contributes = { str = { "add", "bonus", at = 6825 } },
	})
	local unit = { kind = "mob" }
	local base = S.calc("str", 100, unit, { cheerup = { 1, 0, 0, 0 }, full_throttle = { 1, 0, 20, 0 } })
	assert(base == 123)
	local with = S.calc("str", 100, unit, { cheerup = { 1, 0, 0, 0 }, full_throttle = { 1, 0, 20, 0 }, zz_mod_buff = { 10, 0, 0, 0 } })
	assert(with == 123 + 10 + 0, "the mod's +10 lands after the 20 % term (6825 > 6824): " .. with)
	status.undefine("zz_mod_buff")
	status.define("zz_mod_buff", { fields = { val1 = "bonus" }, contributes = { str = { "add", "bonus", at = 6823.5 } } })
	local before = S.calc("str", 100, unit, { cheerup = { 1, 0, 0, 0 }, full_throttle = { 1, 0, 20, 0 }, zz_mod_buff = { 10, 0, 0, 0 } })
	assert(before == 135, "before the rate term the +10 is multiplied too (100 + 10 = 110, + 22 = 132, + 3 = 135): " .. before)
	status.undefine("zz_mod_buff")
	assert(status.get("zz_mod_buff") == nil)
end)

T.run("BR-RULESET-065", "the stage lists are the ruleset's: every attribute has a stage list that starts with the base value and every op belongs to a stage of its list", false, function()
	local n = 0
	for name, attr in pairs(import("scripts/status/attrs_re").attributes) do
		n += 1
		assert(attr.stages[1].id == "base", name)
		local ids = {}
		for _, st in ipairs(attr.stages) do ids[st.id] = true; assert(type(st.fn) == "function") end
		for op, stage in pairs(attr.ops) do assert(ids[stage], name .. ": op " .. op .. " names the stage " .. stage .. " which the list lacks") end
	end
	assert(n == 48, n .. " attributes") -- 41 outputs of the hook + speed_rate and the fix / rate attributes behind the maximum HP / SP / AP
end)

-- the stage functions on their own (the aggregator already reduces a group to its best member; a mod that feeds contributions directly gets the same rule)
T.run("BR-RULESET-068", "stage level: of several ASPD potion contributions only the lowest rank (the strongest potion) applies, in the fixed group and in the rate", false, function()
	local function contrib(rank, value) return { source = "p" .. rank, tag = "potion|8306|potion|" .. rank, value = value } end
	local base = { source = "base", tag = "base|0||", value = 0 }
	assert(model.fold(status.attribute("aspd_fixed").stages, { base, contrib(3, 5), contrib(1, 9), contrib(2, 7) }) == 9)
	local rate_base = { source = "base", tag = "base|0||", value = 1000 }
	local function rc(rank, value) return { source = "p" .. rank, tag = "potion|8497|potion|" .. rank, value = value } end
	assert(model.fold(status.attribute("aspd_rate").stages, { rate_base, rc(3, 50), rc(1, 150), rc(2, 100) }) == 850)
end)

return true
