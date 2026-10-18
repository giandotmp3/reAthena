--!nonstrict
-- Acceptance tests for group 'damage', one per BR-RULESET rule, tagged with the rule ID.
-- Rules listed in conformance/pending.luau are expected-failure (strict: XPASS fails the run).
local T = import("tests/support/harness")
local pending = import("conformance/pending")
local cases = import("conformance/cases/damage")
local rules = import("scripts/rules/damage")

local fake_ctx = import("tests/support/fake_ctx")
local scripted = import("tests/support/scripted_rng").scripted
local env = {
	make = function(draws)
		local rng = scripted(draws)
		return fake_ctx.new(rng), rng
	end,
}

for _, case in ipairs(cases) do
	T.case(case, rules, pending, env)
end
