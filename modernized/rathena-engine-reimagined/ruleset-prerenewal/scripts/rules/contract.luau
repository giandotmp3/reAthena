--!strict-compatible
-- Rule functions for group 'contract'. Each rNNN takes the case `given` table and returns
-- the outcome table. Unimplemented rules raise NOT_IMPLEMENTED (expected-failure
-- acceptance tests, tagged with the rule ID, track them).
local notimpl = import("scripts/notimpl")
local M = {}

-- BR-RULESET-111 (B, P0): ruleset publishes the tables the native default stage needs
function M.r111(input)
	return notimpl("BR-RULESET-111")
end

-- BR-RULESET-112 (B, P0): item scripts return a cacheable compiled list of bonus deltas
function M.r112(input)
	return notimpl("BR-RULESET-112")
end

-- BR-RULESET-116 (B, P0): calculation returns an ordered effect queue and never mutates the world
function M.r116(input)
	return notimpl("BR-RULESET-116")
end

-- BR-RULESET-117 (B, P0): hook surface: skill, damage-stage, status, item and formula hooks
function M.r117(input)
	return notimpl("BR-RULESET-117")
end

-- BR-RULESET-118: implemented.
function M.r118(input)
	return import("scripts/manifest").manifest()
end

-- BR-RULESET-119: implemented.
function M.r119(input)
	return import("conformance/index").coverage()
end

return M
