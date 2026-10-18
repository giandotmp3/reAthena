--!strict-compatible
-- Rule functions for group 'skill'. Each rNNN takes the case `given` table and returns
-- the outcome table. Unimplemented rules raise NOT_IMPLEMENTED (expected-failure
-- acceptance tests, tagged with the rule ID, track them).
local notimpl = import("scripts/notimpl")
local M = {}

-- BR-RULESET-078 (B, P0): skill_db field semantics: per-level arrays, requires, damage flags
function M.r078(input)
	return notimpl("BR-RULESET-078")
end

-- BR-RULESET-079 (B, P0): pre-renewal skills come from db/pre-re, never db/re
function M.r079(input)
	return notimpl("BR-RULESET-079")
end

-- BR-RULESET-080 (B, P0): dispatch: single-target attack skill goes to castend_damage_id
function M.r080(input)
	return notimpl("BR-RULESET-080")
end

-- BR-RULESET-081 (B, P0): cast-end lifecycle runs checks in the documented order
function M.r081(input)
	return notimpl("BR-RULESET-081")
end

-- BR-RULESET-082 (B, P0): pre-cast check (castbegin) requirements
function M.r082(input)
	return notimpl("BR-RULESET-082")
end

-- BR-RULESET-083 (B, P0): requirement computation: positive rate applies to current SP
function M.r083(input)
	return notimpl("BR-RULESET-083")
end

-- BR-RULESET-084 (B, P1): consume-on-fail: out of range with skill_out_range_consume consumes
function M.r084(input)
	return notimpl("BR-RULESET-084")
end

-- BR-RULESET-085 (B, P0): canact_tick = max(tick+delay, canact_tick - SECURITY_CASTTIME)
function M.r085(input)
	return notimpl("BR-RULESET-085")
end

-- BR-RULESET-086 (B, P1): after-cast walk delay for skills without move-enabled state
function M.r086(input)
	return notimpl("BR-RULESET-086")
end

-- BR-RULESET-087 (B, P0): skill_attack pre-gates (skill-immune, Emperium, Trick Dead)
function M.r087(input)
	return notimpl("BR-RULESET-087")
end

-- BR-RULESET-088 (B, P1): magic reflection swaps caster and target
function M.r088(input)
	return notimpl("BR-RULESET-088")
end

-- BR-RULESET-089 (B, P1): knockback zeroed when damage is less than div
function M.r089(input)
	return notimpl("BR-RULESET-089")
end

-- BR-RULESET-090 (B, P0): additional effects only if damage level is at least BLOCK
function M.r090(input)
	return notimpl("BR-RULESET-090")
end

-- BR-RULESET-091 (B, P0): splash area and target filtering
function M.r091(input)
	return notimpl("BR-RULESET-091")
end

-- BR-RULESET-092 (B, P0): ground unit lifecycle (skill_unitsetting)
function M.r092(input)
	return notimpl("BR-RULESET-092")
end

-- BR-RULESET-093 (B, P2): unit-specific rules (Warp Portal morph after 2 s)
function M.r093(input)
	return notimpl("BR-RULESET-093")
end

-- BR-RULESET-094 (B, P1): combo window = 2000 - 4*AGI - 2*DEX ms
function M.r094(input)
	return notimpl("BR-RULESET-094")
end

-- BR-RULESET-095 (B, P1): combo skill delay = 1000 - 4*AGI - 2*DEX
function M.r095(input)
	return notimpl("BR-RULESET-095")
end

-- BR-RULESET-096 (B, P0): skill hook set contains the nine SkillImpl virtuals
function M.r096(input)
	return notimpl("BR-RULESET-096")
end

-- BR-RULESET-097 (B, P1): skill-items skip requirements and delay
function M.r097(input)
	return notimpl("BR-RULESET-097")
end

return M
