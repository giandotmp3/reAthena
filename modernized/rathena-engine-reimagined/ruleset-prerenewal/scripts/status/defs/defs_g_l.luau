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

D("GATLINGFEVER", {
	fields = { val1 = "lv", val2 = "aspd_rate_quicken", val3 = "batk_inc", val4 = "flee_dec" },
	contributes = {
		batk = {
			{ at = 7325, op = "add", v = function(s, e) return s.batk_inc end },
		},
		flee = {
			{ at = 7671, op = "add", v = function(s, e) return -s.flee_dec end },
		},
		speed = {
			{ at = 8122, op = "gmax", group = "slow", when = function(s, e) return not ((e.unit.type == "pc" and e.sc.HIDING ~= nil) and e.skill("RG_TUNNELDRIVE") > 0) and not ((e.unit.type == "pc" and e.sc.CHASEWALK ~= nil) and e.sc.CHASEWALK.speed_value < 0) end, v = 100 },
		},
		aspd_rate = {
			{ at = 8464, op = "gmax", group = "quicken", v = function(s, e) return s.aspd_rate_quicken end },
		},
	},
})

D("GEF_NOCTURN", {
	in_pre_db = false, -- not in db/pre-re/status.yml: the stat code knows it, a PRE server never starts it
	fields = { val1 = "lv", val2 = "mres_dec", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		mres = {
			{ at = 8685, op = "add", v = function(s, e) return -s.mres_dec end },
		},
	},
})

D("GHOSTWEAPON", {
	fields = { val1 = "val1_unused", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		atk_element = {
			{ at = 8882, op = "final", when = function(s, e) return (e.sc.GHOSTWEAPON ~= nil or e.sc._INVISIBILITY ~= nil) end, v = function(s, e) return K.ELE_GHOST end, also = {"_INVISIBILITY"} },
		},
	},
})

D("GIANTGROWTH", {
	fields = { val1 = "val1_unused", val2 = "stat_inc", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		str = {
			{ at = 6811, op = "add", v = function(s, e) return s.stat_inc end },
		},
	},
})

D("GLOOMYDAY", {
	fields = { val1 = "lv", val2 = "flee_pct_dec", val3 = "aspd_rate_inc_arg", val4 = "speed_cond" },
	contributes = {
		flee = {
			{ at = 7713, op = "mul_pct", v = function(s, e) return -s.flee_pct_dec end },
		},
		speed = {
			{ at = 8104, op = "gmax", group = "slow", when = function(s, e) return not ((e.unit.type == "pc" and e.sc.HIDING ~= nil) and e.skill("RG_TUNNELDRIVE") > 0) and not ((e.unit.type == "pc" and e.sc.CHASEWALK ~= nil) and e.sc.CHASEWALK.speed_value < 0) and ((e.sc.QUAGMIRE ~= nil or e.sc.HALLUCINATIONWALK_POSTDELAY ~= nil) or ((e.sc.GLOOMYDAY ~= nil and e.sc.GLOOMYDAY.speed_cond ~= 0))) end, v = 50, also = {"QUAGMIRE", "HALLUCINATIONWALK_POSTDELAY"} },
		},
		aspd_rate = {
			{ at = 8545, op = "add", v = function(s, e) return s.aspd_rate_inc_arg * 10 end },
		},
	},
})

D("GLORIA", {
	fields = { val1 = "val1_unused", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		luk = {
			{ at = 7151, op = "add", v = 30 },
		},
	},
})

D("GLORYWOUNDS", {
	fields = { val1 = "stat_inc", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		vit = {
			{ at = 6935, op = "add", v = function(s, e) return s.stat_inc end },
		},
	},
})

D("GN_CARTBOOST", {
	fields = { val1 = "val1_unused", val2 = "speed_haste", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		speed = {
			{ at = 8195, op = "gmax", group = "haste", v = function(s, e) return s.speed_haste end },
		},
	},
})

D("GOLDENE_FERSE", {
	fields = { val1 = "lv", val2 = "flee_pct_inc", val3 = "aspd_rate_dec_arg", val4 = "holy_chance" },
	contributes = {
		flee = {
			{ at = 7725, op = "mul_pct", v = function(s, e) return s.flee_pct_inc end },
		},
		aspd_rate = {
			{ at = 8553, op = "add", v = function(s, e) return -(s.aspd_rate_dec_arg * 10) end },
		},
	},
})

D("GOLDENE_TONE", {
	in_pre_db = false, -- not in db/pre-re/status.yml: the stat code knows it, a PRE server never starts it
	fields = { val1 = "val1_unused", val2 = "res_mres_inc", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		res = {
			{ at = 8657, op = "add", v = function(s, e) return s.res_mres_inc end },
		},
		mres = {
			{ at = 8681, op = "add", v = function(s, e) return s.res_mres_inc end },
		},
	},
})

D("GOLDENMACECLAN", {
	fields = { val1 = "val1_unused", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		hp_fix = {
			{ at = 3214, op = "add", v = 30 },
		},
		sp_fix = {
			{ at = 3372, op = "add", v = 10 },
		},
		int = {
			{ at = 7026, op = "add", v = 1 },
		},
		luk = {
			{ at = 7169, op = "add", v = 1 },
		},
	},
})

D("GOSPEL", {
	fields = { val1 = "val1_unused", val2 = "val2_unused", val3 = "val3_unused", val4 = "target_type" },
	contributes = {
		speed = {
			{ at = 8118, op = "gmax", group = "slow", when = function(s, e) return not ((e.unit.type == "pc" and e.sc.HIDING ~= nil) and e.skill("RG_TUNNELDRIVE") > 0) and not ((e.unit.type == "pc" and e.sc.CHASEWALK ~= nil) and e.sc.CHASEWALK.speed_value < 0) and s.target_type == K.BCT_ENEMY end, v = 75 },
		},
		aspd_rate = {
			{ at = 8517, op = "add", when = function(s, e) return s.target_type == K.BCT_ENEMY end, v = 250 },
		},
	},
})

D("GRAVITATION", {
	fields = { val1 = "lv", val2 = "aspd_rate_inc", val3 = "target_type", val4 = "val4_unused" },
	contributes = {
		aspd_rate = {
			{ at = 8520, op = "add", v = function(s, e) return s.aspd_rate_inc end },
		},
	},
})

D("GROOMING", {
	fields = { val1 = "val1_unused", val2 = "flee_inc", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		flee = {
			{ at = 7733, op = "add", v = function(s, e) return s.flee_inc end },
		},
	},
})

D("GROUNDGRAVITY", {
	in_pre_db = false, -- not in db/pre-re/status.yml: the stat code knows it, a PRE server never starts it
	fields = { val1 = "val1_unused", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		speed = {
			{ at = 8156, op = "gmax", group = "slow", when = function(s, e) return not ((e.unit.type == "pc" and e.sc.HIDING ~= nil) and e.skill("RG_TUNNELDRIVE") > 0) and not ((e.unit.type == "pc" and e.sc.CHASEWALK ~= nil) and e.sc.CHASEWALK.speed_value < 0) end, v = 20 },
		},
	},
})

D("GT_CHANGE", {
	fields = { val1 = "lv", val2 = "watk_inc", val3 = "aspd_rate_dec_arg", val4 = "val4_unused" },
	contributes = {
		watk = {
			{ at = 7399, op = "add", v = function(s, e) return s.watk_inc end },
		},
		aspd_rate = {
			{ at = 8547, op = "add", v = function(s, e) return -(s.aspd_rate_dec_arg * 10) end },
		},
	},
})

D("GT_REVITALIZE", {
	fields = { val1 = "lv", val2 = "maxhp_bonus_inc", val3 = "hp_regen_inc", val4 = "def2_inc" },
	contributes = {
		hp_rate = {
			{ at = 3254, op = "add", v = function(s, e) return s.maxhp_bonus_inc end },
		},
	},
})

D("GUARD_STANCE", {
	in_pre_db = false, -- not in db/pre-re/status.yml: the stat code knows it, a PRE server never starts it
	fields = { val1 = "lv", val2 = "def_inc", val3 = "watk_dec", val4 = "val4_unused" },
	contributes = {
		watk = {
			{ at = 7440, op = "add", v = function(s, e) return -s.watk_dec end },
		},
		def = {
			{ at = 7864, op = "add", v = function(s, e) return s.def_inc end },
		},
	},
})

D("GUST_OPTION", {
	fields = { val1 = "val1_unused", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
})

D("HALLUCINATIONWALK", {
	fields = { val1 = "lv", val2 = "flee_inc", val3 = "mdef_evasion", val4 = "val4_unused" },
	contributes = {
		flee = {
			{ at = 7677, op = "add", v = function(s, e) return s.flee_inc end },
		},
	},
})

D("HALLUCINATIONWALK_POSTDELAY", {
	fields = { val1 = "val1_unused", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		aspd_rate = {
			{ at = 8531, op = "add", v = 500 },
		},
	},
})

D("HARMONIZE", {
	fields = { val1 = "lv", val2 = "stat_dec", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		str = {
			{ at = 6779, op = "add", stage = "override", v = function(s, e) return -s.stat_dec end },
			{ at = 6780, op = "stop" },
		},
		agi = {
			{ at = 6854, op = "add", stage = "override", v = function(s, e) return -s.stat_dec end },
			{ at = 6855, op = "stop" },
		},
		vit = {
			{ at = 6927, op = "add", stage = "override", v = function(s, e) return -s.stat_dec end },
			{ at = 6928, op = "stop" },
		},
		int = {
			{ at = 6988, op = "add", stage = "override", v = function(s, e) return -s.stat_dec end },
			{ at = 6989, op = "stop" },
		},
		dex = {
			{ at = 7066, op = "add", stage = "override", v = function(s, e) return -s.stat_dec end },
			{ at = 7067, op = "stop" },
		},
		luk = {
			{ at = 7141, op = "add", stage = "override", v = function(s, e) return -s.stat_dec end },
			{ at = 7142, op = "stop" },
		},
	},
})

D("HAWKEYES", {
	fields = { val1 = "stat_inc", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		dex = {
			{ at = 7076, op = "add", v = function(s, e) return s.stat_inc end },
		},
	},
})

D("HEATER_OPTION", {
	fields = { val1 = "val1_unused", val2 = "watk_inc", val3 = "element_change", val4 = "val4_unused" },
	contributes = {
		watk = {
			{ at = 7416, op = "add", v = function(s, e) return s.watk_inc end },
		},
	},
})

D("HEAT_BARREL", {
	fields = { val1 = "lv", val2 = "fixed_casttime", val3 = "atk", val4 = "hit_dec" },
	contributes = {
		hit = {
			{ at = 7593, op = "add", v = function(s, e) return -s.hit_dec end },
		},
		fix_aspd = {
			{ at = 8411, op = "add", v = function(s, e) return -(s.lv * 10) end },
		},
	},
})

D("HIDDEN_CARD", {
	in_pre_db = false, -- not in db/pre-re/status.yml: the stat code knows it, a PRE server never starts it
	fields = { val1 = "lv", val2 = "patk_inc", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		patk = {
			{ at = 8588, op = "add", v = function(s, e) return s.patk_inc end },
		},
	},
})

D("HIDING", {
	fields = { val1 = "lv", val2 = "val2_unused", val3 = "unused", val4 = "sp_interval" },
	contributes = {
		speed = {
			{ at = 8084, op = "gset", group = "slow_force", when = function(s, e) return e.unit.type == "pc" and e.skill("RG_TUNNELDRIVE") > 0 end, v = function(s, e) return 120 - (6 * e.skill("RG_TUNNELDRIVE")) end },
		},
	},
})

D("HISS", {
	fields = { val1 = "val1_unused", val2 = "flee2_inc_arg", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		flee2 = {
			{ at = 7755, op = "add", v = function(s, e) return s.flee2_inc_arg * 10 end },
		},
	},
})

D("HOVERING", {
	fields = { val1 = "val1_unused", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		speed = {
			{ at = 8193, op = "gmax", group = "haste", v = 10 },
		},
	},
})

D("HUMMING", {
	fields = { val1 = "val1_unused", val2 = "hit_inc", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		hit = {
			{ at = 7575, op = "add", v = function(s, e) return s.hit_inc end },
		},
	},
})

D("ILLUSIONDOPING", {
	fields = { val1 = "val1_unused", val2 = "hit_dec", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		hit = {
			{ at = 7603, op = "add", v = function(s, e) return -s.hit_dec end },
		},
	},
})

D("IMPOSITIO", {
	fields = { val1 = "lv", val2 = "watk_inc", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		watk = {
			{ at = 7370, op = "add", v = function(s, e) return s.watk_inc end },
		},
	},
})

D("INCALLSTATUS", {
	fields = { val1 = "stat_inc", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		str = {
			{ at = 6783, op = "add", v = function(s, e) return s.stat_inc end },
		},
		agi = {
			{ at = 6860, op = "add", v = function(s, e) return s.stat_inc end },
		},
		vit = {
			{ at = 6931, op = "add", v = function(s, e) return s.stat_inc end },
		},
		int = {
			{ at = 6992, op = "add", v = function(s, e) return s.stat_inc end },
		},
		dex = {
			{ at = 7072, op = "add", v = function(s, e) return s.stat_inc end },
		},
		luk = {
			{ at = 7147, op = "add", v = function(s, e) return s.stat_inc end },
		},
	},
})

D("INCASPDRATE", {
	fields = { val1 = "lv", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		aspd_rate = {
			{ at = 8551, op = "add", v = function(s, e) return -(s.lv * 10) end },
		},
	},
})

D("INCDEFRATE", {
	fields = { val1 = "def_pct_inc", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		def = {
			{ at = 7804, op = "mul_pct", v = function(s, e) return s.def_pct_inc end },
		},
	},
})

D("INCFLEE", {
	fields = { val1 = "flee_inc", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		flee = {
			{ at = 7653, op = "add", v = function(s, e) return s.flee_inc end },
		},
	},
})

D("INCFLEERATE", {
	fields = { val1 = "flee_pct_inc", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		flee = {
			{ at = 7695, op = "mul_pct", v = function(s, e) return s.flee_pct_inc end },
		},
	},
})

D("INCHIT", {
	fields = { val1 = "hit_inc", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		hit = {
			{ at = 7571, op = "add", v = function(s, e) return s.hit_inc end },
		},
	},
})

D("INCHITRATE", {
	fields = { val1 = "hit_pct_inc", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		hit = {
			{ at = 7587, op = "mul_pct", v = function(s, e) return s.hit_pct_inc end },
		},
	},
})

D("INCMHP", {
	fields = { val1 = "maxhp_bonus_inc", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		hp_fix = {
			{ at = 3200, op = "add", v = function(s, e) return s.maxhp_bonus_inc end },
		},
	},
})

D("INCMHPRATE", {
	fields = { val1 = "maxhp_bonus_inc", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		hp_rate = {
			{ at = 3232, op = "add", v = function(s, e) return s.maxhp_bonus_inc end },
		},
	},
})

D("INCMSP", {
	fields = { val1 = "maxsp_bonus_inc", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		sp_fix = {
			{ at = 3364, op = "add", v = function(s, e) return s.maxsp_bonus_inc end },
		},
	},
})

D("INCMSPRATE", {
	fields = { val1 = "maxsp_bonus_inc", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		sp_rate = {
			{ at = 3401, op = "add", v = function(s, e) return s.maxsp_bonus_inc end },
		},
	},
})

D("INCREASEAGI", {
	fields = { val1 = "lv", val2 = "agi_inc", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		agi = {
			{ at = 6866, op = "add", v = function(s, e) return s.agi_inc end },
		},
		speed = {
			{ at = 8171, op = "gmax", group = "haste", v = 25 },
		},
	},
})

D("INCREASING", {
	fields = { val1 = "val1_unused", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		agi = {
			{ at = 6868, op = "add", v = 4 },
		},
		dex = {
			{ at = 7088, op = "add", v = 4 },
		},
		hit = {
			{ at = 7583, op = "add", v = 20 },
		},
	},
})

D("INFRAREDSCAN", {
	fields = { val1 = "val1_unused", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		flee = {
			{ at = 7709, op = "mul_pct", v = -30 },
		},
	},
})

D("INSPIRATION", {
	fields = { val1 = "lv", val2 = "atk_inc", val3 = "stat_inc", val4 = "val4_unused" },
	contributes = {
		hp_rate = {
			{ at = 3250, op = "add", v = function(s, e) return (4 * s.lv) end },
		},
		str = {
			{ at = 6815, op = "add", v = function(s, e) return s.stat_inc end },
		},
		agi = {
			{ at = 6886, op = "add", v = function(s, e) return s.stat_inc end },
		},
		vit = {
			{ at = 6945, op = "add", v = function(s, e) return s.stat_inc end },
		},
		int = {
			{ at = 7014, op = "add", v = function(s, e) return s.stat_inc end },
		},
		dex = {
			{ at = 7096, op = "add", v = function(s, e) return s.stat_inc end },
		},
		luk = {
			{ at = 7159, op = "add", v = function(s, e) return s.stat_inc end },
		},
		watk = {
			{ at = 7397, op = "add", v = function(s, e) return s.atk_inc end },
		},
		hit = {
			{ at = 7579, op = "add", v = function(s, e) return 12 * s.lv end },
		},
	},
})

D("INTENSIVE_AIM", {
	in_pre_db = false, -- not in db/pre-re/status.yml: the stat code knows it, a PRE server never starts it
	fields = { val1 = "val1_unused", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		batk = {
			{ at = 7348, op = "add", v = 150 },
		},
		hit = {
			{ at = 7621, op = "add", v = 250 },
		},
		cri = {
			{ at = 7553, op = "add", v = 300 },
		},
	},
})

D("INVINCIBLE", {
	fields = { val1 = "val1_unused", val2 = "atk_pct_inc", val3 = "speed_haste", val4 = "aspd_rate_quicken" },
	contributes = {
		speed = {
			{ at = 8187, op = "gmax", group = "haste", v = function(s, e) return s.speed_haste end },
		},
		aspd_rate = {
			{ at = 8472, op = "gmax", group = "quicken", v = function(s, e) return s.aspd_rate_quicken end },
		},
	},
})

D("IZAYOI", {
	fields = { val1 = "lv", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
})

D("JAWAII_SERENADE", {
	in_pre_db = false, -- not in db/pre-re/status.yml: the stat code knows it, a PRE server never starts it
	fields = { val1 = "lv", val2 = "smatk_inc", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		speed = {
			{ at = 8211, op = "gmax", group = "haste", v = 25 },
		},
		smatk = {
			{ at = 8618, op = "add", v = function(s, e) return s.smatk_inc end },
		},
	},
})

D("JOINTBEAT", {
	fields = { val1 = "val1_unused", val2 = "break_flags", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		def2 = {
			{ at = 7915, op = "sub_pct2", v = function(s, e) return ((O.band(s.break_flags, K.BREAK_SHOULDER) ~= 0 and 50 or 0)) end, v2 = function(s, e) return ((O.band(s.break_flags, K.BREAK_WAIST) ~= 0 and 25 or 0)) end },
		},
		speed = {
			{ at = 8114, op = "gmax", group = "slow", when = function(s, e) return not ((e.unit.type == "pc" and e.sc.HIDING ~= nil) and e.skill("RG_TUNNELDRIVE") > 0) and not ((e.unit.type == "pc" and e.sc.CHASEWALK ~= nil) and e.sc.CHASEWALK.speed_value < 0) and O.band(s.break_flags, (O.bor(K.BREAK_ANKLE, K.BREAK_KNEE))) ~= 0 end, v = function(s, e) return ((O.band(s.break_flags, K.BREAK_ANKLE) ~= 0 and 50 or 0)) + ((O.band(s.break_flags, K.BREAK_KNEE) ~= 0 and 30 or 0)) end },
		},
		aspd_rate = {
			{ at = 8524, op = "add", when = function(s, e) return O.band(s.break_flags, K.BREAK_WRIST) ~= 0 end, v = 250 },
			{ at = 8526, op = "add", when = function(s, e) return O.band(s.break_flags, K.BREAK_KNEE) ~= 0 end, v = 100 },
		},
	},
})

D("JUMPINGCLAN", {
	fields = { val1 = "val1_unused", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		str = {
			{ at = 6823, op = "add", v = 1 },
		},
		agi = {
			{ at = 6894, op = "add", v = 1 },
		},
		vit = {
			{ at = 6953, op = "add", v = 1 },
		},
		int = {
			{ at = 7028, op = "add", v = 1 },
		},
		dex = {
			{ at = 7106, op = "add", v = 1 },
		},
		luk = {
			{ at = 7171, op = "add", v = 1 },
		},
	},
})

D("KEEPING", {
	fields = { val1 = "val1_unused", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		def = {
			{ at = 7781, op = "final", v = 90 },
		},
	},
})

D("KYOUGAKU", {
	fields = { val1 = "lv", val2 = "stat_dec", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		str = {
			{ at = 6819, op = "add", v = function(s, e) return -s.stat_dec end },
		},
		agi = {
			{ at = 6890, op = "add", v = function(s, e) return -s.stat_dec end },
		},
		vit = {
			{ at = 6949, op = "add", v = function(s, e) return -s.stat_dec end },
		},
		int = {
			{ at = 7022, op = "add", v = function(s, e) return -s.stat_dec end },
		},
		dex = {
			{ at = 7100, op = "add", v = function(s, e) return -s.stat_dec end },
		},
		luk = {
			{ at = 7163, op = "add", v = function(s, e) return -s.stat_dec end },
		},
	},
})

D("LAUDAAGNUS", {
	fields = { val1 = "lv", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		hp_rate = {
			{ at = 3266, op = "add", v = function(s, e) return 2 + (s.lv * 2) end },
		},
	},
})

D("LEADERSHIP", {
	fields = { val1 = "stat_inc", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		str = {
			{ at = 6789, op = "add", v = function(s, e) return s.stat_inc end },
		},
	},
})

D("LERADSDEW", {
	fields = { val1 = "lv", val2 = "val2_unused", val3 = "maxhp_bonus_inc", val4 = "val4_unused" },
	contributes = {
		hp_rate = {
			{ at = 3246, op = "add", v = function(s, e) return s.maxhp_bonus_inc end },
		},
	},
})

D("LONGING", {
	fields = { val1 = "lv", val2 = "aspd_rate_inc", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		speed = {
			{ at = 8095, op = "gmax", group = "slow", when = function(s, e) return not ((e.unit.type == "pc" and e.sc.HIDING ~= nil) and e.skill("RG_TUNNELDRIVE") > 0) and not ((e.unit.type == "pc" and e.sc.CHASEWALK ~= nil) and e.sc.CHASEWALK.speed_value < 0) end, v = function(s, e) return 50 - (10 * s.lv) end },
		},
		aspd_rate = {
			{ at = 8510, op = "add", v = function(s, e) return s.aspd_rate_inc end },
		},
	},
})

D("LOUD", {
	fields = { val1 = "val1_unused", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		str = {
			{ at = 6791, op = "add", v = 4 },
		},
	},
})

D("LUNARSTANCE", {
	fields = { val1 = "lv", val2 = "maxhp_bonus_inc", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		hp_rate = {
			{ at = 3272, op = "add", v = function(s, e) return s.maxhp_bonus_inc end },
		},
	},
})

D("LUXANIMA", {
	fields = { val1 = "val1_unused", val2 = "storm_blast_chance", val3 = "maxhp_bonus_maxsp_bonus_inc", val4 = "val4_unused" },
	contributes = {
		hp_rate = {
			{ at = 3274, op = "add", v = function(s, e) return s.maxhp_bonus_maxsp_bonus_inc end },
		},
		sp_rate = {
			{ at = 3409, op = "add", v = function(s, e) return s.maxhp_bonus_maxsp_bonus_inc end },
		},
	},
})

D("_GROOMY", {
	fields = { val1 = "lv", val2 = "aspd_rate_inc_arg", val3 = "hit_pct_dec", val4 = "val4_unused" },
	contributes = {
		hit = {
			{ at = 7595, op = "mul_pct", v = function(s, e) return -s.hit_pct_dec end },
		},
		aspd_rate = {
			{ at = 8539, op = "add", v = function(s, e) return s.aspd_rate_inc_arg * 10 end },
		},
	},
})

D("_INVISIBILITY", {
	fields = { val1 = "lv", val2 = "aspd_rate_inc_arg", val3 = "cri_inc_arg", val4 = "val4_unused" },
	contributes = {
		cri = {
			{ at = 7545, op = "add", v = function(s, e) return s.cri_inc_arg * 10 end },
		},
		aspd_rate = {
			{ at = 8537, op = "add", v = function(s, e) return s.aspd_rate_inc_arg * 10 end },
		},
		element_lv = {
			{ at = 8842, op = "final", v = 1 },
		},
	},
})

D("_LAZINESS", {
	fields = { val1 = "lv", val2 = "cast_inc", val3 = "flee_pct_dec", val4 = "val4_unused" },
	contributes = {
		flee = {
			{ at = 7711, op = "mul_pct", v = function(s, e) return -s.flee_pct_dec end },
		},
		speed = {
			{ at = 8136, op = "gmax", group = "slow", when = function(s, e) return not ((e.unit.type == "pc" and e.sc.HIDING ~= nil) and e.skill("RG_TUNNELDRIVE") > 0) and not ((e.unit.type == "pc" and e.sc.CHASEWALK ~= nil) and e.sc.CHASEWALK.speed_value < 0) end, v = 25 },
		},
	},
})

