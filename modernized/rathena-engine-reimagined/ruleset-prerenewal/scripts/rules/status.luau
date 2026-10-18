--!strict-compatible
-- Rule functions for group 'status'. Each rNNN takes the case `given` table and returns
-- the outcome table. Unimplemented rules raise NOT_IMPLEMENTED (expected-failure
-- acceptance tests, tagged with the rule ID, track them).
local notimpl = import("scripts/notimpl")
local M = {}

-- BR-RULESET-051 (B, P0): status record schema carries id, icon, states, flags, fail/end lists
function M.r051(input)
	return notimpl("BR-RULESET-051")
end

-- BR-RULESET-052 (B, P0): start gating: dead targets refused
function M.r052(input)
	return notimpl("BR-RULESET-052")
end

-- BR-RULESET-053 (PRE, P0): PRE resistance: sc_def from VIT, sc_def2 from LUK (Stun)
function M.r053(input)
	return notimpl("BR-RULESET-053")
end

-- BR-RULESET-055 (B, P0): resistance tail: rate rounded up to a multiple of 10
function M.r055(input)
	return notimpl("BR-RULESET-055")
end

-- BR-RULESET-056 (B, P1): immunity edge cases (Golden Thief Bug blocks magic statuses)
function M.r056(input)
	return notimpl("BR-RULESET-056")
end

-- BR-RULESET-057 (B, P1): delayed application: PRE keeps the full duration
function M.r057(input)
	return notimpl("BR-RULESET-057")
end

-- BR-RULESET-058 (B, P1): undead race is immune to Freeze
function M.r058(input)
	return notimpl("BR-RULESET-058")
end

-- BR-RULESET-059 (B, P1): post-delay prerequisites per type (Kyrie not on mobs)
function M.r059(input)
	return notimpl("BR-RULESET-059")
end

-- BR-RULESET-060 (B, P0): Blessing on a cursed player ends Curse, is not applied, returns true
function M.r060(input)
	return notimpl("BR-RULESET-060")
end

-- BR-RULESET-061 (B, P0): overlap: lower or equal incoming val1 is refused but returns true
function M.r061(input)
	return notimpl("BR-RULESET-061")
end

-- BR-RULESET-062 (B, P2): food: normal food refused if cash food has higher val1
function M.r062(input)
	return notimpl("BR-RULESET-062")
end

-- BR-RULESET-063 (B, P1): strip statuses fail if nothing is equipped
function M.r063(input)
	return notimpl("BR-RULESET-063")
end

-- BR-RULESET-064 (B, P0): ending a status with a stale timer id is ignored
function M.r064(input)
	return notimpl("BR-RULESET-064")
end

-- BR-RULESET-065 (B, P0): stat contributions clamp to 0..65535
function M.r065(input)
	return notimpl("BR-RULESET-065")
end

-- BR-RULESET-066 (B, P0): PRE clamps def2 to at least 1
function M.r066(input)
	return notimpl("BR-RULESET-066")
end

-- BR-RULESET-067 (B, P0): speed model: 100 - haste + slow, base speed 150
function M.r067(input)
	return notimpl("BR-RULESET-067")
end

-- BR-RULESET-068 (B, P0): PRE ASPD: best of the quicken family
function M.r068(input)
	return notimpl("BR-RULESET-068")
end

-- BR-RULESET-069 (B, P0): same contribution pattern for BATK, HIT, FLEE, CRI, MDEF, MAXHP ...
function M.r069(input)
	return notimpl("BR-RULESET-069")
end

-- BR-RULESET-070 (B, P0): cast/delay/SP-cost contributions (PRE cast variants)
function M.r070(input)
	return notimpl("BR-RULESET-070")
end

-- BR-RULESET-071 (B, P1): statuses overriding element, defense element, size, race, look
function M.r071(input)
	return notimpl("BR-RULESET-071")
end

-- BR-RULESET-072 (B, P0): poison tick for players: 2 + maxHP*3/200, stops at 25% HP
function M.r072(input)
	return notimpl("BR-RULESET-072")
end

-- BR-RULESET-073 (B, P0): States (NoMove, NoCast, NoAttack) computed from the active set
function M.r073(input)
	return notimpl("BR-RULESET-073")
end

-- BR-RULESET-074 (B, P0): status flags with behavior (NoDispell, RemoveOnDamaged, ...)
function M.r074(input)
	return notimpl("BR-RULESET-074")
end

-- BR-RULESET-075 (B, P0): scripted statuses contribute via script (Speedup0 bSpeedRate)
function M.r075(input)
	return notimpl("BR-RULESET-075")
end

-- BR-RULESET-076 (B, P2): Soul Link is class-gated
function M.r076(input)
	return notimpl("BR-RULESET-076")
end

-- BR-RULESET-077 (B, P0): recalculation re-entered more than 10 levels aborts
function M.r077(input)
	return notimpl("BR-RULESET-077")
end

return M
