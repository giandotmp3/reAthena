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

D("MADNESSCANCEL", {
	fields = { val1 = "val1_unused", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		batk = {
			{ at = 7327, op = "add", v = 100 },
		},
		aspd_rate = {
			{ at = 8495, op = "add", when = function(s, e) return e.sc.BERSERK == nil end, v = -200 },
		},
	},
})

D("MAGICPOWER", {
	fields = { val1 = "lv", val2 = "uses", val3 = "matk_pct", val4 = "active" },
})

D("MANDRAGORA", {
	fields = { val1 = "lv", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		int = {
			{ at = 7018, op = "add", v = function(s, e) return -(4 * s.lv) end },
		},
	},
})

D("MARINE_FESTIVAL", {
	in_pre_db = false, -- not in db/pre-re/status.yml: the stat code knows it, a PRE server never starts it
	fields = { val1 = "lv", val2 = "pow_con_crt_inc", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		pow = {
			{ at = 7204, op = "add", v = function(s, e) return s.pow_con_crt_inc end },
		},
		con = {
			{ at = 7284, op = "add", v = function(s, e) return s.pow_con_crt_inc end },
		},
		crt = {
			{ at = 7304, op = "add", v = function(s, e) return s.pow_con_crt_inc end },
		},
	},
})

D("MARIONETTE", {
	fields = { val1 = "val1_unused", val2 = "val2_unused", val3 = "str_agi_vit_packed", val4 = "int_dex_luk_packed" },
	contributes = {
		hp_fix = {
			{ at = 3208, op = "add", v = -1000 },
		},
		str = {
			{ at = 6805, op = "add", v = function(s, e) return -(O.band((O.sar((s.str_agi_vit_packed), 16)), 255)) end },
		},
		agi = {
			{ at = 6876, op = "add", v = function(s, e) return -(O.band((O.sar((s.str_agi_vit_packed), 8)), 255)) end },
		},
		vit = {
			{ at = 6939, op = "add", v = function(s, e) return -(O.band(s.str_agi_vit_packed, 255)) end },
		},
		int = {
			{ at = 7008, op = "add", v = function(s, e) return -(O.band((O.sar((s.int_dex_luk_packed), 16)), 255)) end },
		},
		dex = {
			{ at = 7090, op = "add", v = function(s, e) return -(O.band((O.sar((s.int_dex_luk_packed), 8)), 255)) end },
		},
		luk = {
			{ at = 7153, op = "add", v = function(s, e) return -(O.band(s.int_dex_luk_packed, 255)) end },
		},
	},
})

D("MARIONETTE2", {
	fields = { val1 = "val1_unused", val2 = "val2_unused", val3 = "str_agi_vit_packed", val4 = "int_dex_luk_packed" },
	contributes = {
		str = {
			{ at = 6807, op = "add", v = function(s, e) return O.band((O.sar((s.str_agi_vit_packed), 16)), 255) end },
		},
		agi = {
			{ at = 6878, op = "add", v = function(s, e) return O.band((O.sar((s.str_agi_vit_packed), 8)), 255) end },
		},
		vit = {
			{ at = 6941, op = "add", v = function(s, e) return O.band(s.str_agi_vit_packed, 255) end },
		},
		int = {
			{ at = 7010, op = "add", v = function(s, e) return O.band((O.sar((s.int_dex_luk_packed), 16)), 255) end },
		},
		dex = {
			{ at = 7092, op = "add", v = function(s, e) return O.band((O.sar((s.int_dex_luk_packed), 8)), 255) end },
		},
		luk = {
			{ at = 7155, op = "add", v = function(s, e) return O.band(s.int_dex_luk_packed, 255) end },
		},
	},
})

D("MARSHOFABYSS", {
	fields = { val1 = "lv", val2 = "stat_pct_dec", val3 = "speed_slow", val4 = "val4_unused" },
	contributes = {
		agi = {
			{ at = 6884, op = "mul_pct", v = function(s, e) return -s.stat_pct_dec end },
		},
		dex = {
			{ at = 7110, op = "mul_pct", v = function(s, e) return -s.stat_pct_dec end },
		},
		speed = {
			{ at = 8130, op = "gmax", group = "slow", when = function(s, e) return not ((e.unit.type == "pc" and e.sc.HIDING ~= nil) and e.skill("RG_TUNNELDRIVE") > 0) and not ((e.unit.type == "pc" and e.sc.CHASEWALK ~= nil) and e.sc.CHASEWALK.speed_value < 0) end, v = function(s, e) return s.speed_slow end },
		},
	},
})

D("MDEFSET", {
	fields = { val1 = "mdef_set", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		mdef = {
			{ at = 7963, op = "final", v = function(s, e) return s.mdef_set end },
		},
		mdef2 = {
			{ at = 8016, op = "final", v = function(s, e) return s.mdef_set end },
		},
	},
})

D("MELODYOFSINK", {
	fields = { val1 = "lv", val2 = "stat_dec", val3 = "maxsp_bonus_dec", val4 = "val4_unused" },
	contributes = {
		sp_rate = {
			{ at = 3413, op = "add", v = function(s, e) return -s.maxsp_bonus_dec end },
		},
		int = {
			{ at = 7016, op = "add", v = function(s, e) return -s.stat_dec end },
		},
	},
})

D("MELON_BOMB", {
	fields = { val1 = "val1_unused", val2 = "speed_slow", val3 = "aspd_rate_inc_arg", val4 = "val4_unused" },
	contributes = {
		speed = {
			{ at = 8142, op = "gmax", group = "slow", when = function(s, e) return not ((e.unit.type == "pc" and e.sc.HIDING ~= nil) and e.skill("RG_TUNNELDRIVE") > 0) and not ((e.unit.type == "pc" and e.sc.CHASEWALK ~= nil) and e.sc.CHASEWALK.speed_value < 0) end, v = function(s, e) return s.speed_slow end },
		},
		aspd_rate = {
			{ at = 8549, op = "add", v = function(s, e) return s.aspd_rate_inc_arg * 10 end },
		},
	},
})

D("MERC_ATKUP", {
	fields = { val1 = "lv", val2 = "watk_inc", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		watk = {
			{ at = 7376, op = "add", v = function(s, e) return s.watk_inc end },
		},
	},
})

D("MERC_FLEEUP", {
	fields = { val1 = "lv", val2 = "flee_inc", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		flee = {
			{ at = 7675, op = "add", v = function(s, e) return s.flee_inc end },
		},
	},
})

D("MERC_HITUP", {
	fields = { val1 = "lv", val2 = "hit_inc", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		hit = {
			{ at = 7585, op = "add", v = function(s, e) return s.hit_inc end },
		},
	},
})

D("MERC_HPUP", {
	fields = { val1 = "lv", val2 = "maxhp_bonus_inc", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		hp_rate = {
			{ at = 3240, op = "add", v = function(s, e) return s.maxhp_bonus_inc end },
		},
	},
})

D("MERC_QUICKEN", {
	fields = { val1 = "val1_unused", val2 = "aspd_rate_quicken", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		aspd_rate = {
			{ at = 8448, op = "gmax", group = "quicken", v = function(s, e) return s.aspd_rate_quicken end },
		},
	},
})

D("MERC_SPUP", {
	fields = { val1 = "lv", val2 = "maxsp_bonus_inc", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		sp_rate = {
			{ at = 3407, op = "add", v = function(s, e) return s.maxsp_bonus_inc end },
		},
	},
})

D("MINDBREAKER", {
	fields = { val1 = "lv", val2 = "matk_increase", val3 = "mdef2_pct_dec", val4 = "val4_unused" },
	contributes = {
		mdef2 = {
			{ at = 8019, op = "mul_pct", v = function(s, e) return -s.mdef2_pct_dec end },
		},
	},
})

D("MODECHANGE", {
	fields = { val1 = "val1_unused", val2 = "mode_set", val3 = "mode_add", val4 = "mode_del" },
})

D("MOONLITSERENADE", {
	fields = { val1 = "lv", val2 = "val2_unused", val3 = "matk_inc", val4 = "val4_unused" },
})

D("MOON_COMFORT", {
	fields = { val1 = "val1_unused", val2 = "flee_inc", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		flee = {
			{ at = 7661, op = "add", v = function(s, e) return s.flee_inc end },
		},
	},
})

D("MUSICAL_INTERLUDE", {
	in_pre_db = false, -- not in db/pre-re/status.yml: the stat code knows it, a PRE server never starts it
	fields = { val1 = "lv", val2 = "res_inc", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		res = {
			{ at = 8655, op = "add", v = function(s, e) return s.res_inc end },
		},
	},
})

D("NEN", {
	fields = { val1 = "stat_inc", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		str = {
			{ at = 6797, op = "add", v = function(s, e) return s.stat_inc end },
		},
		int = {
			{ at = 7006, op = "add", v = function(s, e) return s.stat_inc end },
		},
	},
})

D("NEUTRALBARRIER", {
	fields = { val1 = "lv", val2 = "def_mdef_pct_inc", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		def = {
			{ at = 7836, op = "mul_pct", v = function(s, e) return s.def_mdef_pct_inc end },
		},
		mdef = {
			{ at = 7978, op = "mul_pct", v = function(s, e) return s.def_mdef_pct_inc end },
		},
	},
})

D("NIBELUNGEN", {
	fields = { val1 = "val1_unused", val2 = "atk_inc", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		watk = {
			{ at = 7382, op = "add", when = function(s, e) return e.unit.type ~= "pc" end, v = function(s, e) return s.atk_inc end },
		},
	},
})

D("NPC_HALLUCINATIONWALK", {
	in_pre_db = false, -- not in db/pre-re/status.yml: the stat code knows it, a PRE server never starts it
	fields = { val1 = "lv", val2 = "flee_inc", val3 = "mdef_evasion", val4 = "val4_unused" },
	contributes = {
		flee = {
			{ at = 7679, op = "add", v = function(s, e) return s.flee_inc end },
		},
	},
})

D("NYANGGRASS", {
	fields = { val1 = "val1_unused", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		def = {
			{ at = 7788, op = "final", when = function(s, e) return e.unit.type == "pc" end, v = 0 },
			{ at = 7790, op = "final_div", when = function(s, e) return not (e.unit.type == "pc") end, v = 2 },
		},
		mdef = {
			{ at = 7958, op = "final", when = function(s, e) return e.unit.type == "pc" end, v = 0 },
			{ at = 7960, op = "final_div", when = function(s, e) return not (e.unit.type == "pc") end, v = 2 },
		},
	},
})

D("ODINS_POWER", {
	fields = { val1 = "lv", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		watk = {
			{ at = 7426, op = "add", v = function(s, e) return 40 + (30 * s.lv) end },
		},
		def = {
			{ at = 7808, op = "add", v = function(s, e) return -(20 * s.lv) end },
		},
		mdef = {
			{ at = 7984, op = "add", v = function(s, e) return -(20 * s.lv) end },
		},
	},
})

D("ONEHAND", {
	fields = { val1 = "val1_unused", val2 = "aspd_rate_quicken", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		aspd_rate = {
			{ at = 8444, op = "gmax", group = "quicken", v = function(s, e) return s.aspd_rate_quicken end },
		},
	},
})

D("OVERCOMING_CRISIS", {
	in_pre_db = false, -- not in db/pre-re/status.yml: the stat code knows it, a PRE server never starts it
	fields = { val1 = "lv", val2 = "patk_smatk_inc", val3 = "maxhp_bonus_inc", val4 = "val4_unused" },
	contributes = {
		hp_fix = {
			{ at = 3222, op = "add", v = function(s, e) return s.maxhp_bonus_inc end },
		},
		patk = {
			{ at = 8596, op = "add", v = function(s, e) return s.patk_smatk_inc end },
		},
		smatk = {
			{ at = 8633, op = "add", v = function(s, e) return s.patk_smatk_inc end },
		},
	},
})

D("OVERED_BOOST", {
	fields = { val1 = "lv", val2 = "flee_set", val3 = "fix_aspd_set_arg", val4 = "def_pct_dec" },
	contributes = {
		flee = {
			{ at = 7649, op = "final", v = function(s, e) return s.flee_set end },
		},
		def = {
			{ at = 7854, op = "mul_pct", when = function(s, e) return e.unit.type == "hom" end, v = function(s, e) return -s.def_pct_dec end },
		},
		fix_aspd = {
			{ at = 8402, op = "final_clamp", v = function(s, e) return K.AMOTION_ZERO_ASPD - (s.fix_aspd_set_arg * K.AMOTION_INTERVAL) end },
		},
	},
})

D("PARALYSE", {
	fields = { val1 = "val1_unused", val2 = "val2_unused", val3 = "kind", val4 = "val4_unused" },
	contributes = {
		flee = {
			{ at = 7707, op = "mul_pct", when = function(s, e) return s.kind == 1 end, v = -10 },
		},
		speed = {
			{ at = 8191, op = "gmax", group = "haste", when = function(s, e) return s.kind == 0 end, v = 50 },
			{ at = 8233, op = "mul_pct", when = function(s, e) return s.kind == 1 end, v = 50 },
		},
		aspd_rate = {
			{ at = 8533, op = "add", when = function(s, e) return s.kind == 1 end, v = 100 },
		},
	},
})

D("PARALYSIS", {
	fields = { val1 = "lv", val2 = "def2_pct_dec", val3 = "varcast_inc", val4 = "val4_unused" },
	contributes = {
		def2 = {
			{ at = 7924, op = "mul_pct", v = function(s, e) return -s.def2_pct_dec end },
		},
	},
})

D("PARTYFLEE", {
	fields = { val1 = "lv", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		flee = {
			{ at = 7673, op = "add", v = function(s, e) return s.lv * 10 end },
		},
	},
})

D("PETROLOGY_OPTION", {
	fields = { val1 = "val1_unused", val2 = "maxhp_bonus_inc", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		hp_rate = {
			{ at = 3258, op = "add", v = function(s, e) return s.maxhp_bonus_inc end },
		},
	},
})

D("POISON", {
	fields = { val1 = "val1_unused", val2 = "val2_unused", val3 = "val3_unused", val4 = "remaining_time" },
})

D("POISON_MIST", {
	in_pre_db = false, -- not in db/pre-re/status.yml: the stat code knows it, a PRE server never starts it
	fields = { val1 = "val1_unused", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		flee = {
			{ at = 7647, op = "final", v = 0 },
		},
	},
})

D("POISON_SHIELD_OPTION", {
	in_pre_db = false, -- not in db/pre-re/status.yml: the stat code knows it, a PRE server never starts it
	fields = { val1 = "val1_unused", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		element = {
			{ at = 8804, op = "final", v = function(s, e) return K.ELE_POISON end },
		},
	},
})

D("POWERFUL_FAITH", {
	in_pre_db = false, -- not in db/pre-re/status.yml: the stat code knows it, a PRE server never starts it
	fields = { val1 = "lv", val2 = "watk_inc", val3 = "patk_inc", val4 = "val4_unused" },
	contributes = {
		watk = {
			{ at = 7438, op = "add", v = function(s, e) return s.watk_inc end },
		},
		patk = {
			{ at = 8575, op = "add", v = function(s, e) return s.patk_inc end },
		},
	},
})

D("POWERUP", {
	fields = { val1 = "val1_unused", val2 = "hit_pct_inc", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		hit = {
			{ at = 7589, op = "mul_pct", v = function(s, e) return s.hit_pct_inc end },
		},
	},
})

D("POWER_OF_GAIA", {
	fields = { val1 = "val1_unused", val2 = "def_speed_val", val3 = "maxhp_bonus_inc", val4 = "val4_unused" },
	contributes = {
		hp_rate = {
			{ at = 3260, op = "add", v = function(s, e) return s.maxhp_bonus_inc end },
		},
		def = {
			{ at = 7850, op = "mul_pct", v = function(s, e) return s.def_speed_val end },
		},
		speed = {
			{ at = 8140, op = "gmax", group = "slow", when = function(s, e) return not ((e.unit.type == "pc" and e.sc.HIDING ~= nil) and e.skill("RG_TUNNELDRIVE") > 0) and not ((e.unit.type == "pc" and e.sc.CHASEWALK ~= nil) and e.sc.CHASEWALK.speed_value < 0) end, v = function(s, e) return s.def_speed_val end },
		},
	},
})

D("PRESTIGE", {
	fields = { val1 = "lv", val2 = "magic_evade", val3 = "def_inc", val4 = "val4_unused" },
	contributes = {
		def = {
			{ at = 7838, op = "add", v = function(s, e) return s.def_inc end },
		},
	},
})

D("PRE_ACIES", {
	in_pre_db = false, -- not in db/pre-re/status.yml: the stat code knows it, a PRE server never starts it
	fields = { val1 = "lv", val2 = "crate_inc", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		crate = {
			{ at = 8721, op = "add", v = function(s, e) return s.crate_inc end },
		},
	},
})

D("PROMOTE_HEALTH_RESERCH", {
	fields = { val1 = "val1_unused", val2 = "val2_unused", val3 = "maxhp_bonus_inc", val4 = "val4_unused" },
	contributes = {
		hp_fix = {
			{ at = 3204, op = "add", v = function(s, e) return s.maxhp_bonus_inc end },
		},
	},
})

D("PRON_MARCH", {
	in_pre_db = false, -- not in db/pre-re/status.yml: the stat code knows it, a PRE server never starts it
	fields = { val1 = "lv", val2 = "patk_inc", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		patk = {
			{ at = 8581, op = "add", v = function(s, e) return s.patk_inc end },
		},
	},
})

D("PROVOKE", {
	fields = { val1 = "lv", val2 = "atk_pct", val3 = "def_pct_dec", val4 = "auto" },
	contributes = {
		def = {
			{ at = 7826, op = "mul_pct", when = function(s, e) return e.unit.type ~= "pc" end, v = function(s, e) return -s.def_pct_dec end },
		},
		def2 = {
			{ at = 7913, op = "mul_pct", v = function(s, e) return -s.def_pct_dec end },
		},
	},
})

D("PUSH_CART", {
	fields = { val1 = "val1_unused", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
})

D("PYROCLASTIC", {
	fields = { val1 = "val1_unused", val2 = "batk_watk_inc", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		batk = {
			{ at = 7332, op = "add", when = function(s, e) return e.unit.type == "hom" end, v = function(s, e) return s.batk_watk_inc end },
		},
		watk = {
			{ at = 7422, op = "add", when = function(s, e) return e.unit.type == "pc" end, v = function(s, e) return s.batk_watk_inc end },
		},
	},
})

D("PYROTECHNIC_OPTION", {
	fields = { val1 = "val1_unused", val2 = "watk_inc", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		watk = {
			{ at = 7414, op = "add", v = function(s, e) return s.watk_inc end },
		},
	},
})

D("QUAGMIRE", {
	fields = { val1 = "lv", val2 = "agi_dex_dec", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		agi = {
			{ at = 6872, op = "add", v = function(s, e) return -s.agi_dex_dec end },
		},
		dex = {
			{ at = 7080, op = "add", v = function(s, e) return -s.agi_dex_dec end },
		},
	},
})

D("RAISINGDRAGON", {
	fields = { val1 = "maxhp_bonus_maxsp_bonus_inc", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		hp_rate = {
			{ at = 3252, op = "add", v = function(s, e) return s.maxhp_bonus_maxsp_bonus_inc end },
		},
		sp_rate = {
			{ at = 3403, op = "add", v = function(s, e) return s.maxhp_bonus_maxsp_bonus_inc end },
		},
	},
})

D("REBOUND", {
	fields = { val1 = "val1_unused", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		speed = {
			{ at = 8144, op = "gmax", group = "slow", when = function(s, e) return not ((e.unit.type == "pc" and e.sc.HIDING ~= nil) and e.skill("RG_TUNNELDRIVE") > 0) and not ((e.unit.type == "pc" and e.sc.CHASEWALK ~= nil) and e.sc.CHASEWALK.speed_value < 0) end, v = 25 },
		},
	},
})

D("RECOGNIZEDSPELL", {
	fields = { val1 = "val1_unused", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
})

D("RELIGIO", {
	in_pre_db = false, -- not in db/pre-re/status.yml: the stat code knows it, a PRE server never starts it
	fields = { val1 = "lv", val2 = "sta_wis_spl_inc", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		sta = {
			{ at = 7222, op = "add", v = function(s, e) return s.sta_wis_spl_inc end },
		},
		wis = {
			{ at = 7242, op = "add", v = function(s, e) return s.sta_wis_spl_inc end },
		},
		spl = {
			{ at = 7262, op = "add", v = function(s, e) return s.sta_wis_spl_inc end },
		},
	},
})

D("ROCK_CRUSHER", {
	fields = { val1 = "val1_unused", val2 = "def_pct_dec", val3 = "hp_rate_bonus", val4 = "val4_unused" },
	contributes = {
		def = {
			{ at = 7848, op = "mul_pct", v = function(s, e) return -s.def_pct_dec end },
		},
	},
})

D("ROCK_CRUSHER_ATK", {
	fields = { val1 = "val1_unused", val2 = "speed_slow", val3 = "hp_rate_bonus", val4 = "val4_unused" },
	contributes = {
		speed = {
			{ at = 8138, op = "gmax", group = "slow", when = function(s, e) return not ((e.unit.type == "pc" and e.sc.HIDING ~= nil) and e.skill("RG_TUNNELDRIVE") > 0) and not ((e.unit.type == "pc" and e.sc.CHASEWALK ~= nil) and e.sc.CHASEWALK.speed_value < 0) end, v = function(s, e) return s.speed_slow end },
		},
	},
})

D("RUN", {
	fields = { val1 = "val1_unused", val2 = "val2_unused", val3 = "val3_unused", val4 = "val4_unused" },
	contributes = {
		speed = {
			{ at = 8183, op = "gmax", group = "haste", v = 55 },
		},
	},
})

D("RUSHWINDMILL", {
	fields = { val1 = "lv", val2 = "val2_unused", val3 = "watk_inc", val4 = "val4_unused" },
	contributes = {
		watk = {
			{ at = 7405, op = "add", v = function(s, e) return s.watk_inc end },
		},
		speed = {
			{ at = 8207, op = "gmax", group = "haste", v = 25 },
		},
	},
})

