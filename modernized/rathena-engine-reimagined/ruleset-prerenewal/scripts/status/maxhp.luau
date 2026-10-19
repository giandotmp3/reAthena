--!nonstrict
-- status_calc_maxhp / status_calc_maxsp for units that are not players (status.cpp:8733-8762) with the status part of
-- status_get_hpbonus / status_get_spbonus (3169-3260, 3329-3420): FIX is added, RATE is `100 + sum` (the sum clamped to >= -100),
-- maxhp = maxhp * rate / 100 when rate ~= 100, then cap_value(maxhp, 1, UINT_MAX).
-- Legacy quirk (status.cpp:8737): maxhp is uint64_t and the flat bonus is int32; a negative flat sum larger than maxhp (Marionette -1000
-- on a unit with max HP < 1000) wraps to ~2^64 and the final cap_value returns UINT_MAX (4294967295), not 1.
local aggregate = import("scripts/status/aggregate")
local ops = import("scripts/status/ops")

local M = {}
local UINT_MAX = 4294967295
local INT_MAX = 2147483647

local function bonus(e, attr)
	local acc = 0
	for _, c in ipairs(aggregate.emit(attr, e)) do acc = aggregate.OPS[c.op](e, acc, c.v, c.v2) end
	return acc
end

-- status_get_hpbonus / status_get_spbonus(bl, type): which = "hp" | "sp", kind = "fix" | "rate". The RATE sum is clamped to >= -100
-- (status.cpp:3290), both end in min(bonus, INT_MAX) (3295).
function M.bonus(e, which, kind)
	local b = bonus(e, which .. "_" .. kind)
	if kind == "rate" and b < -100 then b = -100 end
	if b > INT_MAX then b = INT_MAX end
	return b
end

-- which = "hp" | "sp"
function M.calc(e, which, base)
	local fix = M.bonus(e, which, "fix")
	local rate = 100 + M.bonus(e, which, "rate")
	local v = base + fix
	if v < 0 then -- uint64 wrap-around
		if rate == 0 then return 1 end -- (2^64 - n) * 0 = 0, cap_value -> 1
		return UINT_MAX
	end
	if rate ~= 100 then v = e.ctx.mul_div(v, rate, 100) end
	return ops.cap(v, 1, UINT_MAX)
end

return M
