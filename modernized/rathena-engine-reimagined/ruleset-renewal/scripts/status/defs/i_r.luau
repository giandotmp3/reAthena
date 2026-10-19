--!nonstrict
-- Status definitions, I (INCMSP) to R (hand-written from src/map/status.cpp; see a_c.luau for the conventions).
local status = import("scripts/status/status")
local define = status.define

define("incmsp", {
	fields = { val1 = "sp" },
	contributes = { sp_fix = { "add", "sp", at = 3363 } },
})

define("incmsprate", {
	fields = { val1 = "sp_rate" },
	contributes = { sp_rate = { "add", "sp_rate", at = 3400 } },
})

define("increaseagi", {
	fields = { val1 = "aspd_bonus", val2 = "agi" }, -- val2 = 2 + lv (status.cpp:10875)
	contributes = {
		agi = { "add", "agi", at = 6865 },
		aspd_pct = { "add", "aspd_bonus", at = 8366 },
		speed_rate = { "haste", "25", at = 8170 },
	},
})

define("increasing", {
	contributes = { agi = { "add", "4", at = 6867 }, dex = { "add", "4", at = 7087 }, hit = { "add", "20", at = 7582 } },
})

define("infraredscan", { contributes = { flee = { "pct_sub", "30", at = 7708 } } })

define("inspiration", {
	fields = { val1 = "level", val2 = "atk_matk", val3 = "stat_bonus" },
	contributes = {
		str = { "add", "stat_bonus", at = 6814 }, agi = { "add", "stat_bonus", at = 6885 }, vit = { "add", "stat_bonus", at = 6944 },
		int = { "add", "stat_bonus", at = 7013 }, dex = { "add", "stat_bonus", at = 7095 }, luk = { "add", "stat_bonus", at = 7158 },
		hit = { "add", "12 * level", at = 7578 },
		matk_flat = { "add", "atk_matk", at = 7481 },
		hp_rate = { "add", "4 * level", at = 3249 },
		watk = { "add", "atk_matk", at = 7396 },
	},
})

define("intensive_aim", {
	contributes = { batk = { "add", "150", at = 7347 }, cri = { "add", "300", at = 7552 }, hit = { "add", "250", at = 7620 } },
})

define("invincible", {
	fields = { val3 = "speed_pct", val4 = "aspd_rate" },
	contributes = {
		aspd_rate = { "max_cand", "aspd_rate", at = 8470 },
		speed_rate = { "haste", "speed_pct", at = 8186 },
	},
})

define("izayoi", {
	fields = { val1 = "level" },
	contributes = { matk_flat = { "add", "25 * level", at = 7493 } },
})

define("jawaii_serenade", {
	fields = { val2 = "smatk" },
	contributes = { smatk = { "add", "smatk", at = 8617 }, speed_rate = { "haste", "25", at = 8210 } },
})

define("jointbeat", {
	fields = { val1 = "level", val2 = "broken" }, -- val2: bit set of BREAK_ANKLE / WRIST / KNEE / SHOULDER / WAIST / NECK
	contributes = {
		aspd_pct = {
			{ "sub", "25", when = "(broken & BREAK_WRIST) != 0", at = 8329 },
			{ "sub", "10", when = "(broken & BREAK_KNEE) != 0", at = 8331 },
		},
		aspd_rate = {
			{ "add", "250", when = "(broken & BREAK_WRIST) != 0", at = 8523 },
			{ "add", "100", when = "(broken & BREAK_KNEE) != 0", at = 8525 },
		},
		def2 = { "pct_sub2", "((broken & BREAK_SHOULDER) != 0) * 50", arg = "((broken & BREAK_WAIST) != 0) * 25", at = 7914 },
		speed_rate = {
			"slow", "((broken & BREAK_ANKLE) != 0) * 50 + ((broken & BREAK_KNEE) != 0) * 30",
			when = "(broken & (BREAK_ANKLE | BREAK_KNEE)) != 0", at = 8113,
		},
	},
})

define("jumpingclan", {
	contributes = {
		str = { "add", "1", at = 6822 }, agi = { "add", "1", at = 6893 }, vit = { "add", "1", at = 6952 },
		int = { "add", "1", at = 7027 }, dex = { "add", "1", at = 7105 }, luk = { "add", "1", at = 7170 },
	},
})

define("keeping", { contributes = { def = { "ret", "90", at = 7780 } } })

define("kyougaku", {
	fields = { val2 = "stat_loss" },
	contributes = {
		str = { "sub", "stat_loss", at = 6818 }, agi = { "sub", "stat_loss", at = 6889 }, vit = { "sub", "stat_loss", at = 6948 },
		int = { "sub", "stat_loss", at = 7021 }, dex = { "sub", "stat_loss", at = 7099 }, luk = { "sub", "stat_loss", at = 7162 },
	},
})

define("laudaagnus", {
	fields = { val1 = "level" },
	contributes = { hp_rate = { "add", "2 + level * 2", at = 3265 } },
})

define("leadership", {
	fields = { val1 = "str_bonus" },
	contributes = { str = { "add", "str_bonus", at = 6788 } },
})

define("leradsdew", {
	fields = { val3 = "hp_rate" },
	contributes = { hp_rate = { "add", "hp_rate", at = 3245 } },
})

define("loud", {
	contributes = { batk = { "add", "30", at = 7340 }, str = { "add", "4", at = 6790 } },
})

define("lunarstance", {
	fields = { val2 = "hp_rate" },
	contributes = { hp_rate = { "add", "hp_rate", at = 3271 } },
})

define("luxanima", {
	fields = { val3 = "hp_sp_rate" },
	contributes = { hp_rate = { "add", "hp_sp_rate", at = 3273 }, sp_rate = { "add", "hp_sp_rate", at = 3408 } },
})

define("madnesscancel", {
	contributes = { aspd_fixed = { "madness", at = 8301 }, aspd_rate = { "madness", at = 8494 } },
})

define("mandragora", {
	fields = { val1 = "level" },
	contributes = { int = { "sub", "4 * level", at = 7017 } },
})

define("marine_festival", {
	fields = { val2 = "trait_bonus" },
	contributes = {
		pow = { "add", "trait_bonus", at = 7203 }, con = { "add", "trait_bonus", at = 7283 }, crt = { "add", "trait_bonus", at = 7303 },
	},
})

-- Marionette: the controller's stats are packed into val3 (STR << 16 | AGI << 8 | VIT) and val4 (INT << 16 | DEX << 8 | LUK)
define("marionette", {
	note = "val3 packs the controller's STR << 16 | AGI << 8 | VIT, val4 INT << 16 | DEX << 8 | LUK (status.cpp:6804-7152). The -1000 HP is a signed int added to an unsigned 64-bit maximum HP: a base below 1000 wraps to UINT_MAX.",
	fields = { val3 = "str_agi_vit", val4 = "int_dex_luk" },
	contributes = {
		str = { "sub", "(str_agi_vit >> 16) & 0xFF", at = 6804 }, agi = { "sub", "(str_agi_vit >> 8) & 0xFF", at = 6875 },
		vit = { "sub", "str_agi_vit & 0xFF", at = 6938 }, int = { "sub", "(int_dex_luk >> 16) & 0xFF", at = 7007 },
		dex = { "sub", "(int_dex_luk >> 8) & 0xFF", at = 7089 }, luk = { "sub", "int_dex_luk & 0xFF", at = 7152 },
		hp_fix = { "sub", "1000", at = 3207 },
	},
})

define("marionette2", {
	note = "The caster side of Marionette: the same packing as marionette, added instead of subtracted.",
	fields = { val3 = "str_agi_vit", val4 = "int_dex_luk" },
	contributes = {
		str = { "add", "(str_agi_vit >> 16) & 0xFF", at = 6806 }, agi = { "add", "(str_agi_vit >> 8) & 0xFF", at = 6877 },
		vit = { "add", "str_agi_vit & 0xFF", at = 6940 }, int = { "add", "(int_dex_luk >> 16) & 0xFF", at = 7009 },
		dex = { "add", "(int_dex_luk >> 8) & 0xFF", at = 7091 }, luk = { "add", "int_dex_luk & 0xFF", at = 7154 },
	},
})

define("marshofabyss", {
	fields = { val2 = "stat_pct", val3 = "speed_pct" },
	contributes = {
		agi = { "pct_sub", "stat_pct", at = 6883 }, dex = { "pct_sub", "stat_pct", at = 7109 },
		speed_rate = { { "slow", "speed_pct", at = 8129 }, { "cap_marsh", at = 8164 } },
	},
})

define("mdefset", {
	note = "Same narrowing as defset (status.cpp:7962).",
	fields = { val1 = "mdef" },
	contributes = { mdef = { "ret", "mdef", at = 7962 }, mdef2 = { "ret", "mdef", at = 8015 } },
})

define("melodyofsink", {
	fields = { val2 = "int_loss", val3 = "sp_rate_loss" },
	contributes = { int = { "sub", "int_loss", at = 7015 }, sp_rate = { "sub", "sp_rate_loss", at = 3412 } },
})

define("melon_bomb", {
	fields = { val2 = "speed_pct", val3 = "aspd_penalty" },
	contributes = {
		aspd_pct = { "sub", "aspd_penalty", at = 8354 },
		aspd_rate = { "add", "aspd_penalty * 10", at = 8548 },
		speed_rate = { "slow", "speed_pct", at = 8141 },
	},
})

define("merc_atkup", { fields = { val2 = "watk" }, contributes = { watk = { "add", "watk", at = 7375 } } })
define("merc_fleeup", { fields = { val2 = "flee" }, contributes = { flee = { "add", "flee", at = 7674 } } })
define("merc_hitup", { fields = { val2 = "hit" }, contributes = { hit = { "add", "hit", at = 7584 } } })
define("merc_hpup", { fields = { val2 = "hp_rate" }, contributes = { hp_rate = { "add", "hp_rate", at = 3239 } } })
define("merc_spup", { fields = { val2 = "sp_rate" }, contributes = { sp_rate = { "add", "sp_rate", at = 3406 } } })

define("merc_quicken", {
	fields = { val2 = "aspd_rate" },
	contributes = { aspd_fixed = { "quicken7", at = 8270 }, aspd_rate = { "max_cand", "aspd_rate", at = 8446 } },
})

define("mindbreaker", {
	fields = { val3 = "mdef2_pct" },
	contributes = { mdef2 = { "pct_sub", "mdef2_pct", at = 8018 } },
})

define("modechange", { -- val2: replaces the mode bits under MD_MASK, val3: adds, val4: removes (each applied when non-zero, in this order)
	note = "val2 replaces the mode bits under MD_MASK, val3 adds bits, val4 removes bits; each applies only when non-zero.",
	fields = { val2 = "mode_set", val3 = "mode_add", val4 = "mode_del" },
	contributes = {
		mode = {
			{ "set_masked", "mode_set", when = "mode_set != 0", at = 8900 },
			{ "bor", "mode_add", when = "mode_add != 0", at = 8902 },
			{ "band_not", "mode_del", when = "mode_del != 0", at = 8904 },
		},
	},
})

define("moonlitserenade", {
	fields = { val3 = "matk" },
	contributes = { matk_flat = { "add", "matk", at = 7487 } },
})

define("moon_comfort", {
	fields = { val2 = "flee" },
	contributes = { flee = { "add", "flee", at = 7660 } },
})

define("musical_interlude", {
	fields = { val2 = "res" },
	contributes = { res = { "add", "res", at = 8654 } },
})

define("nen", {
	fields = { val1 = "stat_bonus" },
	contributes = { str = { "add", "stat_bonus", at = 6796 }, int = { "add", "stat_bonus", at = 7005 } },
})

define("neutralbarrier", {
	fields = { val2 = "def_pct" },
	contributes = { def = { "pct_add", "def_pct", at = 7835 }, mdef = { "pct_add", "def_pct", at = 7977 } },
})

define("nibelungen", { -- the Ring of Nibelungen option (val2 is the RINGNBL_* type chosen by the card)
	fields = { val2 = "ring" },
	contributes = {
		str = { "add", "15", when = "ring == RINGNBL_ALLSTAT", at = 6829 }, agi = { "add", "15", when = "ring == RINGNBL_ALLSTAT", at = 6902 },
		vit = { "add", "15", when = "ring == RINGNBL_ALLSTAT", at = 6963 }, int = { "add", "15", when = "ring == RINGNBL_ALLSTAT", at = 7043 },
		dex = { "add", "15", when = "ring == RINGNBL_ALLSTAT", at = 7116 }, luk = { "add", "15", when = "ring == RINGNBL_ALLSTAT", at = 7177 },
		batk = { "pct_add", "20", when = "ring == RINGNBL_ATKRATE", at = 7342 },
		hit = { "add", "50", when = "ring == RINGNBL_HIT", at = 7611 },
		flee = { "add", "50", when = "ring == RINGNBL_FLEE", at = 7689 },
		aspd_pct = { "add", "20", when = "ring == RINGNBL_ASPDRATE", at = 8368 },
		hp_rate = { "add", "30", when = "ring == RINGNBL_HPRATE", at = 3268 },
		sp_rate = { "add", "30", when = "u.is_pc && ring == RINGNBL_SPRATE", at = 3391 },
	},
})

define("npc_hallucinationwalk", {
	fields = { val2 = "flee" },
	contributes = { flee = { "add", "flee", at = 7678 } },
})

define("nyanggrass", { -- a player gets 0; everything else halves (an early return: the result is narrowed, not capped)
	note = "Early returns: players get 0, everything else gets def / 2 and mdef / 2 without the int16 cap (status.cpp:7786-7790, 7956-7960).",
	contributes = {
		def = { { "ret", "0", when = "u.is_pc", at = 7786 }, { "div_ret", "2", when = "u.is_pc == 0", at = 7786 } },
		mdef = { { "ret", "0", when = "u.is_pc", at = 7956 }, { "div_ret", "2", when = "u.is_pc == 0", at = 7956 } },
	},
})

define("odins_power", {
	fields = { val1 = "level" },
	contributes = {
		def = { "sub", "20 * level", at = 7807 },
		matk_flat = { "add", "40 + 30 * level", at = 7491 },
		mdef = { "sub", "20 * level", at = 7983 },
		watk = { "add", "40 + 30 * level", at = 7425 },
	},
})

define("onehand", {
	fields = { val2 = "aspd_rate" },
	contributes = { aspd_fixed = { "quicken7", at = 8270 }, aspd_rate = { "max_cand", "aspd_rate", at = 8442 } },
})

define("overcoming_crisis", {
	fields = { val2 = "patk_smatk", val3 = "hp" },
	contributes = {
		hp_fix = { "add", "hp", at = 3221 },
		patk = { "add", "patk_smatk", at = 8595 },
		smatk = { "add", "patk_smatk", at = 8632 },
	},
})

define("overed_boost", { -- homunculus Overed Boost: FLEE and ASPD become fixed values
	note = "FLEE becomes val2 and amotion a fixed value regardless of every other status (status.cpp:7648, 8401).",
	fields = { val2 = "flee", val3 = "aspd", val4 = "def_pct" },
	contributes = {
		def = { "pct_sub", "def_pct", when = "u.is_hom", at = 7853 },
		fix_aspd = { "set_end", "AMOTION_ZERO_ASPD - aspd * AMOTION_INTERVAL", at = 8401 },
		flee = { "ret", "flee", at = 7648 },
	},
})

define("paralyse", { -- val3: 0 = the walking half of the status, 1 = the debuff half
	fields = { val3 = "phase" },
	contributes = {
		aspd_pct = { "sub", "10", when = "phase == 1", at = 8338 },
		aspd_rate = { "add", "100", when = "phase == 1", at = 8532 },
		flee = { "pct_sub", "10", when = "phase == 1", at = 7706 },
		speed_rate = { "haste", "50", when = "phase == 0", at = 8190 },
		speed = { "paralyse_slow", when = "phase == 1", at = 8232 },
	},
})

define("paralysis", {
	fields = { val2 = "def2_pct" },
	contributes = { def2 = { "pct_sub", "def2_pct", at = 7923 } },
})

define("partyflee", {
	fields = { val1 = "level" },
	contributes = { flee = { "add", "level * 10", at = 7672 } },
})

define("petrology_option", {
	fields = { val2 = "hp_rate" },
	contributes = { hp_rate = { "add", "hp_rate", at = 3257 } },
})

define("poison", {
	contributes = {
		def = { "scale", "75", when = "u.is_pc == 0", group = "poison", at = 7817 },
		def2 = { "scale", "75", group = "poison", at = 7908 },
	},
})

define("poison_mist", { contributes = { flee = { "ret", "0", at = 7646 } } })

define("poison_shield_option", {
	contributes = { def_ele = { "ret", "ELE_POISON", at = 8803 }, ele_lv = { "ret", "1", at = 8843 } },
})

define("powerful_faith", {
	fields = { val2 = "watk", val3 = "patk" },
	contributes = { patk = { "add", "patk", at = 8574 }, watk = { "add", "watk", at = 7437 } },
})

define("powerup", {
	fields = { val2 = "hit_pct" },
	contributes = { hit = { "pct_add", "hit_pct", at = 7588 } },
})

define("power_of_gaia", {
	fields = { val2 = "def_speed_pct", val3 = "hp_rate" },
	contributes = {
		def = { "pct_add", "def_speed_pct", at = 7849 },
		hp_rate = { "add", "hp_rate", at = 3259 },
		speed_rate = { "slow", "def_speed_pct", at = 8139 },
	},
})

define("prestige", {
	fields = { val3 = "def" },
	contributes = { def = { "add", "def", at = 7837 } },
})

define("pre_acies", {
	fields = { val2 = "crate" },
	contributes = { crate = { "add", "crate", at = 8720 } },
})

define("promote_health_reserch", {
	fields = { val3 = "hp" },
	contributes = { hp_fix = { "add", "hp", at = 3203 } },
})

define("pron_march", {
	fields = { val2 = "patk" },
	contributes = { patk = { "add", "patk", at = 8580 } },
})

define("provoke", {
	fields = { val1 = "level", val2 = "atk_pct", val3 = "def_pct" },
	contributes = {
		def = { "pct_sub", "def_pct", when = "u.is_pc == 0", at = 7825 },
		def2 = { "pct_sub", "def_pct", at = 7912 },
	},
})

define("push_cart", { -- NEW_CARTS: pc_iscarton(sd) is this status (pc.hpp:1181)
	contributes = { speed = { "cart", at = 8230 } },
})

define("pyroclastic", {
	fields = { val2 = "atk" },
	contributes = {
		batk = { "add", "atk", when = "u.is_hom", at = 7331 },
		watk = { "add", "atk", when = "u.is_pc", at = 7421 },
	},
})

define("pyrotechnic_option", {
	fields = { val2 = "watk" },
	contributes = { watk = { "add", "watk", at = 7413 } },
})

define("quagmire", {
	fields = { val1 = "level", val2 = "stat_loss" },
	contributes = {
		agi = { "sub", "stat_loss", at = 6871 },
		aspd_fixed = { "no_quicken", at = 8268 },
		dex = { "sub", "stat_loss", at = 7079 },
		speed_rate = { "slow", "50", at = 8103 },
	},
})

define("raisingdragon", {
	fields = { val1 = "rate" },
	contributes = { hp_rate = { "add", "rate", at = 3251 }, sp_rate = { "add", "rate", at = 3402 } },
})

define("rebound", { contributes = { speed_rate = { "slow", "25", at = 8143 } } })

define("religio", {
	fields = { val2 = "trait_bonus" },
	contributes = {
		sta = { "add", "trait_bonus", at = 7221 }, wis = { "add", "trait_bonus", at = 7241 }, spl = { "add", "trait_bonus", at = 7261 },
	},
})

define("rock_crusher", {
	fields = { val2 = "def_pct" },
	contributes = { def = { "pct_sub", "def_pct", at = 7847 } },
})

define("rock_crusher_atk", {
	fields = { val2 = "speed_pct" },
	contributes = { speed_rate = { "slow", "speed_pct", at = 8137 } },
})

define("run", { contributes = { speed_rate = { "haste", "55", at = 8182 } } })

define("rushwindmill", {
	fields = { val3 = "watk" },
	contributes = { speed_rate = { "haste", "25", at = 8206 }, watk = { "add", "watk", at = 7404 } },
})
