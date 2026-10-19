--!nonstrict
-- Acceptance tests for group 'status', one per BR-RULESET rule, tagged with the rule ID.
-- Rules listed in conformance/pending.luau are expected-failure (strict: XPASS fails the run).
-- The converted rules (051 065 066 067 068 069 071 073 075 077) run on the status layer (scripts/status) with the injected int64 ctx; their
-- cases are cut from the status golden data, which tests/acceptance/status_golden_test.luau (extra suite) re-derives and replays in full.
local T = import("tests/support/harness")
local pending = import("conformance/pending")
local cases = import("conformance/cases/status")
local rules = import("scripts/rules/status")
local fake_ctx = import("tests/support/fake_ctx")

local env = { make = function() return fake_ctx.new({}) end }

for _, case in ipairs(cases) do
	T.case(case, rules, pending, env)
end
