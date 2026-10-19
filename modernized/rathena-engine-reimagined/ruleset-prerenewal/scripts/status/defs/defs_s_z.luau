--!nonstrict
-- Status definitions, draft generated from the PRE branch of src/map/status.cpp by conformance/golden_gen/st_gen_defs.py,
-- then reviewed and corrected by hand against the golden data (this file is the maintained source).
-- Every contribution is tagged `at = <status.cpp line>`: the citation and the legacy evaluation order inside its attribute.
-- Field names answer Q-F (docs/STATUS_FIELDS.md). `s` = this status' named fields, `e` = environment (e.sc[NAME] other statuses,
-- e.unit, e.idiv ...), `K` = legacy constants (scripts/status/consts.luau), `O` = bit helpers.
local status = import("scripts/status/define")
local K = import("scripts/status/consts")
local O = import("scripts/status/ops")
local D = status.define

D("SANDY_FESTIVAL", {
	in_pre_db = false, -- not in db/pre-re/status.yml: the stat code knows it, a PRE server never starts it
	fields = { val1 = "lv", val2 = "sta_wis_spl_inc", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		sta = {
			{ at = 7224, op = "add", v = function(s, e) return s.sta_wis_spl_inc end },
		},
		wis = {
			{ at = 7244, op = "add", v = function(s, e) return s.sta_wis_spl_inc end },
		},
		spl = {
			{ at = 7264, op = "add", v = function(s, e) return s.sta_wis_spl_inc end },
		},
	},
})

D("SATURDAYNIGHTFEVER", {
	fields = { val1 = "lv", val2 = "mark_not_lose", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		hit = {
			{ at = 7617, op = "add", v = function(s, e) return -(50 + (50 * s.lv)) end },
		},
		flee = {
			{ at = 7715, op = "add", v = function(s, e) return -(20 + (30 * s.lv)) end },
		},
	},
})

D("SERVICE4U", {
	fields = { val1 = "val1_unused", val2 = "maxsp_bonus_inc", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		sp_rate = {
			{ at = 3405, op = "add", v = function(s, e) return s.maxsp_bonus_inc end },
		},
	},
})

D("SHADOWWEAPON", {
	fields = { val1 = "val1_unused", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		atk_element = {
			{ at = 8880, op = "final", v = function(s, e) return K.ELE_DARK end },
		},
	},
})

D("SHADOW_CLOCK", {
	in_pre_db = false, -- not in db/pre-re/status.yml: the stat code knows it, a PRE server never starts it
	fields = { val1 = "val1_unused", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		speed = {
			{ at = 8216, op = "gmax", group = "haste", v = 50 },
		},
	},
})

D("SHADOW_STRIP", {
	in_pre_db = false, -- not in db/pre-re/status.yml: the stat code knows it, a PRE server never starts it
	fields = { val1 = "val1_unused", val2 = "res_mres_pct_dec", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		res = {
			{ at = 8659, op = "mul_pct", when = function(s, e) return e.unit.type ~= "pc" end, v = function(s, e) return -s.res_mres_pct_dec end },
		},
		mres = {
			{ at = 8683, op = "mul_pct", when = function(s, e) return e.unit.type ~= "pc" end, v = function(s, e) return -s.res_mres_pct_dec end },
		},
	},
})

D("SHAPESHIFT", {
	fields = { val1 = "val1_unused", val2 = "element", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		element = {
			{ at = 8812, op = "final", v = function(s, e) return s.element end },
		},
		element_lv = {
			{ at = 8840, op = "final", v = 1 },
		},
	},
})

D("SHIELDCHAINRUSH", {
	in_pre_db = false, -- not in db/pre-re/status.yml: the stat code knows it, a PRE server never starts it
	fields = { val1 = "val1_unused", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		speed = {
			{ at = 8154, op = "gmax", group = "slow", when = function(s, e) return not ((e.unit.type == "pc" and e.sc.HIDING ~= nil) and e.skill("RG_TUNNELDRIVE") > 0) and not ((e.unit.type == "pc" and e.sc.CHASEWALK ~= nil) and e.sc.CHASEWALK.speed_value < 0) end, v = 20 },
		},
	},
})

D("SHIELDSPELL_ATK", {
	fields = { val1 = "val1_unused", val2 = "atk_inc", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		watk = {
			{ at = 7395, op = "add", v = function(s, e) return s.atk_inc end },
		},
	},
})

D("SIGNUMCRUCIS", {
	fields = { val1 = "lv", val2 = "def_pct_dec", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		def = {
			{ at = 7820, op = "mul_pct", v = function(s, e) return -s.def_pct_dec end },
		},
	},
})

D("SINCERE_FAITH", {
	in_pre_db = false, -- not in db/pre-re/status.yml: the stat code knows it, a PRE server never starts it
	fields = { val1 = "lv", val2 = "fix_aspd_dec_arg", val3 = "perfect_hit_inc", val4 = "val4_unused" },
	contributes = {
		fix_aspd = {
			{ at = 8413, op = "add", v = function(s, e) return -(10 * s.fix_aspd_dec_arg) end },
		},
	},
})

D("SKA", {
	fields = { val1 = "val1_unused", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		mdef2 = {
			{ at = 8014, op = "final", v = 90 },
		},
	},
})

D("SKE", {
	fields = { val1 = "val1_unused", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		def = {
			{ at = 7824, op = "div", v = 2 },
		},
		def2 = {
			{ at = 7911, op = "mul_pct", v = -50 },
		},
	},
})

D("SLOWDOWN", {
	fields = { val1 = "speed_slow", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		speed = {
			{ at = 8120, op = "gmax", group = "slow", when = function(s, e) return not ((e.unit.type == "pc" and e.sc.HIDING ~= nil) and e.skill("RG_TUNNELDRIVE") > 0) and not ((e.unit.type == "pc" and e.sc.CHASEWALK ~= nil) and e.sc.CHASEWALK.speed_value < 0) end, v = function(s, e) return s.speed_slow end },
		},
	},
})

D("SMOKEPOWDER", {
	fields = { val1 = "val1_unused", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		flee = {
			{ at = 7727, op = "mul_pct", v = 20 },
		},
	},
})

D("SOLID_SKIN_OPTION", {
	fields = { val1 = "val1_unused", val2 = "def_pct_inc", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		hp_fix = {
			{ at = 3206, op = "add", v = 2000 },
		},
		def = {
			{ at = 7846, op = "mul_pct", v = function(s, e) return s.def_pct_inc end },
		},
	},
})

D("SOULCOLD", {
	fields = { val1 = "stat_inc", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		agi = {
			{ at = 6862, op = "add", v = function(s, e) return s.stat_inc end },
		},
	},
})

D("SOULFAIRY", {
	fields = { val1 = "lv", val2 = "matk_inc", val3 = "varcast_dec", val4 = "val4_unused" },
})

D("SOULFALCON", {
	fields = { val1 = "lv", val2 = "watk_inc", val3 = "hit_inc", val4 = "val4_unused" },
	contributes = {
		watk = {
			{ at = 7436, op = "add", v = function(s, e) return s.watk_inc end },
		},
		hit = {
			{ at = 7615, op = "add", v = function(s, e) return s.hit_inc end },
		},
	},
})

D("SOULGOLEM", {
	fields = { val1 = "lv", val2 = "def_inc", val3 = "mdef_inc", val4 = "val4_unused" },
	contributes = {
		def = {
			{ at = 7856, op = "add", v = function(s, e) return s.def_inc end },
		},
		mdef = {
			{ at = 7986, op = "add", v = function(s, e) return s.mdef_inc end },
		},
	},
})

D("SOULSHADOW", {
	fields = { val1 = "lv", val2 = "fix_aspd_dec_arg", val3 = "cri_inc_arg", val4 = "val4_unused" },
	contributes = {
		cri = {
			{ at = 7549, op = "add", v = function(s, e) return 10 * s.cri_inc_arg end },
		},
		fix_aspd = {
			{ at = 8409, op = "add", v = function(s, e) return -(10 * s.fix_aspd_dec_arg) end },
		},
	},
})

D("SPEARQUICKEN", {
	fields = { val1 = "lv", val2 = "aspd_rate_quicken", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		aspd_rate = {
			{ at = 8460, op = "gmax", group = "quicken", v = function(s, e) return s.aspd_rate_quicken end },
		},
	},
})

D("SPEED", {
	fields = { val1 = "lv", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		flee = {
			{ at = 7669, op = "add", v = function(s, e) return 10 + (s.lv * 10) end },
		},
	},
})

D("SPELL_ENCHANTING", {
	in_pre_db = false, -- not in db/pre-re/status.yml: the stat code knows it, a PRE server never starts it
	fields = { val1 = "lv", val2 = "smatk_inc", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		smatk = {
			{ at = 8620, op = "add", v = function(s, e) return s.smatk_inc end },
		},
	},
})

D("SPIDERWEB", {
	fields = { val1 = "val1_unused", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
})

D("SPIRIT", {
	fields = { val1 = "lv", val2 = "link_type", val3 = "str_agi_vit_packed", val4 = "int_dex_luk_packed" },
	contributes = {
		str = {
			{ at = 6809, op = "add", when = function(s, e) return s.link_type == K.SL_HIGH end, v = function(s, e) return O.band((O.sar((s.str_agi_vit_packed), 16)), 255) end },
		},
		agi = {
			{ at = 6880, op = "add", when = function(s, e) return s.link_type == K.SL_HIGH end, v = function(s, e) return O.band((O.sar((s.str_agi_vit_packed), 8)), 255) end },
		},
		vit = {
			{ at = 6943, op = "add", when = function(s, e) return s.link_type == K.SL_HIGH end, v = function(s, e) return O.band(s.str_agi_vit_packed, 255) end },
		},
		int = {
			{ at = 7012, op = "add", when = function(s, e) return s.link_type == K.SL_HIGH end, v = function(s, e) return O.band((O.sar((s.int_dex_luk_packed), 16)), 255) end },
		},
		dex = {
			{ at = 7094, op = "add", when = function(s, e) return s.link_type == K.SL_HIGH end, v = function(s, e) return O.band((O.sar((s.int_dex_luk_packed), 8)), 255) end },
		},
		luk = {
			{ at = 7157, op = "add", when = function(s, e) return s.link_type == K.SL_HIGH end, v = function(s, e) return O.band(s.int_dex_luk_packed, 255) end },
		},
	},
})

D("SPURT", {
	fields = { val1 = "val1_unused", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		str = {
			{ at = 6795, op = "add", v = 10 },
		},
	},
})

D("SP_SHA", {
	fields = { val1 = "val1_unused", val2 = "speed_slow", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		speed = {
			{ at = 8150, op = "gmax", group = "slow", when = function(s, e) return not ((e.unit.type == "pc" and e.sc.HIDING ~= nil) and e.skill("RG_TUNNELDRIVE") > 0) and not ((e.unit.type == "pc" and e.sc.CHASEWALK ~= nil) and e.sc.CHASEWALK.speed_value < 0) end, v = function(s, e) return s.speed_slow end },
		},
	},
})

D("STARSTANCE", {
	fields = { val1 = "lv", val2 = "aspd_rate_dec_arg", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		aspd_rate = {
			{ at = 8557, op = "add", v = function(s, e) return -(10 * s.aspd_rate_dec_arg) end },
		},
	},
})

D("STAR_COMFORT", {
	fields = { val1 = "val1_unused", val2 = "aspd_rate_quicken", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		aspd_rate = {
			{ at = 8436, op = "gset", group = "quicken", v = function(s, e) return s.aspd_rate_quicken end },
		},
	},
})

D("STEALTHFIELD", {
	fields = { val1 = "val1_unused", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		speed = {
			{ at = 8134, op = "gmax", group = "slow", when = function(s, e) return not ((e.unit.type == "pc" and e.sc.HIDING ~= nil) and e.skill("RG_TUNNELDRIVE") > 0) and not ((e.unit.type == "pc" and e.sc.CHASEWALK ~= nil) and e.sc.CHASEWALK.speed_value < 0) end, v = 20 },
		},
	},
})

D("STEELBODY", {
	fields = { val1 = "val1_unused", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		def = {
			{ at = 7784, op = "final", v = 90 },
		},
		mdef = {
			{ at = 7954, op = "final", v = 90 },
		},
		speed = {
			{ at = 8237, op = "set", v = 200 },
		},
		aspd_rate = {
			{ at = 8513, op = "add", v = 250 },
		},
	},
})

D("STOMACHACHE", {
	fields = { val1 = "stat_dec", val2 = "sp_consume", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		str = {
			{ at = 6817, op = "add", v = function(s, e) return -s.stat_dec end },
		},
		agi = {
			{ at = 6888, op = "add", v = function(s, e) return -s.stat_dec end },
		},
		vit = {
			{ at = 6947, op = "add", v = function(s, e) return -s.stat_dec end },
		},
		int = {
			{ at = 7020, op = "add", v = function(s, e) return -s.stat_dec end },
		},
		dex = {
			{ at = 7098, op = "add", v = function(s, e) return -s.stat_dec end },
		},
		luk = {
			{ at = 7161, op = "add", v = function(s, e) return -s.stat_dec end },
		},
	},
})

D("STONE", {
	fields = { val1 = "val1_unused", val2 = "val2_unused", val3 = "petrify_time", val4 = "remaining_time" },
	contributes = {
		def = {
			{ at = 7814, op = "div", v = 2 },
		},
		mdef = {
			{ at = 7972, op = "mul_pct", v = 25 },
		},
		element_lv = {
			{ at = 8832, op = "final", v = 1 },
		},
	},
})

D("STONEHARDSKIN", {
	fields = { val1 = "def_mdef_inc", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		def = {
			{ at = 7812, op = "add", v = function(s, e) return s.def_mdef_inc end },
		},
		mdef = {
			{ at = 7970, op = "add", v = function(s, e) return s.def_mdef_inc end },
		},
	},
})

D("STONE_WALL", {
	in_pre_db = false, -- not in db/pre-re/status.yml: the stat code knows it, a PRE server never starts it
	fields = { val1 = "lv", val2 = "def_inc", val3 = "mdef_inc", val4 = "val4_unused" },
	contributes = {
		def = {
			{ at = 7858, op = "add", v = function(s, e) return s.def_inc end },
		},
		mdef = {
			{ at = 7988, op = "add", v = function(s, e) return s.mdef_inc end },
		},
	},
})

D("STRIKING", {
	fields = { val1 = "lv", val2 = "watk_inc", val3 = "sp_cost", val4 = "val4_unused" },
	contributes = {
		watk = {
			{ at = 7403, op = "add", v = function(s, e) return s.watk_inc end },
		},
	},
})

D("STRIPARMOR", {
	fields = { val1 = "val1_unused", val2 = "stat_pct_dec", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		vit = {
			{ at = 6955, op = "mul_pct", when = function(s, e) return e.unit.type ~= "pc" end, v = function(s, e) return -s.stat_pct_dec end },
		},
	},
})

D("STRIPHELM", {
	fields = { val1 = "val1_unused", val2 = "stat_pct_dec", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		int = {
			{ at = 7038, op = "mul_pct", when = function(s, e) return e.unit.type ~= "pc" end, v = function(s, e) return -s.stat_pct_dec end },
		},
	},
})

D("STRIPSHIELD", {
	fields = { val1 = "val1_unused", val2 = "def_pct_dec", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		def = {
			{ at = 7828, op = "mul_pct", when = function(s, e) return e.unit.type ~= "pc" end, v = function(s, e) return -s.def_pct_dec end },
		},
	},
})

D("STRONG_PROTECTION_OPTION", {
	in_pre_db = false, -- not in db/pre-re/status.yml: the stat code knows it, a PRE server never starts it
	fields = { val1 = "val1_unused", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		element = {
			{ at = 8798, op = "final", when = function(s, e) return (e.sc.STONE ~= nil or e.sc.STRONG_PROTECTION_OPTION ~= nil) end, v = function(s, e) return K.ELE_EARTH end, also = {"STONE"} },
		},
		element_lv = {
			{ at = 8845, op = "final", when = function(s, e) return ((((e.sc.FLAMEARMOR_OPTION ~= nil or e.sc.CRYSTAL_ARMOR_OPTION ~= nil) or e.sc.EYES_OF_STORM_OPTION ~= nil) or e.sc.STRONG_PROTECTION_OPTION ~= nil) or e.sc.POISON_SHIELD_OPTION ~= nil) end, v = 1, also = {"EYES_OF_STORM_OPTION", "POISON_SHIELD_OPTION", "FLAMEARMOR_OPTION", "CRYSTAL_ARMOR_OPTION"} },
		},
	},
})

D("SUITON", {
	fields = { val1 = "val1_unused", val2 = "stat_dec", val3 = "stat_speed_slow", val4 = "val4_unused" },
	contributes = {
		agi = {
			{ at = 6874, op = "add", when = function(s, e) return s.stat_speed_slow ~= 0 end, v = function(s, e) return -s.stat_dec end },
		},
		speed = {
			{ at = 8124, op = "gmax", group = "slow", when = function(s, e) return not ((e.unit.type == "pc" and e.sc.HIDING ~= nil) and e.skill("RG_TUNNELDRIVE") > 0) and not ((e.unit.type == "pc" and e.sc.CHASEWALK ~= nil) and e.sc.CHASEWALK.speed_value < 0) end, v = function(s, e) return s.stat_speed_slow end },
		},
	},
})

D("SUNSTANCE", {
	fields = { val1 = "lv", val2 = "batk_watk_pct_inc", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		batk = {
			{ at = 7346, op = "mul_pct", v = function(s, e) return s.batk_watk_pct_inc end },
		},
		watk = {
			{ at = 7434, op = "mul_pct", v = function(s, e) return s.batk_watk_pct_inc end },
		},
	},
})

D("SUN_COMFORT", {
	fields = { val1 = "val1_unused", val2 = "def2_inc", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		def2 = {
			{ at = 7895, op = "add", v = function(s, e) return s.def2_inc end },
		},
	},
})

D("SWINGDANCE", {
	fields = { val1 = "lv", val2 = "val2_unused", val3 = "aspd_rate_speed_val", val4 = "val4_unused" },
	contributes = {
		speed = {
			{ at = 8197, op = "gmax", group = "haste", v = function(s, e) return s.aspd_rate_speed_val end },
		},
		aspd_rate = {
			{ at = 8541, op = "add", v = function(s, e) return -(s.aspd_rate_speed_val * 10) end },
		},
	},
})

D("SWOO", {
	fields = { val1 = "val1_unused", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		speed = {
			{ at = 8126, op = "gmax", group = "slow", when = function(s, e) return not ((e.unit.type == "pc" and e.sc.HIDING ~= nil) and e.skill("RG_TUNNELDRIVE") > 0) and not ((e.unit.type == "pc" and e.sc.CHASEWALK ~= nil) and e.sc.CHASEWALK.speed_value < 0) end, v = 300 },
		},
	},
})

D("SWORDCLAN", {
	fields = { val1 = "val1_unused", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		hp_fix = {
			{ at = 3210, op = "add", v = 30 },
		},
		sp_fix = {
			{ at = 3368, op = "add", v = 10 },
		},
		str = {
			{ at = 6821, op = "add", v = 1 },
		},
		vit = {
			{ at = 6951, op = "add", v = 1 },
		},
	},
})

D("SYMPHONYOFLOVER", {
	fields = { val1 = "lv", val2 = "val2_unused", val3 = "mdef_pct_inc", val4 = "val4_unused" },
	contributes = {
		mdef = {
			{ at = 7982, op = "mul_pct", v = function(s, e) return s.mdef_pct_inc end },
		},
	},
})

D("TALISMAN_OF_MAGICIAN", {
	in_pre_db = false, -- not in db/pre-re/status.yml: the stat code knows it, a PRE server never starts it
	fields = { val1 = "lv", val2 = "smatk_inc", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		smatk = {
			{ at = 8625, op = "add", v = function(s, e) return s.smatk_inc end },
		},
	},
})

D("TALISMAN_OF_WARRIOR", {
	in_pre_db = false, -- not in db/pre-re/status.yml: the stat code knows it, a PRE server never starts it
	fields = { val1 = "lv", val2 = "patk_inc", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		patk = {
			{ at = 8590, op = "add", v = function(s, e) return s.patk_inc end },
		},
	},
})

D("TEARGAS", {
	fields = { val1 = "val1_unused", val2 = "hp_drain", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		hit = {
			{ at = 7601, op = "mul_pct", v = -50 },
		},
		flee = {
			{ at = 7729, op = "mul_pct", v = -50 },
		},
	},
})

D("TEMPERING", {
	in_pre_db = false, -- not in db/pre-re/status.yml: the stat code knows it, a PRE server never starts it
	fields = { val1 = "val1_unused", val2 = "patk_inc", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		patk = {
			{ at = 8583, op = "add", v = function(s, e) return s.patk_inc end },
		},
	},
})

D("TEMPORARY_COMMUNION", {
	in_pre_db = false, -- not in db/pre-re/status.yml: the stat code knows it, a PRE server never starts it
	fields = { val1 = "lv", val2 = "patk_smatk_hplus_inc", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		patk = {
			{ at = 8592, op = "add", v = function(s, e) return s.patk_smatk_hplus_inc end },
		},
		smatk = {
			{ at = 8629, op = "add", v = function(s, e) return s.patk_smatk_hplus_inc end },
		},
		hplus = {
			{ at = 8703, op = "add", v = function(s, e) return s.patk_smatk_hplus_inc end },
		},
	},
})

D("TIDAL_WEAPON", {
	fields = { val1 = "val1_unused", val2 = "watk_pct_inc", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		watk = {
			{ at = 7420, op = "mul_pct", v = function(s, e) return s.watk_pct_inc end },
		},
	},
})

D("TIDAL_WEAPON_OPTION", {
	fields = { val1 = "val1_unused", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		atk_element = {
			{ at = 8884, op = "final", when = function(s, e) return (e.sc.TIDAL_WEAPON_OPTION ~= nil or e.sc.TIDAL_WEAPON ~= nil) end, v = function(s, e) return K.ELE_WATER end, also = {"TIDAL_WEAPON"} },
		},
	},
})

D("TINDER_BREAKER", {
	fields = { val1 = "val1_unused", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
})

D("TINDER_BREAKER2", {
	fields = { val1 = "val1_unused", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		flee = {
			{ at = 7719, op = "mul_pct", when = function(s, e) return (e.sc.TINDER_BREAKER ~= nil or e.sc.TINDER_BREAKER2 ~= nil) end, v = -50, also = {"TINDER_BREAKER"} },
		},
	},
})

D("TOXIN_OF_MANDARA", {
	in_pre_db = false, -- not in db/pre-re/status.yml: the stat code knows it, a PRE server never starts it
	fields = { val1 = "lv", val2 = "res_dec", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		res = {
			{ at = 8663, op = "add", v = function(s, e) return -s.res_dec end },
		},
	},
})

D("TROPIC_OPTION", {
	fields = { val1 = "val1_unused", val2 = "watk_inc", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		watk = {
			{ at = 7418, op = "add", v = function(s, e) return s.watk_inc end },
		},
	},
})

D("TRUESIGHT", {
	fields = { val1 = "lv", val2 = "cri_inc", val3 = "hit_inc", val4 = "val4_unused" },
	contributes = {
		str = {
			{ at = 6793, op = "add", v = 5 },
		},
		agi = {
			{ at = 6864, op = "add", v = 5 },
		},
		vit = {
			{ at = 6937, op = "add", v = 5 },
		},
		int = {
			{ at = 6998, op = "add", v = 5 },
		},
		dex = {
			{ at = 7078, op = "add", v = 5 },
		},
		luk = {
			{ at = 7149, op = "add", v = 5 },
		},
		hit = {
			{ at = 7573, op = "add", v = function(s, e) return s.hit_inc end },
		},
		cri = {
			{ at = 7535, op = "add", v = function(s, e) return s.cri_inc end },
		},
	},
})

D("TWOHANDQUICKEN", {
	fields = { val1 = "val1_unused", val2 = "aspd_rate_quicken", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		aspd_rate = {
			{ at = 8440, op = "gmax", group = "quicken", v = function(s, e) return s.aspd_rate_quicken end },
		},
	},
})

D("T_FIFTH_GOD", {
	in_pre_db = false, -- not in db/pre-re/status.yml: the stat code knows it, a PRE server never starts it
	fields = { val1 = "lv", val2 = "smatk_inc", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		smatk = {
			{ at = 8627, op = "add", v = function(s, e) return s.smatk_inc end },
		},
	},
})

D("UNIVERSESTANCE", {
	fields = { val1 = "lv", val2 = "stat_inc", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		str = {
			{ at = 6833, op = "add", v = function(s, e) return s.stat_inc end },
		},
		agi = {
			{ at = 6906, op = "add", v = function(s, e) return s.stat_inc end },
		},
		vit = {
			{ at = 6967, op = "add", v = function(s, e) return s.stat_inc end },
		},
		int = {
			{ at = 7034, op = "add", v = function(s, e) return s.stat_inc end },
		},
		dex = {
			{ at = 7120, op = "add", v = function(s, e) return s.stat_inc end },
		},
		luk = {
			{ at = 7181, op = "add", v = function(s, e) return s.stat_inc end },
		},
	},
})

D("UPHEAVAL_OPTION", {
	fields = { val1 = "val1_unused", val2 = "maxhp_bonus_inc", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		hp_rate = {
			{ at = 3264, op = "add", v = function(s, e) return s.maxhp_bonus_inc end },
		},
	},
})

D("VENOMBLEED", {
	fields = { val1 = "val1_unused", val2 = "reflect_dec", val3 = "hp_penalty_flag", val4 = "val4_unused" },
	contributes = {
		hp_rate = {
			{ at = 3280, op = "add", when = function(s, e) return s.hp_penalty_flag == 1 end, v = -15 },
		},
	},
})

D("VIOLENTGALE", {
	fields = { val1 = "lv", val2 = "flee_inc", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		flee = {
			{ at = 7659, op = "add", v = function(s, e) return s.flee_inc end },
		},
	},
})

D("VOLCANO", {
	fields = { val1 = "lv", val2 = "batk_watk_inc", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		batk = {
			{ at = 7322, op = "add", v = function(s, e) return s.batk_watk_inc end },
		},
		watk = {
			{ at = 7374, op = "add", when = function(s, e) return e.unit.type == "mob" end, v = function(s, e) return s.batk_watk_inc end },
		},
	},
})

D("WALKSPEED", {
	fields = { val1 = "speed_pct", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		speed = {
			{ at = 8243, op = "scale", when = function(s, e) return s.speed_pct > 0 end, v = 100, v2 = function(s, e) return s.speed_pct end },
		},
	},
})

D("WATERWEAPON", {
	fields = { val1 = "val1_unused", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		atk_element = {
			{ at = 8865, op = "final", when = function(s, e) return (e.sc.WATERWEAPON ~= nil or ((e.sc.WATER_INSIGNIA ~= nil and e.sc.WATER_INSIGNIA.level == 2))) end, v = function(s, e) return K.ELE_WATER end, also = {"WATER_INSIGNIA"} },
		},
	},
})

D("WATER_BARRIER", {
	fields = { val1 = "val1_unused", val2 = "watk_flee_dec", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		watk = {
			{ at = 7378, op = "add", v = function(s, e) return -s.watk_flee_dec end },
		},
		flee = {
			{ at = 7681, op = "add", v = function(s, e) return -s.watk_flee_dec end },
		},
	},
})

D("WATER_INSIGNIA", {
	fields = { val1 = "level", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
})

D("WATKFOOD", {
	fields = { val1 = "watk_inc", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		watk = {
			{ at = 7372, op = "add", v = function(s, e) return s.watk_inc end },
		},
	},
})

D("WEDDING", {
	fields = { val1 = "val1_unused", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		speed = {
			{ at = 8112, op = "gmax", group = "slow", when = function(s, e) return not ((e.unit.type == "pc" and e.sc.HIDING ~= nil) and e.skill("RG_TUNNELDRIVE") > 0) and not ((e.unit.type == "pc" and e.sc.CHASEWALK ~= nil) and e.sc.CHASEWALK.speed_value < 0) end, v = 100 },
		},
	},
})

D("WHISTLE", {
	fields = { val1 = "val1_unused", val2 = "flee_inc", val3 = "flee2_inc", val4 = "val4_unused" },
	contributes = {
		flee = {
			{ at = 7655, op = "add", v = function(s, e) return s.flee_inc end },
		},
		flee2 = {
			{ at = 7751, op = "add", v = function(s, e) return s.flee2_inc end },
		},
	},
})

D("WIDEWEB", {
	in_pre_db = false, -- not in db/pre-re/status.yml: the stat code knows it, a PRE server never starts it
	fields = { val1 = "val1_unused", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		flee = {
			{ at = 7699, op = "mul_pct", when = function(s, e) return (e.sc.SPIDERWEB ~= nil or e.sc.WIDEWEB ~= nil) end, v = -50, also = {"SPIDERWEB"} },
		},
	},
})

D("WILD_STORM_OPTION", {
	fields = { val1 = "val1_unused", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		fix_aspd = {
			{ at = 8405, op = "add", when = function(s, e) return ((e.sc.GUST_OPTION ~= nil or e.sc.BLAST_OPTION ~= nil) or e.sc.WILD_STORM_OPTION ~= nil) end, v = -50, also = {"BLAST_OPTION", "GUST_OPTION"} },
		},
	},
})

D("WILD_WALK", {
	in_pre_db = false, -- not in db/pre-re/status.yml: the stat code knows it, a PRE server never starts it
	fields = { val1 = "lv", val2 = "speed_haste", val3 = "flee_inc", val4 = "val4_unused" },
	contributes = {
		flee = {
			{ at = 7685, op = "add", v = function(s, e) return s.flee_inc end },
		},
		speed = {
			{ at = 8214, op = "gmax", group = "haste", v = function(s, e) return s.speed_haste end },
		},
	},
})

D("WINDWALK", {
	fields = { val1 = "lv", val2 = "flee_inc", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		flee = {
			{ at = 7657, op = "add", v = function(s, e) return s.flee_inc end },
		},
		speed = {
			{ at = 8173, op = "gmax", group = "haste", v = function(s, e) return 2 * s.lv end },
		},
	},
})

D("WINDWEAPON", {
	fields = { val1 = "val1_unused", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		atk_element = {
			{ at = 8874, op = "final", when = function(s, e) return (e.sc.WINDWEAPON ~= nil or ((e.sc.WIND_INSIGNIA ~= nil and e.sc.WIND_INSIGNIA.level == 2))) end, v = function(s, e) return K.ELE_WIND end, also = {"WIND_INSIGNIA"} },
		},
	},
})

D("WIND_INSIGNIA", {
	fields = { val1 = "level", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		aspd_rate = {
			{ at = 8555, op = "add", when = function(s, e) return s.level == 2 end, v = -100 },
		},
	},
})

D("WIND_STEP_OPTION", {
	fields = { val1 = "val1_unused", val2 = "flee_speed_val", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		flee = {
			{ at = 7717, op = "mul_pct", v = function(s, e) return s.flee_speed_val end },
		},
		speed = {
			{ at = 8199, op = "gmax", group = "haste", v = function(s, e) return s.flee_speed_val end },
		},
	},
})

D("ZANGETSU", {
	fields = { val1 = "lv", val2 = "batk_inc", val3 = "matk_inc", val4 = "val4_unused" },
	contributes = {
		batk = {
			{ at = 7338, op = "add", v = function(s, e) return s.batk_inc end },
		},
	},
})

D("ZEPHYR", {
	fields = { val1 = "val1_unused", val2 = "flee_inc", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		flee = {
			{ at = 7721, op = "add", v = function(s, e) return s.flee_inc end },
		},
	},
})

D("_STRIPACCESSORY", {
	fields = { val1 = "val1_unused", val2 = "stat_pct_dec", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		int = {
			{ at = 7040, op = "mul_pct", when = function(s, e) return e.unit.type ~= "pc" end, v = function(s, e) return -s.stat_pct_dec end },
		},
		dex = {
			{ at = 7108, op = "mul_pct", when = function(s, e) return e.unit.type ~= "pc" end, v = function(s, e) return -s.stat_pct_dec end },
		},
		luk = {
			{ at = 7165, op = "mul_pct", when = function(s, e) return e.unit.type ~= "pc" end, v = function(s, e) return -s.stat_pct_dec end },
		},
	},
})

D("_UNLUCKY", {
	fields = { val1 = "val1_unused", val2 = "cri_flee2_val", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		cri = {
			{ at = 7547, op = "add", v = function(s, e) return -s.cri_flee2_val end },
		},
		flee2 = {
			{ at = 7753, op = "mul_pct", v = function(s, e) return -s.cri_flee2_val end },
		},
	},
})

D("_WEAKNESS", {
	fields = { val1 = "lv", val2 = "maxhp_bonus_dec", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		hp_rate = {
			{ at = 3284, op = "add", v = function(s, e) return -s.maxhp_bonus_dec end },
		},
	},
})

