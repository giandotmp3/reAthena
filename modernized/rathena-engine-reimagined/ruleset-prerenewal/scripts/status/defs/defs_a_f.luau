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

D("ABYSS_SLAYER", {
	in_pre_db = false, -- not in db/pre-re/status.yml: the stat code knows it, a PRE server never starts it
	fields = { val1 = "lv", val2 = "patk_smatk_inc", val3 = "hit_inc", val4 = "val4_unused" },
	contributes = {
		hit = {
			{ at = 7619, op = "add", v = function(s, e) return s.hit_inc end },
		},
		patk = {
			{ at = 8579, op = "add", v = function(s, e) return s.patk_smatk_inc end },
		},
		smatk = {
			{ at = 8616, op = "add", v = function(s, e) return s.patk_smatk_inc end },
		},
	},
})

D("ACCELERATION", {
	fields = { val1 = "val1_unused", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
})

D("ADJUSTMENT", {
	fields = { val1 = "val1_unused", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		hit = {
			{ at = 7581, op = "add", v = -30 },
		},
		flee = {
			{ at = 7667, op = "add", v = 30 },
		},
	},
})

D("ADORAMUS", {
	fields = { val1 = "lv", val2 = "stat_dec", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		agi = {
			{ at = 6882, op = "add", v = function(s, e) return -s.stat_dec end },
		},
	},
})

D("ADRENALINE", {
	fields = { val1 = "lv", val2 = "val2_unused", val3 = "aspd_rate_quicken", val4 = "val4_unused" },
	contributes = {
		aspd_rate = {
			{ at = 8456, op = "gmax", group = "quicken", v = function(s, e) return s.aspd_rate_quicken end },
		},
	},
})

D("ADRENALINE2", {
	fields = { val1 = "lv", val2 = "val2_unused", val3 = "aspd_rate_quicken", val4 = "val4_unused" },
	contributes = {
		aspd_rate = {
			{ at = 8452, op = "gmax", group = "quicken", v = function(s, e) return s.aspd_rate_quicken end },
		},
	},
})

D("AGIUP", {
	fields = { val1 = "speed_haste", val2 = "flee_pct_inc", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		flee = {
			{ at = 7697, op = "mul_pct", v = function(s, e) return s.flee_pct_inc end },
		},
		speed = {
			{ at = 8169, op = "gmax", group = "haste", v = function(s, e) return s.speed_haste end },
		},
	},
})

D("AIN_RHAPSODY", {
	in_pre_db = false, -- not in db/pre-re/status.yml: the stat code knows it, a PRE server never starts it
	fields = { val1 = "lv", val2 = "res_dec", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		res = {
			{ at = 8661, op = "add", v = function(s, e) return -s.res_dec end },
		},
	},
})

D("ALL_RIDING", {
	fields = { val1 = "val1_unused", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		speed = {
			{ at = 8078, op = "gset", group = "mount", when = function(s, e) return e.sc.FUSION == nil and e.unit.type == "pc" and not (O.band(e.unit.option, K.OPTION_RIDING) ~= 0 or O.band(e.unit.option, K.OPTION_DRAGON) ~= 0) and not (O.band(e.unit.option, K.OPTION_WUGRIDER) ~= 0) end, v = function(s, e) return e.cfg.rental_mount_speed_boost end },
		},
	},
})

D("ALL_STAT_DOWN", {
	in_pre_db = false, -- not in db/pre-re/status.yml: the stat code knows it, a PRE server never starts it
	fields = { val1 = "lv", val2 = "stat_dec", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		str = {
			{ at = 6835, op = "add", v = function(s, e) return -s.stat_dec end },
		},
		agi = {
			{ at = 6908, op = "add", v = function(s, e) return -s.stat_dec end },
		},
		vit = {
			{ at = 6969, op = "add", v = function(s, e) return -s.stat_dec end },
		},
		int = {
			{ at = 7047, op = "add", v = function(s, e) return -s.stat_dec end },
		},
		dex = {
			{ at = 7122, op = "add", v = function(s, e) return -s.stat_dec end },
		},
		luk = {
			{ at = 7183, op = "add", v = function(s, e) return -s.stat_dec end },
		},
	},
})

D("ANALYZE", {
	fields = { val1 = "lv", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		def = {
			{ at = 7834, op = "mul_pct", v = function(s, e) return -((14 * s.lv)) end },
		},
		def2 = {
			{ at = 7920, op = "mul_pct", v = function(s, e) return -((14 * s.lv)) end },
		},
		mdef = {
			{ at = 7980, op = "mul_pct", v = function(s, e) return -((14 * s.lv)) end },
		},
		mdef2 = {
			{ at = 8023, op = "mul_pct", v = function(s, e) return -((14 * s.lv)) end },
		},
	},
})

D("ANGELUS", {
	fields = { val1 = "lv", val2 = "def_pct_inc", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		def2 = {
			{ at = 7904, op = "mul_pct", v = function(s, e) return s.def_pct_inc end },
		},
	},
})

D("ANGRIFFS_MODUS", {
	fields = { val1 = "lv", val2 = "atk_val", val3 = "flee_dec", val4 = "drain_timer" },
	contributes = {
		hp_rate = {
			{ at = 3256, op = "add", v = function(s, e) return (5 * s.lv) end },
		},
		batk = {
			{ at = 7334, op = "add", v = function(s, e) return s.atk_val end },
		},
		watk = {
			{ at = 7424, op = "mul_pct", v = function(s, e) return s.atk_val end },
		},
		flee = {
			{ at = 7665, op = "add", v = function(s, e) return -s.flee_dec end },
		},
		def = {
			{ at = 7810, op = "add", v = function(s, e) return -(20 + (10 * s.lv)) end },
		},
	},
})

D("APPLEIDUN", {
	fields = { val1 = "lv", val2 = "maxhp_bonus_inc", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		hp_rate = {
			{ at = 3234, op = "add", v = function(s, e) return s.maxhp_bonus_inc end },
		},
	},
})

D("AQUAPLAY_OPTION", {
	fields = { val1 = "val1_unused", val2 = "matk_inc", val3 = "val3_unused", val4 = "val4_unused" },
})

D("ARCLOUSEDASH", {
	fields = { val1 = "lv", val2 = "stat_inc", val3 = "speed_haste", val4 = "ranged_atk_bonus" },
	contributes = {
		agi = {
			{ at = 6898, op = "add", v = function(s, e) return s.stat_inc end },
		},
		speed = {
			{ at = 8203, op = "gmax", group = "haste", v = function(s, e) return s.speed_haste end },
		},
	},
})

D("ARCWANDCLAN", {
	fields = { val1 = "val1_unused", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		hp_fix = {
			{ at = 3212, op = "add", v = 30 },
		},
		sp_fix = {
			{ at = 3370, op = "add", v = 10 },
		},
		int = {
			{ at = 7024, op = "add", v = 1 },
		},
		dex = {
			{ at = 7102, op = "add", v = 1 },
		},
	},
})

D("ARMOR", {
	fields = { val1 = "val1_unused", val2 = "damage_divisor", val3 = "range", val4 = "type" },
})

D("ASH", {
	fields = { val1 = "val1_unused", val2 = "hit_pct_dec", val3 = "def_def2_pct_dec", val4 = "batk_flee_pct_dec" },
	contributes = {
		batk = {
			{ at = 7330, op = "mul_pct", v = function(s, e) return -s.batk_flee_pct_dec end },
		},
		hit = {
			{ at = 7599, op = "mul_pct", v = function(s, e) return -s.hit_pct_dec end },
		},
		flee = {
			{ at = 7723, op = "mul_pct", v = function(s, e) return -s.batk_flee_pct_dec end },
		},
		def = {
			{ at = 7852, op = "mul_pct", v = function(s, e) return -s.def_def2_pct_dec end },
		},
		def2 = {
			{ at = 7922, op = "mul_pct", v = function(s, e) return -s.def_def2_pct_dec end },
		},
	},
})

D("ASPDPOTION0", {
	fields = { val1 = "aspd_inc", val2 = "aspd_rate_dec", val3 = "val3_unused", val4 = "val4_unused" },
})

D("ASPDPOTION1", {
	fields = { val1 = "aspd_inc", val2 = "aspd_rate_dec", val3 = "val3_unused", val4 = "val4_unused" },
})

D("ASPDPOTION2", {
	fields = { val1 = "aspd_inc", val2 = "aspd_rate_dec", val3 = "val3_unused", val4 = "val4_unused" },
})

D("ASPDPOTION3", {
	fields = { val1 = "aspd_inc", val2 = "aspd_rate_dec", val3 = "val3_unused", val4 = "val4_unused" },
})

D("ASPERSIO", {
	fields = { val1 = "val1_unused", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		atk_element = {
			{ at = 8878, op = "final", v = function(s, e) return K.ELE_HOLY end },
		},
	},
})

D("ASSNCROS", {
	fields = { val1 = "val1_unused", val2 = "aspd_rate_quicken", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		aspd_rate = {
			{ at = 8476, op = "gmax", group = "quicken", when = function(s, e) return e.unit.type ~= "pc" end, v = function(s, e) return s.aspd_rate_quicken end },
		},
	},
})

D("ATTACK_STANCE", {
	in_pre_db = false, -- not in db/pre-re/status.yml: the stat code knows it, a PRE server never starts it
	fields = { val1 = "lv", val2 = "def_dec", val3 = "patk_smatk_inc", val4 = "val4_unused" },
	contributes = {
		def = {
			{ at = 7866, op = "add", v = function(s, e) return -s.def_dec end },
		},
		patk = {
			{ at = 8585, op = "add", v = function(s, e) return s.patk_smatk_inc end },
		},
		smatk = {
			{ at = 8622, op = "add", v = function(s, e) return s.patk_smatk_inc end },
		},
	},
})

D("AVOID", {
	fields = { val1 = "lv", val2 = "speed_haste", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		speed = {
			{ at = 8185, op = "gmax", group = "haste", v = function(s, e) return s.speed_haste end },
		},
	},
})

D("BANANA_BOMB", {
	fields = { val1 = "val1_unused", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		luk = {
			{ at = 7167, op = "add", v = -75 },
		},
	},
})

D("BANDING", {
	fields = { val1 = "lv", val2 = "def_cond", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		def = {
			{ at = 7840, op = "add", when = function(s, e) return s.def_cond > 1 end, v = function(s, e) return 6 * s.lv end },
		},
	},
})

D("BATTLEORDERS", {
	fields = { val1 = "val1_unused", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		str = {
			{ at = 6787, op = "add", v = 5 },
		},
		int = {
			{ at = 6996, op = "add", v = 5 },
		},
		dex = {
			{ at = 7074, op = "add", v = 5 },
		},
	},
})

D("BENEDICTIO", {
	fields = { val1 = "val1_unused", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		element = {
			{ at = 8806, op = "final", v = function(s, e) return K.ELE_HOLY end },
		},
		element_lv = {
			{ at = 8834, op = "final", v = 1 },
		},
	},
})

D("BENEDICTUM", {
	in_pre_db = false, -- not in db/pre-re/status.yml: the stat code knows it, a PRE server never starts it
	fields = { val1 = "lv", val2 = "pow_con_crt_inc", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		pow = {
			{ at = 7202, op = "add", v = function(s, e) return s.pow_con_crt_inc end },
		},
		con = {
			{ at = 7282, op = "add", v = function(s, e) return s.pow_con_crt_inc end },
		},
		crt = {
			{ at = 7302, op = "add", v = function(s, e) return s.pow_con_crt_inc end },
		},
	},
})

D("BERSERK", {
	fields = { val1 = "lv", val2 = "no_hp_loss", val3 = "skill_duration", val4 = "val4_unused" },
	contributes = {
		hp_rate = {
			{ at = 3238, op = "add", v = 200 },
		},
		flee = {
			{ at = 7701, op = "mul_pct", v = -50 },
		},
		def = {
			{ at = 7775, op = "final", v = 0 },
		},
		def2 = {
			{ at = 7888, op = "final", v = 0 },
		},
		mdef = {
			{ at = 7950, op = "final", v = 0 },
		},
		mdef2 = {
			{ at = 8012, op = "final", v = 0 },
		},
		speed = {
			{ at = 8181, op = "gmax", group = "haste", v = 25 },
		},
		aspd_rate = {
			{ at = 8493, op = "add", v = -300 },
		},
	},
})

D("BEYONDOFWARCRY", {
	fields = { val1 = "lv", val2 = "stat_dec", val3 = "cri_maxhp_bonus_val", val4 = "val4_unused" },
	contributes = {
		hp_rate = {
			{ at = 3282, op = "add", v = function(s, e) return -s.cri_maxhp_bonus_val end },
		},
		str = {
			{ at = 6813, op = "add", v = function(s, e) return -s.stat_dec end },
		},
		cri = {
			{ at = 7551, op = "add", v = function(s, e) return s.cri_maxhp_bonus_val end },
		},
	},
})

D("BLAST_OPTION", {
	fields = { val1 = "val1_unused", val2 = "val2_unused", val3 = "element_change", val4 = "val4_unused" },
})

D("BLESSING", {
	fields = { val1 = "lv", val2 = "stat_inc", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		str = {
			{ at = 6800, op = "add", when = function(s, e) return s.stat_inc ~= 0 end, v = function(s, e) return s.stat_inc end },
			{ at = 6802, op = "mul_frac", when = function(s, e) return not (s.stat_inc ~= 0) end, v = -1, v2 = 2 },
		},
		int = {
			{ at = 7001, op = "add", when = function(s, e) return s.stat_inc ~= 0 end, v = function(s, e) return s.stat_inc end },
			{ at = 7003, op = "mul_frac", when = function(s, e) return not (s.stat_inc ~= 0) end, v = -1, v2 = 2 },
		},
		dex = {
			{ at = 7083, op = "add", when = function(s, e) return s.stat_inc ~= 0 end, v = function(s, e) return s.stat_inc end },
			{ at = 7085, op = "mul_frac", when = function(s, e) return not (s.stat_inc ~= 0) end, v = -1, v2 = 2 },
		},
	},
})

D("BLESSING_OF_M_CREATURES", {
	in_pre_db = false, -- not in db/pre-re/status.yml: the stat code knows it, a PRE server never starts it
	fields = { val1 = "lv", val2 = "patk_smatk_inc", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		patk = {
			{ at = 8594, op = "add", v = function(s, e) return s.patk_smatk_inc end },
		},
		smatk = {
			{ at = 8631, op = "add", v = function(s, e) return s.patk_smatk_inc end },
		},
	},
})

D("BLIND", {
	fields = { val1 = "val1_unused", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		hit = {
			{ at = 7591, op = "mul_pct", v = -25 },
		},
		flee = {
			{ at = 7703, op = "mul_pct", v = -25 },
		},
	},
})

D("BURNING", {
	fields = { val1 = "lv", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		mdef = {
			{ at = 7976, op = "mul_pct", v = -25 },
		},
		mdef2 = {
			{ at = 8021, op = "mul_pct", v = -25 },
		},
	},
})

D("B_TRAP", {
	fields = { val1 = "lv", val2 = "val2_unused", val3 = "speed_slow", val4 = "val4_unused" },
	contributes = {
		speed = {
			{ at = 8146, op = "gmax", group = "slow", when = function(s, e) return not ((e.unit.type == "pc" and e.sc.HIDING ~= nil) and e.skill("RG_TUNNELDRIVE") > 0) and not ((e.unit.type == "pc" and e.sc.CHASEWALK ~= nil) and e.sc.CHASEWALK.speed_value < 0) end, v = function(s, e) return s.speed_slow end },
		},
	},
})

D("CAMOUFLAGE", {
	fields = { val1 = "lv", val2 = "val2_unused", val3 = "def_def2_pct_dec_arg", val4 = "val4_unused" },
	contributes = {
		def = {
			{ at = 7844, op = "mul_pct", v = function(s, e) return -(5 * s.def_def2_pct_dec_arg) end },
		},
		def2 = {
			{ at = 7928, op = "mul_pct", v = function(s, e) return -(5 * s.def_def2_pct_dec_arg) end },
		},
		speed = {
			{ at = 8132, op = "gmax", group = "slow", when = function(s, e) return not ((e.unit.type == "pc" and e.sc.HIDING ~= nil) and e.skill("RG_TUNNELDRIVE") > 0) and not ((e.unit.type == "pc" and e.sc.CHASEWALK ~= nil) and e.sc.CHASEWALK.speed_value < 0) and s.lv > 2 end, v = function(s, e) return 25 * (5 - s.lv) end },
		},
	},
})

D("CARTBOOST", {
	fields = { val1 = "val1_unused", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		speed = {
			{ at = 8175, op = "gmax", group = "haste", v = 20 },
		},
	},
})

D("CATNIPPOWDER", {
	fields = { val1 = "lv", val2 = "watk_pct_inc", val3 = "speed_slow", val4 = "val4_unused" },
	contributes = {
		watk = {
			{ at = 7430, op = "mul_pct", v = function(s, e) return s.watk_pct_inc end },
		},
		speed = {
			{ at = 8148, op = "gmax", group = "slow", when = function(s, e) return not ((e.unit.type == "pc" and e.sc.HIDING ~= nil) and e.skill("RG_TUNNELDRIVE") > 0) and not ((e.unit.type == "pc" and e.sc.CHASEWALK ~= nil) and e.sc.CHASEWALK.speed_value < 0) end, v = function(s, e) return s.speed_slow end },
		},
	},
})

D("CHANGE", {
	fields = { val1 = "lv", val2 = "vit_inc", val3 = "int_inc", val4 = "val4_unused" },
	contributes = {
		vit = {
			{ at = 6933, op = "add", v = function(s, e) return s.vit_inc end },
		},
		int = {
			{ at = 6994, op = "add", v = function(s, e) return s.int_inc end },
		},
	},
})

D("CHANGEUNDEAD", {
	fields = { val1 = "val1_unused", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		element = {
			{ at = 8808, op = "final", v = function(s, e) return K.ELE_UNDEAD end },
		},
		element_lv = {
			{ at = 8836, op = "final", v = 1 },
		},
	},
})

D("CHASEWALK", {
	fields = { val1 = "lv", val2 = "sp_interval", val3 = "speed_value", val4 = "sp_cost" },
	contributes = {
		speed = {
			{ at = 8086, op = "gset", group = "slow_force", when = function(s, e) return not ((e.unit.type == "pc" and e.sc.HIDING ~= nil) and e.skill("RG_TUNNELDRIVE") > 0) and e.unit.type == "pc" and s.speed_value < 0 end, v = function(s, e) return s.speed_value end },
			{ at = 8110, op = "gmax", group = "slow", when = function(s, e) return not ((e.unit.type == "pc" and e.sc.HIDING ~= nil) and e.skill("RG_TUNNELDRIVE") > 0) and not ((e.unit.type == "pc" and e.sc.CHASEWALK ~= nil) and s.speed_value < 0) end, v = function(s, e) return s.speed_value end },
		},
	},
})

D("CHASEWALK2", {
	fields = { val1 = "stat_inc", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		str = {
			{ at = 6785, op = "add", v = function(s, e) return s.stat_inc end },
		},
	},
})

D("CHATTERING", {
	fields = { val1 = "val1_unused", val2 = "watk_inc", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		watk = {
			{ at = 7432, op = "add", v = function(s, e) return s.watk_inc end },
		},
	},
})

D("CHEERUP", {
	fields = { val1 = "val1_unused", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		str = {
			{ at = 6827, op = "add", v = 3 },
		},
		agi = {
			{ at = 6900, op = "add", v = 3 },
		},
		vit = {
			{ at = 6961, op = "add", v = 3 },
		},
		int = {
			{ at = 7032, op = "add", v = 3 },
		},
		dex = {
			{ at = 7114, op = "add", v = 3 },
		},
		luk = {
			{ at = 7175, op = "add", v = 3 },
		},
	},
})

D("CHILLY_AIR_OPTION", {
	fields = { val1 = "val1_unused", val2 = "matk_inc", val3 = "val3_unused", val4 = "val4_unused" },
})

D("CLIMAX_CRYIMP", {
	in_pre_db = false, -- not in db/pre-re/status.yml: the stat code knows it, a PRE server never starts it
	fields = { val1 = "val1_unused", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		def = {
			{ at = 7862, op = "add", v = 300 },
		},
		mdef = {
			{ at = 7990, op = "add", v = 100 },
		},
	},
})

D("CLIMAX_DES_HU", {
	in_pre_db = false, -- not in db/pre-re/status.yml: the stat code knows it, a PRE server never starts it
	fields = { val1 = "val1_unused", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
})

D("CLOAKING", {
	fields = { val1 = "lv", val2 = "sp_interval", val3 = "unused", val4 = "mode_flags" },
	contributes = {
		cri = {
			{ at = 7537, op = "mul_frac", v = 1, v2 = 1 },
		},
		speed = {
			{ at = 8116, op = "gmax", group = "slow", when = function(s, e) return not ((e.unit.type == "pc" and e.sc.HIDING ~= nil) and e.skill("RG_TUNNELDRIVE") > 0) and not ((e.unit.type == "pc" and e.sc.CHASEWALK ~= nil) and e.sc.CHASEWALK.speed_value < 0) and (O.band(s.mode_flags, 1)) == 0 end, v = function(s, e) return (s.lv < 3 and 300 or 30 - (3 * s.lv)) end },
			{ at = 8179, op = "gmax", group = "haste", when = function(s, e) return (O.band(s.mode_flags, 1)) == 1 end, v = function(s, e) return (s.lv >= 10 and 25 or (3 * s.lv) - 3) end },
		},
	},
})

D("CLOAKINGEXCEED", {
	fields = { val1 = "lv", val2 = "hit_count", val3 = "speed_haste", val4 = "val4_unused" },
	contributes = {
		speed = {
			{ at = 8189, op = "gmax", group = "haste", v = function(s, e) return s.speed_haste end },
		},
	},
})

D("CLOSECONFINE", {
	fields = { val1 = "val1_unused", val2 = "val2_unused", val3 = "flee_inc", val4 = "val4_unused" },
	contributes = {
		flee = {
			{ at = 7663, op = "add", v = function(s, e) return s.flee_inc end },
		},
	},
})

D("COMPETENTIA", {
	in_pre_db = false, -- not in db/pre-re/status.yml: the stat code knows it, a PRE server never starts it
	fields = { val1 = "lv", val2 = "patk_smatk_inc", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		patk = {
			{ at = 8577, op = "add", v = function(s, e) return s.patk_smatk_inc end },
		},
		smatk = {
			{ at = 8614, op = "add", v = function(s, e) return s.patk_smatk_inc end },
		},
	},
})

D("CONCENTRATE", {
	fields = { val1 = "lv", val2 = "stat_pct", val3 = "agi_card_bonus", val4 = "dex_card_bonus" },
	contributes = {
		agi = {
			{ at = 6858, op = "pct_over", when = function(s, e) return e.sc.QUAGMIRE == nil end, v = function(s, e) return s.agi_card_bonus end, v2 = function(s, e) return s.stat_pct end },
		},
		dex = {
			{ at = 7070, op = "pct_over", when = function(s, e) return e.sc.QUAGMIRE == nil end, v = function(s, e) return s.dex_card_bonus end, v2 = function(s, e) return s.stat_pct end },
		},
	},
})

D("CONCENTRATION", {
	fields = { val1 = "lv", val2 = "atk_pct", val3 = "hit_inc", val4 = "def_pct_dec" },
	contributes = {
		hit = {
			{ at = 7577, op = "add", v = function(s, e) return s.hit_inc end },
		},
		def = {
			{ at = 7822, op = "mul_pct", v = function(s, e) return -s.def_pct_dec end },
		},
		def2 = {
			{ at = 7906, op = "mul_pct", v = function(s, e) return -s.def_pct_dec end },
		},
	},
})

D("COOLER_OPTION", {
	fields = { val1 = "val1_unused", val2 = "matk_inc", val3 = "element_change", val4 = "val4_unused" },
})

D("CREATINGSTAR", {
	fields = { val1 = "val1_unused", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		speed = {
			{ at = 8152, op = "gmax", group = "slow", when = function(s, e) return not ((e.unit.type == "pc" and e.sc.HIDING ~= nil) and e.skill("RG_TUNNELDRIVE") > 0) and not ((e.unit.type == "pc" and e.sc.CHASEWALK ~= nil) and e.sc.CHASEWALK.speed_value < 0) end, v = 90 },
		},
	},
})

D("CROSSBOWCLAN", {
	fields = { val1 = "val1_unused", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		hp_fix = {
			{ at = 3216, op = "add", v = 30 },
		},
		sp_fix = {
			{ at = 3374, op = "add", v = 10 },
		},
		agi = {
			{ at = 6892, op = "add", v = 1 },
		},
		dex = {
			{ at = 7104, op = "add", v = 1 },
		},
	},
})

D("CRYSTAL_ARMOR_OPTION", {
	in_pre_db = false, -- not in db/pre-re/status.yml: the stat code knows it, a PRE server never starts it
	fields = { val1 = "val1_unused", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
})

D("CURSE", {
	fields = { val1 = "val1_unused", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		luk = {
			{ at = 7145, op = "final", v = 0 },
		},
		speed = {
			{ at = 8108, op = "gmax", group = "slow", when = function(s, e) return not ((e.unit.type == "pc" and e.sc.HIDING ~= nil) and e.skill("RG_TUNNELDRIVE") > 0) and not ((e.unit.type == "pc" and e.sc.CHASEWALK ~= nil) and e.sc.CHASEWALK.speed_value < 0) end, v = 300 },
		},
	},
})

D("CURSED_SOIL_OPTION", {
	fields = { val1 = "val1_unused", val2 = "maxhp_bonus_inc", val3 = "element_change", val4 = "val4_unused" },
	contributes = {
		hp_rate = {
			{ at = 3262, op = "add", v = function(s, e) return s.maxhp_bonus_inc end },
		},
	},
})

D("C_MARKER", {
	fields = { val1 = "val1_unused", val2 = "val2_unused", val3 = "flee_dec", val4 = "val4_unused" },
	contributes = {
		flee = {
			{ at = 7683, op = "add", v = function(s, e) return -s.flee_dec end },
		},
	},
})

D("DANCEWITHWUG", {
	fields = { val1 = "lv", val2 = "val2_unused", val3 = "aspd_rate_dec_arg", val4 = "fixed_cast_time" },
	contributes = {
		aspd_rate = {
			{ at = 8543, op = "add", v = function(s, e) return -(s.aspd_rate_dec_arg * 10) end },
		},
	},
})

D("DANCING", {
	fields = { val1 = "lv", val2 = "val2_unused", val3 = "tick_duration", val4 = "ensemble_flag" },
	contributes = {
		speed = {
			{ at = 8099, op = "gmax", group = "slow", when = function(s, e) return not ((e.unit.type == "pc" and e.sc.HIDING ~= nil) and e.skill("RG_TUNNELDRIVE") > 0) and not ((e.unit.type == "pc" and e.sc.CHASEWALK ~= nil) and e.sc.CHASEWALK.speed_value < 0) and e.sc.LONGING == nil and e.unit.type == "pc" end, v = function(s, e) return 500 - ((40 + (10 * ((e.sc.SPIRIT ~= nil and e.sc.SPIRIT.link_type == K.SL_BARDDANCER and 1 or 0)))) * e.skill(e.unit.sex ~= 0 and "BA_MUSICALLESSON" or "DC_DANCINGLESSON")) end },
		},
	},
})

D("DECREASEAGI", {
	fields = { val1 = "lv", val2 = "agi_dec", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		agi = {
			{ at = 6870, op = "add", v = function(s, e) return -s.agi_dec end },
		},
		speed = {
			{ at = 8102, op = "gmax", group = "slow", when = function(s, e) return not ((e.unit.type == "pc" and e.sc.HIDING ~= nil) and e.skill("RG_TUNNELDRIVE") > 0) and not ((e.unit.type == "pc" and e.sc.CHASEWALK ~= nil) and e.sc.CHASEWALK.speed_value < 0) end, v = 25 },
		},
	},
})

D("DEFENCE", {
	fields = { val1 = "lv", val2 = "stat_def_inc", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		vit = {
			{ at = 6959, op = "add", when = function(s, e) return e.unit.type == "pc" end, v = function(s, e) return s.stat_def_inc end },
		},
		def = {
			{ at = 7802, op = "add", when = function(s, e) return e.unit.type == "hom" end, v = function(s, e) return s.stat_def_inc end },
		},
	},
})

D("DEFENDER", {
	fields = { val1 = "lv", val2 = "damage_reduction", val3 = "unused", val4 = "aspd_rate_inc" },
	contributes = {
		aspd_rate = {
			{ at = 8515, op = "add", v = function(s, e) return s.aspd_rate_inc end },
		},
	},
})

D("DEFSET", {
	fields = { val1 = "def_set", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		def = {
			{ at = 7793, op = "final", v = function(s, e) return s.def_set end },
		},
		def2 = {
			{ at = 7892, op = "final", v = function(s, e) return s.def_set end },
		},
	},
})

D("DELUGE", {
	fields = { val1 = "val1_unused", val2 = "maxhp_bonus_inc", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		hp_rate = {
			{ at = 3236, op = "add", v = function(s, e) return s.maxhp_bonus_inc end },
		},
	},
})

D("DONTFORGETME", {
	fields = { val1 = "val1_unused", val2 = "aspd_rate_inc", val3 = "speed_slow", val4 = "val4_unused" },
	contributes = {
		speed = {
			{ at = 8106, op = "gmax", group = "slow", when = function(s, e) return not ((e.unit.type == "pc" and e.sc.HIDING ~= nil) and e.skill("RG_TUNNELDRIVE") > 0) and not ((e.unit.type == "pc" and e.sc.CHASEWALK ~= nil) and e.sc.CHASEWALK.speed_value < 0) end, v = function(s, e) return s.speed_slow end },
		},
		aspd_rate = {
			{ at = 8504, op = "add", when = function(s, e) return e.unit.type == "pc" end, v = function(s, e) return s.aspd_rate_inc end },
		},
	},
})

D("DORAM_FLEE2", {
	fields = { val1 = "flee2_inc", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		flee2 = {
			{ at = 7757, op = "add", v = function(s, e) return s.flee2_inc end },
		},
	},
})

D("DORAM_MATK", {
	fields = { val1 = "matk_inc", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
})

D("DORAM_WALKSPEED", {
	fields = { val1 = "speed_haste", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		speed = {
			{ at = 8205, op = "gmax", group = "haste", v = function(s, e) return s.speed_haste end },
		},
	},
})

D("DPOISON", {
	fields = { val1 = "val1_unused", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		def = {
			{ at = 7818, op = "scale", when = function(s, e) return (e.sc.POISON ~= nil or e.sc.DPOISON ~= nil) and e.unit.type ~= "pc" end, v = 75, v2 = 100, also = {"POISON"} },
		},
		def2 = {
			{ at = 7909, op = "scale", when = function(s, e) return (e.sc.POISON ~= nil or e.sc.DPOISON ~= nil) end, v = 75, v2 = 100, also = {"POISON"} },
		},
	},
})

D("DRUMBATTLE", {
	fields = { val1 = "val1_unused", val2 = "watk_inc", val3 = "def_inc", val4 = "val4_unused" },
	contributes = {
		watk = {
			{ at = 7367, op = "add", v = function(s, e) return s.watk_inc end },
		},
		def = {
			{ at = 7796, op = "add", v = function(s, e) return s.def_inc end },
		},
	},
})

D("D_MACHINE", {
	in_pre_db = false, -- not in db/pre-re/status.yml: the stat code knows it, a PRE server never starts it
	fields = { val1 = "lv", val2 = "def_inc", val3 = "res_inc", val4 = "val4_unused" },
	contributes = {
		def = {
			{ at = 7860, op = "add", v = function(s, e) return s.def_inc end },
		},
		res = {
			{ at = 8653, op = "add", v = function(s, e) return s.res_inc end },
		},
	},
})

D("EARTHWEAPON", {
	fields = { val1 = "val1_unused", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
})

D("EARTH_INSIGNIA", {
	fields = { val1 = "level", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		hp_fix = {
			{ at = 3202, op = "add", when = function(s, e) return s.level == 2 end, v = 500 },
		},
		sp_fix = {
			{ at = 3366, op = "add", when = function(s, e) return s.level == 3 end, v = 50 },
		},
		def = {
			{ at = 7806, op = "add", when = function(s, e) return s.level == 2 end, v = 50 },
		},
		mdef = {
			{ at = 7966, op = "add", when = function(s, e) return s.level == 3 end, v = 50 },
		},
		atk_element = {
			{ at = 8868, op = "final", when = function(s, e) return (e.sc.EARTHWEAPON ~= nil or ((e.sc.EARTH_INSIGNIA ~= nil and e.sc.EARTH_INSIGNIA.level == 2))) end, v = function(s, e) return K.ELE_EARTH end, also = {"EARTHWEAPON"} },
		},
	},
})

D("ECHOSONG", {
	fields = { val1 = "lv", val2 = "val2_unused", val3 = "def_inc", val4 = "val4_unused" },
	contributes = {
		def = {
			{ at = 7842, op = "add", v = function(s, e) return s.def_inc end },
		},
	},
})

D("ELEMENTALCHANGE", {
	fields = { val1 = "element_lv", val2 = "element", val3 = "unused", val4 = "val4_unused" },
	contributes = {
		element = {
			{ at = 8810, op = "final", v = function(s, e) return s.element end },
		},
		element_lv = {
			{ at = 8838, op = "final", v = function(s, e) return s.element_lv end },
		},
	},
})

D("EMERGENCY_MOVE", {
	in_pre_db = false, -- not in db/pre-re/status.yml: the stat code knows it, a PRE server never starts it
	fields = { val1 = "val1_unused", val2 = "speed_haste", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		speed = {
			{ at = 8209, op = "gmax", group = "haste", v = function(s, e) return s.speed_haste end },
		},
	},
})

D("ENCHANTARMS", {
	fields = { val1 = "element", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		atk_element = {
			{ at = 8862, op = "final", v = function(s, e) return s.element end },
		},
	},
})

D("ENCPOISON", {
	fields = { val1 = "lv", val2 = "poison_chance", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		atk_element = {
			{ at = 8876, op = "final", v = function(s, e) return K.ELE_POISON end },
		},
	},
})

D("ENDURE", {
	fields = { val1 = "lv", val2 = "hit_count", val3 = "permanent_flag", val4 = "card_flag" },
	contributes = {
		mdef = {
			{ at = 7968, op = "add", when = function(s, e) return not (s.permanent_flag ~= 0) end, v = function(s, e) return ((s.card_flag == 0) and s.lv or 1) end },
		},
	},
})

D("EPICLESIS", {
	fields = { val1 = "lv", val2 = "maxhp_bonus_inc", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		hp_rate = {
			{ at = 3242, op = "add", v = function(s, e) return s.maxhp_bonus_inc end },
		},
	},
})

D("EQC", {
	fields = { val1 = "lv", val2 = "def2_pct_dec", val3 = "maxhp_bonus_dec", val4 = "val4_unused" },
	contributes = {
		hp_rate = {
			{ at = 3286, op = "add", v = function(s, e) return -s.maxhp_bonus_dec end },
		},
		def2 = {
			{ at = 7926, op = "mul_pct", v = function(s, e) return -s.def2_pct_dec end },
		},
	},
})

D("ETERNALCHAOS", {
	fields = { val1 = "val1_unused", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		def2 = {
			{ at = 7890, op = "final", v = 0 },
		},
	},
})

D("EXPLOSIONSPIRITS", {
	fields = { val1 = "lv", val2 = "cri_inc", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		cri = {
			{ at = 7531, op = "add", v = function(s, e) return s.cri_inc end },
		},
	},
})

D("EYES_OF_STORM_OPTION", {
	in_pre_db = false, -- not in db/pre-re/status.yml: the stat code knows it, a PRE server never starts it
	fields = { val1 = "val1_unused", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		element = {
			{ at = 8802, op = "final", v = function(s, e) return K.ELE_WIND end },
		},
	},
})

D("FEAR", {
	fields = { val1 = "val1_unused", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		hit = {
			{ at = 7597, op = "mul_pct", v = -20 },
		},
		flee = {
			{ at = 7705, op = "mul_pct", v = -20 },
		},
	},
})

D("FIGHTINGSPIRIT", {
	fields = { val1 = "watk_inc", val2 = "fix_aspd_dec", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		watk = {
			{ at = 7393, op = "add", v = function(s, e) return s.watk_inc end },
		},
		fix_aspd = {
			{ at = 8407, op = "add", v = function(s, e) return -s.fix_aspd_dec end },
		},
	},
})

D("FIREWEAPON", {
	fields = { val1 = "val1_unused", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
})

D("FIRE_INSIGNIA", {
	fields = { val1 = "level", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		watk = {
			{ at = 7407, op = "add", when = function(s, e) return s.level == 2 end, v = 50 },
			{ at = 7412, op = "mul_pct", when = function(s, e) return (((((e.sc.FIRE_INSIGNIA ~= nil and e.sc.FIRE_INSIGNIA.level == 2)) or ((e.sc.WATER_INSIGNIA ~= nil and e.sc.WATER_INSIGNIA.level == 2))) or ((e.sc.WIND_INSIGNIA ~= nil and e.sc.WIND_INSIGNIA.level == 2))) or ((e.sc.EARTH_INSIGNIA ~= nil and e.sc.EARTH_INSIGNIA.level == 2))) end, v = 10, also = {"WIND_INSIGNIA", "EARTH_INSIGNIA", "WATER_INSIGNIA"} },
		},
		atk_element = {
			{ at = 8871, op = "final", when = function(s, e) return (e.sc.FIREWEAPON ~= nil or ((e.sc.FIRE_INSIGNIA ~= nil and e.sc.FIRE_INSIGNIA.level == 2))) end, v = function(s, e) return K.ELE_FIRE end, also = {"FIREWEAPON"} },
		},
	},
})

D("FIRM_FAITH", {
	in_pre_db = false, -- not in db/pre-re/status.yml: the stat code knows it, a PRE server never starts it
	fields = { val1 = "lv", val2 = "maxhp_bonus_inc", val3 = "res_inc", val4 = "val4_unused" },
	contributes = {
		hp_rate = {
			{ at = 3276, op = "add", v = function(s, e) return s.maxhp_bonus_inc end },
		},
		res = {
			{ at = 8651, op = "add", v = function(s, e) return s.res_inc end },
		},
	},
})

D("FLAMEARMOR_OPTION", {
	in_pre_db = false, -- not in db/pre-re/status.yml: the stat code knows it, a PRE server never starts it
	fields = { val1 = "val1_unused", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		element = {
			{ at = 8800, op = "final", v = function(s, e) return K.ELE_FIRE end },
		},
	},
})

D("FLASHCOMBO", {
	fields = { val1 = "lv", val2 = "watk_inc", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		watk = {
			{ at = 7428, op = "add", v = function(s, e) return s.watk_inc end },
		},
	},
})

D("FLEET", {
	fields = { val1 = "lv", val2 = "aspd_rate_quicken", val3 = "atk_pct_change", val4 = "val4_unused" },
	contributes = {
		aspd_rate = {
			{ at = 8468, op = "gmax", group = "quicken", v = function(s, e) return s.aspd_rate_quicken end },
		},
	},
})

D("FLING", {
	fields = { val1 = "lv", val2 = "def_pct_dec", val3 = "def2_pct_dec", val4 = "val4_unused" },
	contributes = {
		def = {
			{ at = 7830, op = "mul_pct", v = function(s, e) return -((s.def_pct_dec)) end },
		},
		def2 = {
			{ at = 7918, op = "mul_pct", v = function(s, e) return -((s.def2_pct_dec)) end },
		},
	},
})

D("FORCEOFVANGUARD", {
	fields = { val1 = "lv", val2 = "chance", val3 = "max_rage", val4 = "val4_unused" },
	contributes = {
		hp_rate = {
			{ at = 3248, op = "add", v = function(s, e) return (3 * s.lv) end },
		},
	},
})

D("FORTUNE", {
	fields = { val1 = "val1_unused", val2 = "cri_inc", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		cri = {
			{ at = 7533, op = "add", v = function(s, e) return s.cri_inc end },
		},
	},
})

D("FREEZE", {
	fields = { val1 = "val1_unused", val2 = "val2_unused", val3 = "petrify_time", val4 = "val4_unused" },
	contributes = {
		def = {
			{ at = 7816, op = "div", v = 2 },
		},
		mdef = {
			{ at = 7974, op = "mul_pct", v = 25 },
		},
		element = {
			{ at = 8796, op = "final", when = function(s, e) return (e.sc.FREEZE ~= nil or e.sc.CRYSTAL_ARMOR_OPTION ~= nil) end, v = function(s, e) return K.ELE_WATER end, also = {"CRYSTAL_ARMOR_OPTION"} },
		},
		element_lv = {
			{ at = 8830, op = "final", v = 1 },
		},
	},
})

D("FREEZING", {
	fields = { val1 = "val1_unused", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		def = {
			{ at = 7832, op = "mul_pct", v = function(s, e) return -(((e.unit.type == "pc" and 30 or 10))) end },
		},
		speed = {
			{ at = 8128, op = "gmax", group = "slow", when = function(s, e) return not ((e.unit.type == "pc" and e.sc.HIDING ~= nil) and e.skill("RG_TUNNELDRIVE") > 0) and not ((e.unit.type == "pc" and e.sc.CHASEWALK ~= nil) and e.sc.CHASEWALK.speed_value < 0) end, v = 30 },
		},
		aspd_rate = {
			{ at = 8529, op = "add", v = 300 },
		},
	},
})

D("FRIGG_SONG", {
	fields = { val1 = "lv", val2 = "maxhp_bonus_inc", val3 = "healing", val4 = "val4_unused" },
	contributes = {
		hp_rate = {
			{ at = 3244, op = "add", v = function(s, e) return s.maxhp_bonus_inc end },
		},
	},
})

D("FULL_THROTTLE", {
	fields = { val1 = "lv", val2 = "val2_unused", val3 = "stat_pct_inc", val4 = "val4_unused" },
	contributes = {
		str = {
			{ at = 6825, op = "mul_pct", v = function(s, e) return s.stat_pct_inc end },
		},
		agi = {
			{ at = 6896, op = "mul_pct", v = function(s, e) return s.stat_pct_inc end },
		},
		vit = {
			{ at = 6957, op = "mul_pct", v = function(s, e) return s.stat_pct_inc end },
		},
		int = {
			{ at = 7030, op = "mul_pct", v = function(s, e) return s.stat_pct_inc end },
		},
		dex = {
			{ at = 7112, op = "mul_pct", v = function(s, e) return s.stat_pct_inc end },
		},
		luk = {
			{ at = 7173, op = "mul_pct", v = function(s, e) return s.stat_pct_inc end },
		},
		speed = {
			{ at = 8201, op = "gmax", group = "haste", v = 25 },
		},
	},
})

D("FUSION", {
	fields = { val1 = "val1_unused", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		speed = {
			{ at = 8071, op = "gset", group = "mount", v = 25 },
		},
	},
})

D("_AUTOSHADOWSPELL", {
	fields = { val1 = "val1_unused", val2 = "val2_unused", val3 = "val3_unused", val4 = "matk_inc" },
})

D("_BODYPAINT", {
	fields = { val1 = "lv", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		aspd_rate = {
			{ at = 8535, op = "add", v = function(s, e) return 50 * s.lv end },
		},
	},
})

D("_ENERVATION", {
	fields = { val1 = "lv", val2 = "batk_watk_pct_dec", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		batk = {
			{ at = 7336, op = "mul_pct", v = function(s, e) return -s.batk_watk_pct_dec end },
		},
		watk = {
			{ at = 7401, op = "mul_pct", v = function(s, e) return -s.batk_watk_pct_dec end },
		},
	},
})

