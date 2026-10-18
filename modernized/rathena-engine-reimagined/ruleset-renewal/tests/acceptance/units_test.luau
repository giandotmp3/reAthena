--!nonstrict
-- Unit-level checks of the renewal formulas (BR-RULESET-010, 025, 034) and of the float32 helper. The numbers that are not
-- restated from the legacy golden data come from float32 identities that are exact by construction.
local T = import("tests/support/harness")
local fp = import("scripts/damage/fp")
local formulas = import("scripts/formulas")

T.run("BR-RULESET-034", "fp.f32 rounds to binary32 (ties to even) like (float)", false, function()
	assert(fp.f32(1.4) == 1.39999997615814208984375)
	assert(fp.f32(0.01) == 0.00999999977648258209228515625)
	assert(fp.f32(16777217) == 16777216 and fp.f32(16777219) == 16777220) -- ties to even above 2^24
	assert(fp.f32(-16777217) == -16777216 and fp.f32(3) == 3 and fp.f32(0) == 0)
	assert(fp.f32(0.1) == 0.100000001490116119384765625)
end)

T.run("BR-RULESET-034", "crit damage: player 1.4f + 0.01f*crate in float32, others 1.4f, floored", false, function()
	assert(formulas.crit_damage(1000, 0, false) == 1400)
	assert(formulas.crit_damage(1000, 0, true) == 1400)
	assert(formulas.crit_damage(1000, 37, true) == 1770)
	assert(formulas.crit_damage(373, 0, false) == 522)
end)

T.run("BR-RULESET-025", "RES: res/(res+400)*80% rounded down; ignore percent capped at 50", false, function()
	assert(formulas.res_reduction(1000, 100, 0) == 840)
	assert(formulas.res_reduction(1000, 100, 50) == 912)
	assert(formulas.res_reduction(1000, 100, 80) == 912, "ignore RES is capped at max_res_mres_ignored")
	assert(formulas.res_reduction(1000, 400, 0) == 600)
end)

T.run("BR-RULESET-010", "weapon ATK range: variance 5% per weapon level plus ATK*STAT/200", false, function()
	local lo, hi = formulas.weapon_atk_range(100, 2, 40)
	assert(lo == 110 and hi == 130)
	lo, hi = formulas.weapon_atk_range(100, 4, 80)
	assert(lo == 120 and hi == 160)
end)
