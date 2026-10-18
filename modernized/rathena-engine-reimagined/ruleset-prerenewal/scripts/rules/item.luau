--!strict-compatible
-- Rule functions for group 'item'. Each rNNN takes the case `given` table and returns
-- the outcome table. Unimplemented rules raise NOT_IMPLEMENTED (expected-failure
-- acceptance tests, tagged with the rule ID, track them).
local notimpl = import("scripts/notimpl")
local M = {}

-- BR-RULESET-098 (B, P0): bonus argument count selects the pc_bonus variant
function M.r098(input)
	return notimpl("BR-RULESET-098")
end

-- BR-RULESET-099 (B, P0): most stat bonuses are ignored when the hand flag is arrow
function M.r099(input)
	return notimpl("BR-RULESET-099")
end

-- BR-RULESET-100 (B, P0): PRE DEF clamps to a signed byte
function M.r100(input)
	return notimpl("BR-RULESET-100")
end

-- BR-RULESET-101 (B, P1): add-effect defaults: target, weapon, short + long range
function M.r101(input)
	return notimpl("BR-RULESET-101")
end

-- BR-RULESET-102 (B, P0): PRE bBaseAtk maps to batk
function M.r102(input)
	return notimpl("BR-RULESET-102")
end

-- BR-RULESET-103 (B, P0): equipment parse order in status_calc_pc_sub
function M.r103(input)
	return notimpl("BR-RULESET-103")
end

-- BR-RULESET-104 (B, P0): forged star crumbs: 3 stars add +40
function M.r104(input)
	return notimpl("BR-RULESET-104")
end

-- BR-RULESET-105 (B, P1): item combos run only when every item is equipped
function M.r105(input)
	return notimpl("BR-RULESET-105")
end

-- BR-RULESET-106 (B, P0): equip scripts run on equip unless restricted on the map
function M.r106(input)
	return notimpl("BR-RULESET-106")
end

-- BR-RULESET-107 (B, P0): consuming an item: refused while casting
function M.r107(input)
	return notimpl("BR-RULESET-107")
end

-- BR-RULESET-108 (B, P0): item heal bonus = 100 + 2*VIT + 10*Recovery
function M.r108(input)
	return notimpl("BR-RULESET-108")
end

-- BR-RULESET-109 (B, P1): timed bonus_script and autobonus
function M.r109(input)
	return notimpl("BR-RULESET-109")
end

-- BR-RULESET-110 (B, P0): item_db fields validate against the PRE item schema
function M.r110(input)
	return notimpl("BR-RULESET-110")
end

return M
