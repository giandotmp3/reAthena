--!nonstrict
-- BR-RULESET-077: the guards around a recalculation. Source-derived (the player recalculation needs a whole player, so there is no
-- golden for it); the partial recalculation by calc flags is the golden-checked scripts/status/chain.luau.
--
--  * status_calc_pc_sub (status.cpp:3775-3782): `static int32 calculating; if (++calculating > 10) return -1;` -- a recalculation that
--    re-enters more than 10 levels deep (an item script that retriggers it) is aborted. The counter is reset with `calculating = 0` at the
--    normal exit (status.cpp:5012), NOT decremented: when an inner call finishes, the outer ones are no longer counted (legacy defect,
--    kept while legacy_quirks is true; false decrements, so the limit really is 10 nested levels).
--  * status_calc_bl_ (status.cpp:6546-6556): while a player has delayed damage pending the recalculation is held (and replayed later)
--    unless it is forced (SCO_FORCE).
local M = {}

function M.new(legacy_quirks)
	return { depth = 0, legacy_quirks = legacy_quirks ~= false }
end

-- returns true when the calculation may proceed, false when it is aborted
function M.enter(g)
	g.depth = g.depth + 1
	return g.depth <= 10
end

function M.leave(g)
	if g.legacy_quirks then g.depth = 0 else g.depth = g.depth - 1 end
end

-- status_calc_bl_: true when the recalculation of a player must be held back
function M.hold(delayed_damage, forced)
	return delayed_damage ~= 0 and not forced
end

return M
