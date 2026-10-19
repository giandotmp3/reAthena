--!nonstrict
-- The ruleset-supplied aggregation configuration of every attribute (the "stage list" of the engine attrs module): the clamp the legacy
-- function ends with (cap_value bounds, status.cpp), the width and signedness of the legacy return type (early `return X;` statements
-- are narrowed to it without any clamp), and the stages. Standard stages, in order:
--   pre       contributions marked stage = "pre"   (map penalties applied before the status checks: flee in GvG / battlegrounds)
--   override  terminal contributions (`return 0;`, `return 90;`, `str -= val2; return clamp`) and the adds that belong to them; the
--             first active terminal contribution, in legacy order, ends the calculation
--   ordered   everything else, one after the other in legacy order (the `at` line of status.cpp)
--   clamp     cap_value(acc, lo, hi)
-- (The legacy functions are straight-line code: acc = acc + ... interleaves adds and percentages in source order, so "ordered" is the
-- faithful form of the default `override > add > mul_pct > clamp` chain; where a function happens to put all its adds before all its
-- percentages the two coincide.) Attributes with `custom` have their own stage list (speed, aspd_rate, mode, ...).
local K = import("scripts/status/consts")

local function std(lo, hi, bits, signed, fn)
	return { clamp = lo and { lo, hi } or nil, ret = { bits, signed }, fn = fn, stages = { "pre", "override", "ordered", "clamp" } }
end

local U16 = K.USHRT_MAX
local S16 = K.SHRT_MAX
return {
	str = std(0, U16, 16, false, "status_calc_str"), agi = std(0, U16, 16, false, "status_calc_agi"),
	vit = std(0, U16, 16, false, "status_calc_vit"), int = std(0, U16, 16, false, "status_calc_int"),
	dex = std(0, U16, 16, false, "status_calc_dex"), luk = std(0, U16, 16, false, "status_calc_luk"),
	pow = std(0, U16, 16, false, "status_calc_pow"), sta = std(0, U16, 16, false, "status_calc_sta"),
	wis = std(0, U16, 16, false, "status_calc_wis"), spl = std(0, U16, 16, false, "status_calc_spl"),
	con = std(0, U16, 16, false, "status_calc_con"), crt = std(0, U16, 16, false, "status_calc_crt"),
	batk = std(nil, nil, 32, true, "status_calc_batk"),
	watk = std(0, U16, 16, false, "status_calc_watk"),
	hit = std(1, S16, 16, true, "status_calc_hit"), cri = std(10, S16, 16, true, "status_calc_critical"),
	flee = std(1, S16, 16, true, "status_calc_flee"), flee2 = std(10, S16, 16, true, "status_calc_flee2"),
	def = std(K.DEFTYPE_MIN, K.DEFTYPE_MAX, 8, true, "status_calc_def"), def2 = std(1, S16, 16, true, "status_calc_def2"),
	mdef = std(K.DEFTYPE_MIN, K.DEFTYPE_MAX, 8, true, "status_calc_mdef"), mdef2 = std(1, S16, 16, true, "status_calc_mdef2"),
	fix_aspd = std(1, K.MIN_ASPD, 16, true, "status_calc_fix_aspd"),
	patk = std(0, S16, 16, true, "status_calc_patk"), smatk = std(0, S16, 16, true, "status_calc_smatk"),
	res = std(0, S16, 16, true, "status_calc_res"), mres = std(0, S16, 16, true, "status_calc_mres"),
	hplus = std(0, S16, 16, true, "status_calc_hplus"), crate = std(0, S16, 16, true, "status_calc_crate"),
	element = std(0, K.UCHAR_MAX, 8, false, "status_calc_element"),
	element_lv = std(1, 4, 8, false, "status_calc_element_lv"),
	atk_element = std(0, K.UCHAR_MAX, 8, false, "status_calc_attack_element"),
	matk = std(0, U16, 16, false, "status_calc_pseudobuff_matk"), -- the status / skill flat MATK of PRE (the part status_calc_bl_main adds to the base MATK)
	-- custom stage lists (their own modules)
	aspd_rate = { clamp = { 0, S16 }, ret = { 16, true }, custom = "scripts/status/aspd", fn = "status_calc_aspd_rate" },
	speed = { clamp = { K.MIN_WALK_SPEED, K.MAX_WALK_SPEED }, ret = { 16, false }, custom = "scripts/status/speed", fn = "status_calc_speed" },
	mode = { clamp = { 0, K.INT_MAX }, ret = { 32, true }, custom = "scripts/status/mode", fn = "status_calc_mode" },
}
