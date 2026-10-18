--!strict-compatible
-- Acceptance tests for group 'skill', one per BR-RULESET rule, tagged with the rule ID.
-- Rules listed in conformance/pending.luau are expected-failure (strict: XPASS fails the run).
local T = import("tests/support/harness")
local pending = import("conformance/pending")
local cases = import("conformance/cases/skill")
local rules = import("scripts/rules/skill")

for _, case in ipairs(cases) do
	T.case(case, rules, pending)
end
