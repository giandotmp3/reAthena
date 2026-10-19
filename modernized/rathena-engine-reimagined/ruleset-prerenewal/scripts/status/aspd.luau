--!nonstrict
-- status_calc_aspd_rate (status.cpp:8427-8559), PRE (not RENEWAL_ASPD). `max` is the best of the quicken family: Star Comfort assigns it
-- first, the others raise it (gset / gmax in group "quicken"), then `aspd_rate -= max` and the penalties and potions, all in source order
-- (everything at > 8490 is an ordinary contribution). Berserk -300 and Madness Cancel -200 are an else-if pair; the potions are an
-- else-if chain Potion3 > 2 > 1 > 0 (the guards are in the definitions).
local aggregate = import("scripts/status/aggregate")
local attrs = import("scripts/status/attrs")
local ops = import("scripts/status/ops")

local M = {}

function M.calc(e, base)
	local contribs = aggregate.emit("aspd_rate", e)
	local g = aggregate.groups(contribs)
	local acc = base - (g.quicken or 0) -- 8490
	for _, c in ipairs(contribs) do
		if c.op == "add" or c.op == "mul_pct" or c.op == "mul_frac" or c.op == "scale" or c.op == "set" then
			acc = aggregate.OPS[c.op](e, acc, c.v, c.v2)
		end
	end
	local cl = attrs.aspd_rate.clamp
	return ops.cap(acc, cl[1], cl[2]) -- 8559
end

return M
