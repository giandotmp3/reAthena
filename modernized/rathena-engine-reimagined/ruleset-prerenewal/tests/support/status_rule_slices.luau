--!nonstrict
-- Per-rule slices of the STATUS golden data (conformance/golden/status_pre.jsonl.gz, recorded from the real legacy code). A converted
-- status rule case (conformance/cases/status.luau) is not a hand-written expectation: its `golden` field names one recorded legacy
-- case (by `tag`) or status database record ("db:NAME"), and `given` / `expected` are cut from that record by the functions below
-- (inputs from the case, expected values from the legacy output). The acceptance test re-derives both and fails when the case file
-- drifts from the data. tests/print_status_rule_cases.luau prints the literals; splice_status_cases.py writes them into the case file.
local M = {}

local function find_case(g, tag)
	for _, c in ipairs(g.cases) do
		if c.tag == tag then return c end
	end
	error("status golden: no case tagged " .. tostring(tag))
end

local function sorted(list)
	local o = {}
	for i, v in ipairs(list or {}) do o[i] = v end
	table.sort(o)
	return o
end

local function set_names(cant)
	local t = {}
	for k, v in pairs(cant) do
		if v == 1 then t[#t + 1] = k end
	end
	table.sort(t)
	return t
end

local function attr_slice(tag)
	return {
		find = function(g) return find_case(g, tag) end,
		given = function(g, c) return { attr = c.fn, base = c.base, unit_type = c.ut, statuses = c.sc } end,
		expected = function(g, c) return { value = c.out } end,
	}
end

M["BR-RULESET-051"] = {
	golden = "db:STONE",
	find = function(g, golden) return g.db[golden:sub(4)] end,
	given = function(g, r) return { status = r.name } end,
	expected = function(g, r)
		return { valid = true, legacy_sc = g.consts.sc[r.name], states = sorted(r.state), calc_flags = sorted(r.calc), flags = sorted(r.flag),
			fail = r.fail, end_on_start = r.end_on_start, end_return = r.end_return, end_on_end = r.end_on_end, min_rate = r.min_rate,
			min_duration = r.min_duration, has_script = r.script == 1 }
	end,
}
M["BR-RULESET-065"] = attr_slice("R065-harmonize")
M["BR-RULESET-066"] = attr_slice("R066-floor")
M["BR-RULESET-067"] = {
	find = function(g) return find_case(g, "E-4b") end,
	given = function(g, c) return { base_speed = c.base, unit_type = c.ut, statuses = c.sc } end,
	expected = function(g, c) return { speed = c.out } end,
}
M["BR-RULESET-068"] = {
	find = function(g) return find_case(g, "R068-quicken") end,
	given = function(g, c) return { base_aspd_rate = c.base, unit_type = c.ut, statuses = c.sc } end,
	expected = function(g, c) return { aspd_rate = c.out } end,
}
M["BR-RULESET-069"] = attr_slice("R069-hit")
M["BR-RULESET-071"] = attr_slice("R071-element")
M["BR-RULESET-073"] = {
	find = function(g) return find_case(g, "R073-stun") end,
	given = function(g, c) return { changed = c.changed, start = true, unit_type = c.ut, statuses = c.sc } end,
	expected = function(g, c) return { states = set_names(c.out) } end,
}
M["BR-RULESET-075"] = {
	find = function(g) return find_case(g, "R075-speedup0") end,
	given = function(g, c) return { statuses = c.sc } end,
	expected = function(g, c) return { bonuses = c.out } end,
}
-- 077: source-derived (status_calc_pc_sub needs a whole player): no golden record, the case names the source line
M["BR-RULESET-077"] = { source = "status.cpp:3781" }

M.golden_tags = {
	["BR-RULESET-065"] = "R065-harmonize", ["BR-RULESET-066"] = "R066-floor", ["BR-RULESET-067"] = "E-4b", ["BR-RULESET-068"] = "R068-quicken",
	["BR-RULESET-069"] = "R069-hit", ["BR-RULESET-071"] = "R071-element", ["BR-RULESET-073"] = "R073-stun", ["BR-RULESET-075"] = "R075-speedup0",
}
return M
