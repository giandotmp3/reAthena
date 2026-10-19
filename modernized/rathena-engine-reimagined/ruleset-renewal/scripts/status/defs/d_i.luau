--!nonstrict
-- Status definitions, D to I (hand-written from src/map/status.cpp; see a_c.luau for the conventions).
local status = import("scripts/status/status")
local define = status.define

define("dancewithwug", {
	fields = { val3 = "aspd_bonus", val4 = "fcast_reduction" }, -- 12128: 5 * lv, 20 + 10 * lv
	contributes = {
		aspd_pct = { "add", "aspd_bonus", at = 8348 },
		aspd_rate = { "sub", "aspd_bonus * 10", at = 8542 },
	},
})

-- Bard / Dancer song or dance in progress. The dance penalty of speed reads the Musical / Dancing Lesson of the player and the spirit link
-- (status.cpp:8098); the restrictions of the performance are conditional states (status.cpp:5657, 5742)
define("dancing", {
	note = "val1 = skill id | (level << 16) for Moonlit Serenade; val4 != 0 marks an ensemble.",
	fields = { val1 = "skill_and_level", val2 = "group", val3 = "ticks", val4 = "partner" }, -- 11086-11093: skill id | level << 16 for Moonlit
	contributes = { speed_rate = { "slow_dance", at = 8098 } },
	state_when = {
		-- RENEWAL: an ensemble (val4 != 0) only restrains movement for Moonlit Serenade and Hermode (status.cpp:5657-5673). The record makes
		-- NOATTACK unconditional, so the conditional attack rule of status_calc_state (5742) is never consulted for this status.
		nomove = "partner != 0 && ((skill_and_level & 0xFFFF) == CG_MOONLIT || (skill_and_level & 0xFFFF) == CG_HERMODE)",
	},
})

define("decreaseagi", {
	fields = { val1 = "level", val2 = "agi_loss" }, -- 10875: val2 = 2 + lv
	contributes = {
		agi = { "sub", "agi_loss", at = 6869 },
		speed_rate = { "slow", "25", at = 8101 },
	},
})

define("defence", { -- Defender's shield buff of the player / the homunculus Defense
	fields = { val1 = "level", val2 = "bonus" }, -- 11703: 5 + 5 * lv (players: 2 * lv)
	contributes = {
		def = { "add", "bonus", when = "u.is_hom", at = 7801 },
		vit = { "add", "bonus", when = "u.is_pc", at = 6958 },
	},
})

define("defender", {
	fields = { val1 = "level", val2 = "damage_reduction", val4 = "aspd_penalty" }, -- 11338: 5 + 15 * lv, val4 = 250 - 50 * lv
	contributes = {
		aspd_pct = { "sub", "aspd_penalty / 10", at = 8320 },
		aspd_rate = { "add", "aspd_penalty", at = 8514 },
		speed = { "min200", at = 8238 },
	},
})

define("defset", {
	note = "The returned value is an int16: a val1 above 32767 wraps (status.cpp:7792, defType is int16 in renewal).",
	fields = { val1 = "def" },
	contributes = { def = { "ret", "def", at = 7792 }, def2 = { "ret", "def", at = 7891 } },
})

define("deluge", {
	fields = { val2 = "hp_rate", val3 = "enchant" }, -- 11049: from a table by (lv - 1) % 5
	contributes = { hp_rate = { "add", "hp_rate", at = 3235 } },
})

define("dontforgetme", {
	fields = { val1 = "level", val2 = "aspd_penalty", val3 = "speed_pct" }, -- 11137: val2 = 1 + 30 * lv, val3 = 5 + 2 * lv
	contributes = {
		aspd_pct = { "sub", "aspd_penalty / 10", when = "u.is_pc", at = 8309 },
		aspd_rate = { "add", "aspd_penalty", when = "u.is_pc", at = 8503 },
		speed_rate = { "slow", "speed_pct", at = 8105 },
	},
})

define("doram_flee2", {
	fields = { val1 = "flee2" },
	contributes = { flee2 = { "add", "flee2", at = 7756 } },
})

define("doram_matk", {
	fields = { val1 = "matk" },
	contributes = { matk_flat = { "add", "matk", at = 7471 } },
})

define("doram_walkspeed", {
	fields = { val1 = "speed_pct" },
	contributes = { speed_rate = { "haste", "speed_pct", at = 8204 } },
})

define("dpoison", {
	contributes = {
		def = { "scale", "75", when = "u.is_pc == 0", group = "poison", at = 7817 },
		def2 = { "scale", "75", group = "poison", at = 7908 },
	},
})

define("drumbattle", {
	fields = { val1 = "level", val2 = "atk_bonus", val3 = "def_bonus" }, -- val2 is read by the pre-renewal WATK only
	contributes = { def = { "add", "def_bonus", at = 7795 } },
})

define("d_machine", {
	fields = { val2 = "def_bonus", val3 = "res_bonus" },
	contributes = { def = { "add", "def_bonus", at = 7859 }, res = { "add", "res_bonus", at = 8652 } },
})

define("earthweapon", { contributes = { atk_ele = { "ret", "ELE_EARTH", at = 8866 } } })

-- Elemental Insignia: val1 is the insignia type; 2 = weapon element and ATK, 3 = MATK / MDEF / SP (status.cpp:7405-7412, 7495, 3201, 3365)
define("earth_insignia", {
	fields = { val1 = "insignia_type" },
	contributes = {
		atk_ele = { "ret", "ELE_EARTH", when = "insignia_type == 2", at = 8866 },
		def = { "add", "50", when = "insignia_type == 2", at = 7805 },
		hp_fix = { "add", "500", when = "insignia_type == 2", at = 3201 },
		sp_fix = { "add", "50", when = "insignia_type == 3", at = 3365 },
		mdef = { "add", "50", when = "insignia_type == 3", at = 7965 },
		watk = { "pct_add", "10", when = "insignia_type == 2", group = "insignia", at = 7408 },
	},
})

define("echosong", {
	fields = { val1 = "level", val3 = "def_bonus" },
	contributes = { def = { "add", "def_bonus", at = 7841 } },
})

define("elementalchange", {
	fields = { val1 = "element_level", val2 = "element" },
	contributes = { def_ele = { "ret", "element", at = 8809 }, ele_lv = { "ret", "element_level", at = 8837 } },
})

define("emergency_move", {
	fields = { val2 = "speed_pct" },
	contributes = { speed_rate = { "haste", "speed_pct", at = 8208 } },
})

define("enchantarms", {
	fields = { val1 = "element" },
	contributes = { atk_ele = { "ret", "element", at = 8861 } },
})

define("encpoison", { contributes = { atk_ele = { "ret", "ELE_POISON", at = 8875 } } })

define("endure", {
	note = "val4 != 0 is the card variant, which gives a fixed 1 MDEF; val3 != 0 disables the bonus.",
	fields = { val1 = "mdef", val3 = "no_mdef_flag", val4 = "card_endure" }, -- val4 != 0: the card variant (a fixed 1 MDEF)
	contributes = { mdef = { "add", "(card_endure == 0) * mdef + (card_endure != 0) * 1", when = "no_mdef_flag == 0", at = 7967 } },
})

define("ensemblefatigue", {
	fields = { val2 = "penalty" },
	contributes = {
		aspd_pct = { "sub", "penalty", at = 8312 },
		aspd_rate = { "add", "penalty", at = 8506 },
		speed_rate = { "slow_fatigue", "penalty", at = 8091 },
	},
})

define("epiclesis", {
	fields = { val2 = "hp_rate" },
	contributes = { hp_rate = { "add", "hp_rate", at = 3241 } },
})

define("eqc", {
	fields = { val2 = "def_pct", val3 = "hp_rate_loss" },
	contributes = { def2 = { "pct_sub", "def_pct", at = 7925 }, hp_rate = { "sub", "hp_rate_loss", at = 3285 } },
})

define("eternalchaos", {
	contributes = { def = { "ret", "0", at = 7777 }, def2 = { "ret", "0", at = 7889 } },
})

define("explosionspirits", {
	fields = { val2 = "cri" },
	contributes = { cri = { "add", "cri", at = 7530 } },
})

define("eyes_of_storm_option", {
	contributes = { def_ele = { "ret", "ELE_WIND", at = 8801 }, ele_lv = { "ret", "1", at = 8843 } },
})

define("fear", {
	fields = { val2 = "no_move" },
	contributes = { flee = { "pct_sub", "20", at = 7704 }, hit = { "pct_sub", "20", at = 7596 } },
	state_when = { nomove = "no_move > 0" }, -- status.cpp:5643
})

define("fightingspirit", {
	fields = { val1 = "watk", val2 = "aspd" },
	contributes = { fix_aspd = { "sub", "aspd", at = 8406 }, watk = { "add", "watk", at = 7392 } },
})

define("fireweapon", { contributes = { atk_ele = { "ret", "ELE_FIRE", at = 8869 } } })

define("fire_insignia", {
	fields = { val1 = "insignia_type" },
	contributes = {
		atk_ele = { "ret", "ELE_FIRE", when = "insignia_type == 2", at = 8869 },
		matk_flat = { "add", "50", when = "insignia_type == 3", at = 7495 },
		watk = {
			{ "add", "50", when = "insignia_type == 2", at = 7406 },
			{ "pct_add", "10", when = "insignia_type == 2", group = "insignia", at = 7408 },
		},
	},
})

define("firm_faith", {
	fields = { val2 = "hp_rate", val3 = "res" },
	contributes = { hp_rate = { "add", "hp_rate", at = 3275 }, res = { "add", "res", at = 8650 } },
})

define("flamearmor_option", {
	contributes = { def_ele = { "ret", "ELE_FIRE", at = 8799 }, ele_lv = { "ret", "1", at = 8843 } },
})

define("flashcombo", {
	fields = { val2 = "watk" },
	contributes = { watk = { "add", "watk", at = 7427 } },
})

define("fleet", {
	fields = { val1 = "level", val2 = "aspd_rate" },
	contributes = {
		aspd_fixed = { "quicken5", at = 8274 },
		aspd_rate = { "max_cand", "aspd_rate", at = 8466 },
	},
})

define("fling", {
	fields = { val2 = "def_pct", val3 = "def2_pct" },
	contributes = { def = { "pct_sub", "def_pct", at = 7829 }, def2 = { "pct_sub", "def2_pct", at = 7917 } },
})

define("forceofvanguard", {
	fields = { val1 = "level" },
	contributes = { hp_rate = { "add", "3 * level", at = 3247 } },
})

define("fortune", {
	fields = { val2 = "cri" },
	contributes = { cri = { "add", "cri", at = 7532 } },
})

define("freeze", {
	contributes = {
		def = { "div", "2", at = 7815 },
		def_ele = { "ret", "ELE_WATER", at = 8795 },
		ele_lv = { "ret", "1", at = 8829 },
		mdef = { "pct_add", "25", at = 7973 },
	},
})

define("freezing", {
	contributes = {
		aspd_pct = { "sub", "30", at = 8334 },
		aspd_rate = { "add", "300", at = 8528 },
		def = { "pct_sub", "10 + 20 * u.is_pc", at = 7831 },
		speed_rate = { "slow", "30", at = 8127 },
	},
})

define("frigg_song", {
	fields = { val2 = "hp_rate" },
	contributes = { hp_rate = { "add", "hp_rate", at = 3243 } },
})

define("full_throttle", {
	fields = { val3 = "stat_pct" },
	contributes = {
		str = { "pct_add", "stat_pct", at = 6824 }, agi = { "pct_add", "stat_pct", at = 6895 }, vit = { "pct_add", "stat_pct", at = 6956 },
		int = { "pct_add", "stat_pct", at = 7029 }, dex = { "pct_add", "stat_pct", at = 7111 }, luk = { "pct_add", "stat_pct", at = 7172 },
		speed_rate = { "haste", "25", at = 8200 },
	},
})

define("fusion", { contributes = { speed_rate = { "haste2_fusion", "25", at = 8070 } } })

define("gatlingfever", {
	fields = { val1 = "aspd_bonus", val2 = "aspd_rate", val4 = "flee_loss" },
	contributes = {
		aspd_pct = { "add", "aspd_bonus", at = 8360 },
		aspd_rate = { "max_cand", "aspd_rate", at = 8462 },
		flee = { "sub", "flee_loss", at = 7670 },
		speed_rate = { "slow", "100", at = 8121 },
	},
})

define("gef_nocturn", {
	fields = { val2 = "mres_loss" },
	contributes = { mres = { "sub", "mres_loss", at = 8684 } },
})

define("ghostweapon", { contributes = { atk_ele = { "ret", "ELE_GHOST", at = 8881 } } })

define("giantgrowth", {
	fields = { val2 = "str_bonus" },
	contributes = { str = { "add", "str_bonus", at = 6810 } },
})

define("gloomyday", {
	fields = { val2 = "flee_pct", val3 = "aspd_penalty", val4 = "slows" },
	contributes = {
		aspd_pct = { "sub", "aspd_penalty", at = 8350 },
		aspd_rate = { "add", "aspd_penalty * 10", at = 8544 },
		flee = { "pct_sub", "flee_pct", at = 7712 },
		speed_rate = { "slow", "50", when = "slows != 0", at = 8103 },
	},
})

define("gloria", { contributes = { luk = { "add", "30", at = 7150 } } })

define("glorywounds", {
	fields = { val1 = "vit_bonus" },
	contributes = { vit = { "add", "vit_bonus", at = 6934 } },
})

define("gn_cartboost", {
	fields = { val2 = "speed_pct" },
	contributes = { speed_rate = { "haste", "speed_pct", at = 8194 } },
})

define("goldene_ferse", {
	fields = { val2 = "flee_pct", val3 = "aspd_bonus" },
	contributes = {
		aspd_pct = { "add", "aspd_bonus", at = 8356 },
		aspd_rate = { "sub", "aspd_bonus * 10", at = 8552 },
		flee = { "pct_add", "flee_pct", at = 7724 },
	},
})

define("goldene_tone", {
	fields = { val2 = "res_mres" },
	contributes = { mres = { "add", "res_mres", at = 8680 }, res = { "add", "res_mres", at = 8656 } },
})

define("goldenmaceclan", {
	contributes = {
		int = { "add", "1", at = 7025 }, luk = { "add", "1", at = 7168 },
		hp_fix = { "add", "30", at = 3213 }, sp_fix = { "add", "10", at = 3371 },
	},
})

define("gospel", {
	note = "val4 = BCT_SELF for the caster's own gospel (movement restriction), BCT_ENEMY for the debuff side.",
	fields = { val4 = "target_type" }, -- BCT_ENEMY: the debuff side of the gospel; BCT_SELF: the caster
	contributes = {
		aspd_pct = { "sub", "75", when = "target_type == BCT_ENEMY", at = 8322 },
		aspd_rate = { "add", "250", when = "target_type == BCT_ENEMY", at = 8516 },
		speed_rate = { "slow", "75", when = "target_type == BCT_ENEMY", at = 8117 },
	},
	state_when = { nomove = "target_type == BCT_SELF" }, -- status.cpp:5609
})

define("grooming", {
	fields = { val2 = "flee" },
	contributes = { flee = { "add", "flee", at = 7732 } },
})

define("groundgravity", { contributes = { speed_rate = { "slow", "20", at = 8155 } } })

define("gt_change", {
	fields = { val2 = "watk", val3 = "aspd_bonus" },
	contributes = {
		aspd_pct = { "add", "aspd_bonus", at = 8352 },
		aspd_rate = { "sub", "aspd_bonus * 10", at = 8546 },
		watk = { "add", "watk", at = 7398 },
	},
})

define("gt_revitalize", {
	fields = { val2 = "hp_rate" },
	contributes = { hp_rate = { "add", "hp_rate", at = 3253 } },
})

define("guard_stance", {
	fields = { val2 = "def_bonus", val3 = "watk_loss" },
	contributes = { def = { "add", "def_bonus", at = 7863 }, watk = { "sub", "watk_loss", at = 7439 } },
})

define("gust_option", { contributes = { fix_aspd = { "sub", "50", group = "gust", at = 8404 } } })

define("hallucinationwalk", {
	fields = { val2 = "flee" },
	contributes = { flee = { "add", "flee", at = 7676 } },
})

define("hallucinationwalk_postdelay", {
	contributes = {
		aspd_pct = { "sub", "50", at = 8336 },
		aspd_rate = { "add", "500", at = 8530 },
		speed_rate = { "slow", "50", at = 8103 },
	},
})

define("harmonize", { -- Harmonize returns before anything else touches the stat (the first statement of every stat function)
	fields = { val2 = "stat_loss" },
	contributes = {
		str = { "sub_end", "stat_loss", at = 6778 }, agi = { "sub_end", "stat_loss", at = 6853 }, vit = { "sub_end", "stat_loss", at = 6926 },
		int = { "sub_end", "stat_loss", at = 6987 }, dex = { "sub_end", "stat_loss", at = 7065 }, luk = { "sub_end", "stat_loss", at = 7140 },
	},
})

define("hawkeyes", {
	fields = { val1 = "dex_bonus" },
	contributes = { dex = { "add", "dex_bonus", at = 7075 } },
})

define("heater_option", {
	fields = { val2 = "watk" },
	contributes = { watk = { "add", "watk", at = 7415 } },
})

define("heat_barrel", {
	fields = { val1 = "level", val4 = "hit_loss" },
	contributes = { fix_aspd = { "sub", "level * 10", at = 8410 }, hit = { "sub", "hit_loss", at = 7592 } },
})

define("hidden_card", {
	fields = { val2 = "patk" },
	contributes = { patk = { "add", "patk", at = 8587 } },
})

define("hiding", {
	note = "Slows a player only with Tunnel Drive; monsters never move while hidden (conditional state).",
	contributes = { speed_rate = { "slow_hiding", "120 - 6 * u.skill.RG_TUNNELDRIVE", when = "u.is_pc && u.skill.RG_TUNNELDRIVE > 0", at = 8083 } },
	state_when = { nomove = "u.is_pc == 0 || u.skill.RG_TUNNELDRIVE <= 0" }, -- status.cpp:5649
})

define("hiss", {
	fields = { val2 = "flee2" },
	contributes = { flee2 = { "add", "flee2 * 10", at = 7754 } },
})

define("hovering", { contributes = { speed_rate = { "haste", "10", at = 8192 } } })

define("humming", {
	fields = { val2 = "hit" },
	contributes = { hit = { "add", "hit", at = 7574 } },
})

define("illusiondoping", {
	fields = { val2 = "hit_loss" },
	contributes = { hit = { "sub", "hit_loss", at = 7602 } },
})

define("impositio", {
	fields = { val2 = "atk" },
	contributes = { matk_flat = { "add", "atk", at = 7466 }, watk = { "add", "atk", at = 7369 } },
})

define("incallstatus", {
	fields = { val1 = "stat_bonus" },
	contributes = {
		str = { "add", "stat_bonus", at = 6782 }, agi = { "add", "stat_bonus", at = 6859 }, vit = { "add", "stat_bonus", at = 6930 },
		int = { "add", "stat_bonus", at = 6991 }, dex = { "add", "stat_bonus", at = 7071 }, luk = { "add", "stat_bonus", at = 7146 },
	},
})

define("incaspdrate", {
	fields = { val1 = "aspd_bonus" },
	contributes = { aspd_pct = { "add", "aspd_bonus", at = 8358 }, aspd_rate = { "sub", "aspd_bonus * 10", at = 8550 } },
})

define("incdefrate", {
	fields = { val1 = "def_pct" },
	contributes = { def = { "pct_add", "def_pct", at = 7803 } },
})

define("incflee", {
	fields = { val1 = "flee" },
	contributes = { flee = { "add", "flee", at = 7652 } },
})

define("incfleerate", {
	fields = { val1 = "flee_pct" },
	contributes = { flee = { "pct_add", "flee_pct", at = 7694 } },
})

define("inchit", {
	fields = { val1 = "hit" },
	contributes = { hit = { "add", "hit", at = 7570 } },
})

define("inchitrate", {
	fields = { val1 = "hit_pct" },
	contributes = { hit = { "pct_add", "hit_pct", at = 7586 } },
})

define("incmhp", {
	fields = { val1 = "hp" },
	contributes = { hp_fix = { "add", "hp", at = 3199 } },
})

define("incmhprate", {
	fields = { val1 = "hp_rate" },
	contributes = { hp_rate = { "add", "hp_rate", at = 3231 } },
})
