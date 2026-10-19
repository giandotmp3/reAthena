--!nonstrict
-- Status definitions, S to Z (hand-written from src/map/status.cpp; see a_c.luau for the conventions).
local status = import("scripts/status/status")
local define = status.define

define("sandy_festival", {
	fields = { val2 = "trait_bonus" },
	contributes = {
		sta = { "add", "trait_bonus", at = 7223 }, wis = { "add", "trait_bonus", at = 7243 }, spl = { "add", "trait_bonus", at = 7263 },
	},
})

define("saturdaynightfever", {
	fields = { val1 = "level" },
	contributes = { flee = { "sub", "20 + 30 * level", at = 7714 }, hit = { "sub", "50 + 50 * level", at = 7616 } },
})

define("service4u", {
	fields = { val2 = "sp_rate" },
	contributes = { sp_rate = { "add", "sp_rate", at = 3404 } },
})

define("shadowweapon", { contributes = { atk_ele = { "ret", "ELE_DARK", at = 8879 } } })

define("shadow_clock", { contributes = { speed_rate = { "haste", "50", at = 8215 } } })

define("shadow_strip", {
	fields = { val2 = "pct" },
	contributes = {
		mres = { "pct_sub", "pct", when = "u.is_pc == 0", at = 8682 },
		res = { "pct_sub", "pct", when = "u.is_pc == 0", at = 8658 },
	},
})

define("shapeshift", {
	fields = { val2 = "element" },
	contributes = { def_ele = { "ret", "element", at = 8811 }, ele_lv = { "ret", "1", at = 8839 } },
})

define("shieldchainrush", { contributes = { speed_rate = { "slow", "20", at = 8153 } } })

define("shieldspell_atk", {
	fields = { val2 = "atk" },
	contributes = { matk_flat = { "add", "atk", at = 7483 }, watk = { "add", "atk", at = 7394 } },
})

define("signumcrucis", {
	fields = { val2 = "def_pct" },
	contributes = { def = { "pct_sub", "def_pct", at = 7819 } },
})

define("sincere_faith", {
	fields = { val2 = "aspd" },
	contributes = { fix_aspd = { "sub", "10 * aspd", at = 8412 } },
})

define("ska", {
	contributes = { def2 = { "add", "80", at = 7897 }, mdef2 = { "ret", "90", at = 8013 } },
})

define("ske", {
	contributes = { def = { "div", "2", at = 7823 }, def2 = { "pct_sub", "50", at = 7910 } },
})

define("slowdown", { -- Slow Potion
	fields = { val1 = "speed_pct" },
	contributes = { speed_rate = { "slow", "speed_pct", at = 8119 } },
})

define("smokepowder", { contributes = { flee = { "pct_add", "20", at = 7726 } } })

define("solid_skin_option", {
	fields = { val2 = "def_pct" },
	contributes = { def = { "pct_add", "def_pct", at = 7845 }, hp_fix = { "add", "2000", at = 3205 } },
})

define("soulcold", {
	fields = { val1 = "agi" },
	contributes = { agi = { "add", "agi", at = 6861 } },
})

define("soulfairy", {
	fields = { val2 = "matk" },
	contributes = { matk_flat = { "add", "matk", at = 7479 } },
})

define("soulfalcon", {
	fields = { val2 = "watk", val3 = "hit" },
	contributes = { hit = { "add", "hit", at = 7614 }, watk = { "add", "watk", at = 7435 } },
})

define("soulgolem", {
	fields = { val2 = "def", val3 = "mdef" },
	contributes = { def = { "add", "def", at = 7855 }, mdef = { "add", "mdef", at = 7985 } },
})

define("soulshadow", {
	fields = { val2 = "aspd", val3 = "cri" },
	contributes = { cri = { "add", "10 * cri", at = 7548 }, fix_aspd = { "sub", "10 * aspd", at = 8408 } },
})

define("spearquicken", {
	fields = { val1 = "level", val2 = "aspd_rate" },
	contributes = {
		aspd_fixed = { "quicken7", at = 8270 },
		aspd_rate = { "max_cand", "aspd_rate", at = 8458 },
		cri = { "add", "3 * level * 10", at = 7539 },
		flee = { "add", "2 * level", at = 7687 },
	},
})

define("speed", {
	fields = { val1 = "level" },
	contributes = { flee = { "add", "10 + level * 10", at = 7668 } },
})

define("spell_enchanting", {
	fields = { val2 = "smatk" },
	contributes = { smatk = { "add", "smatk", at = 8619 } },
})

define("spiderweb", { contributes = { flee = { "pct_sub", "50", group = "web", at = 7698 } } })

-- Soul Link: val2 is the link type (SL_HIGH = the high-class link that lends stats), val3 / val4 pack the lent stats as in Marionette
define("spirit", {
	note = "val2 selects the soul link: SL_HIGH lends the packed stats of val3 / val4; SL_BARDDANCER lowers the dance penalty of speed.",
	fields = { val1 = "level", val2 = "link", val3 = "str_agi_vit", val4 = "int_dex_luk" },
	contributes = {
		str = { "add", "(str_agi_vit >> 16) & 0xFF", when = "link == SL_HIGH", at = 6808 },
		agi = { "add", "(str_agi_vit >> 8) & 0xFF", when = "link == SL_HIGH", at = 6879 },
		vit = { "add", "str_agi_vit & 0xFF", when = "link == SL_HIGH", at = 6942 },
		int = { "add", "(int_dex_luk >> 16) & 0xFF", when = "link == SL_HIGH", at = 7011 },
		dex = { "add", "(int_dex_luk >> 8) & 0xFF", when = "link == SL_HIGH", at = 7093 },
		luk = { "add", "int_dex_luk & 0xFF", when = "link == SL_HIGH", at = 7156 },
		speed_rate = { "dance_bard", when = "link == SL_BARDDANCER", at = 8099 }, -- the dance penalty of Dancing is smaller for a linked Bard / Dancer
	},
})

define("spurt", { contributes = { str = { "add", "10", at = 6794 } } })

define("sp_sha", {
	fields = { val2 = "speed_pct" },
	contributes = { speed_rate = { "slow", "speed_pct", at = 8149 } },
})

define("starstance", {
	fields = { val2 = "aspd" },
	contributes = { aspd_pct = { "add", "aspd", at = 8370 }, aspd_rate = { "sub", "10 * aspd", at = 8556 } },
})

define("star_comfort", {
	fields = { val1 = "level", val2 = "aspd_rate" },
	contributes = { aspd_pct = { "add", "3 * level", at = 8362 }, aspd_rate = { "max_assign", "aspd_rate", at = 8435 } },
})

define("stealthfield", { contributes = { speed_rate = { "slow", "20", at = 8133 } } })

define("steelbody", {
	contributes = {
		aspd_pct = { "sub", "25", at = 8318 },
		aspd_rate = { "add", "250", at = 8512 },
		speed = { "steelbody", at = 8236 },
	},
})

define("stomachache", {
	fields = { val1 = "stat_loss" },
	contributes = {
		str = { "sub", "stat_loss", at = 6816 }, agi = { "sub", "stat_loss", at = 6887 }, vit = { "sub", "stat_loss", at = 6946 },
		int = { "sub", "stat_loss", at = 7019 }, dex = { "sub", "stat_loss", at = 7097 }, luk = { "sub", "stat_loss", at = 7160 },
	},
})

define("stone", {
	contributes = {
		def = { "div", "2", at = 7813 },
		def_ele = { "ret", "ELE_EARTH", at = 8797 },
		ele_lv = { "ret", "1", at = 8831 },
		mdef = { "pct_add", "25", at = 7971 },
	},
})

define("stonehardskin", {
	fields = { val1 = "def_mdef" },
	contributes = { def = { "add", "def_mdef", at = 7811 }, mdef = { "add", "def_mdef", at = 7969 } },
})

define("stone_wall", {
	fields = { val2 = "def", val3 = "mdef" },
	contributes = { def = { "add", "def", at = 7857 }, mdef = { "add", "mdef", at = 7987 } },
})

define("striking", {
	fields = { val2 = "watk" },
	contributes = { watk = { "add", "watk", at = 7402 } },
})

define("striparmor", {
	fields = { val2 = "vit_pct" },
	contributes = { vit = { "pct_sub", "vit_pct", when = "u.is_pc == 0", at = 6954 } },
})

define("striphelm", {
	fields = { val2 = "int_pct" },
	contributes = { int = { "pct_sub", "int_pct", when = "u.is_pc == 0", at = 7037 } },
})

define("stripshield", {
	fields = { val2 = "def_pct" },
	contributes = { def = { "pct_sub", "def_pct", when = "u.is_pc == 0", at = 7827 } },
})

define("strong_protection_option", {
	contributes = { def_ele = { "ret", "ELE_EARTH", at = 8797 }, ele_lv = { "ret", "1", at = 8843 } },
})

define("suiton", {
	note = "AGI is reduced only while val3 (the speed slow) is non-zero.",
	fields = { val2 = "agi_loss", val3 = "speed_pct" },
	contributes = {
		agi = { "sub", "agi_loss", when = "speed_pct != 0", at = 6873 },
		speed_rate = { "slow", "speed_pct", at = 8123 },
	},
})

define("sunstance", {
	fields = { val2 = "atk_pct" },
	contributes = { batk = { "pct_add", "atk_pct", at = 7345 }, watk = { "pct_add", "atk_pct", at = 7433 } },
})

define("sun_comfort", {
	fields = { val2 = "def2" },
	contributes = { def2 = { "add", "def2", at = 7894 } },
})

define("swingdance", {
	fields = { val3 = "aspd_speed" },
	contributes = {
		aspd_pct = { "add", "aspd_speed", at = 8346 },
		aspd_rate = { "sub", "aspd_speed * 10", at = 8540 },
		speed_rate = { "haste", "aspd_speed", at = 8196 },
	},
})

define("swoo", { contributes = { speed_rate = { "slow", "300", at = 8125 } } })

define("swordclan", {
	contributes = {
		str = { "add", "1", at = 6820 }, vit = { "add", "1", at = 6950 },
		hp_fix = { "add", "30", at = 3209 }, sp_fix = { "add", "10", at = 3367 },
	},
})

define("symphonyoflover", {
	fields = { val3 = "mdef_pct" },
	contributes = { mdef = { "pct_add", "mdef_pct", at = 7981 } },
})

define("talisman_of_magician", {
	fields = { val2 = "smatk" },
	contributes = { smatk = { "add", "smatk", at = 8624 } },
})

define("talisman_of_warrior", {
	fields = { val2 = "patk" },
	contributes = { patk = { "add", "patk", at = 8589 } },
})

define("teargas", { contributes = { flee = { "pct_sub", "50", at = 7728 }, hit = { "pct_sub", "50", at = 7600 } } })

define("tempering", {
	fields = { val2 = "patk" },
	contributes = { patk = { "add", "patk", at = 8582 } },
})

define("temporary_communion", {
	fields = { val2 = "bonus" },
	contributes = {
		hplus = { "add", "bonus", at = 8702 }, patk = { "add", "bonus", at = 8591 }, smatk = { "add", "bonus", at = 8628 },
	},
})

define("tidal_weapon", {
	fields = { val2 = "watk_pct" },
	contributes = { atk_ele = { "ret", "ELE_WATER", at = 8883 }, watk = { "pct_add", "watk_pct", at = 7419 } },
})

define("tidal_weapon_option", { contributes = { atk_ele = { "ret", "ELE_WATER", at = 8883 } } })

define("tinder_breaker", { contributes = { flee = { "pct_sub", "50", group = "tinder", at = 7718 } } })
define("tinder_breaker2", { contributes = { flee = { "pct_sub", "50", group = "tinder", at = 7718 } } })

define("toxin_of_mandara", {
	fields = { val2 = "res_loss" },
	contributes = { res = { "sub", "res_loss", at = 8662 } },
})

define("tropic_option", {
	fields = { val2 = "watk" },
	contributes = { watk = { "add", "watk", at = 7417 } },
})

define("truesight", {
	fields = { val1 = "level", val2 = "cri", val3 = "hit" },
	contributes = {
		str = { "add", "5", at = 6792 }, agi = { "add", "5", at = 6863 }, vit = { "add", "5", at = 6936 },
		int = { "add", "5", at = 6997 }, dex = { "add", "5", at = 7077 }, luk = { "add", "5", at = 7148 },
		cri = { "add", "cri", at = 7534 },
		hit = { "add", "hit", at = 7572 },
	},
})

define("twohandquicken", {
	fields = { val1 = "level", val2 = "aspd_rate" },
	contributes = {
		aspd_fixed = { "quicken7", at = 8270 },
		aspd_rate = { "max_cand", "aspd_rate", at = 8438 },
		cri = { "add", "(2 + level) * 10", at = 7541 },
		hit = { "add", "level * 2", at = 7607 },
	},
})

define("t_fifth_god", {
	fields = { val2 = "smatk" },
	contributes = { smatk = { "add", "smatk", at = 8626 } },
})

define("universestance", {
	fields = { val2 = "stat_bonus" },
	contributes = {
		str = { "add", "stat_bonus", at = 6832 }, agi = { "add", "stat_bonus", at = 6905 }, vit = { "add", "stat_bonus", at = 6966 },
		int = { "add", "stat_bonus", at = 7033 }, dex = { "add", "stat_bonus", at = 7119 }, luk = { "add", "stat_bonus", at = 7180 },
	},
})

define("upheaval_option", {
	fields = { val2 = "hp_rate" },
	contributes = { hp_rate = { "add", "hp_rate", at = 3263 } },
})

define("venombleed", {
	fields = { val3 = "phase" },
	contributes = { hp_rate = { "sub", "15", when = "phase == 1", at = 3279 } },
})

define("violentgale", {
	fields = { val2 = "flee" },
	contributes = { flee = { "add", "flee", at = 7658 } },
})

define("volcano", {
	fields = { val2 = "atk" },
	contributes = {
		batk = { "add", "atk", at = 7321 },
		matk_flat = { "add", "atk", at = 7468 },
		watk = { "add", "atk", when = "u.is_mob", at = 7373 },
	},
})

define("walkspeed", {
	fields = { val1 = "speed_pct" },
	contributes = { speed = { "walkspeed", "speed_pct", at = 8242 } },
})

define("waterweapon", { contributes = { atk_ele = { "ret", "ELE_WATER", at = 8863 } } })

define("water_barrier", {
	fields = { val2 = "loss" },
	contributes = { flee = { "sub", "loss", at = 7680 }, watk = { "sub", "loss", at = 7377 } },
})

define("water_insignia", {
	fields = { val1 = "insignia_type" },
	contributes = {
		atk_ele = { "ret", "ELE_WATER", when = "insignia_type == 2", at = 8863 },
		watk = { "pct_add", "10", when = "insignia_type == 2", group = "insignia", at = 7408 },
	},
})

define("watkfood", {
	fields = { val1 = "watk" },
	contributes = { watk = { "add", "watk", at = 7371 } },
})

define("wedding", { contributes = { speed_rate = { "slow", "100", at = 8111 } } })

define("whistle", {
	fields = { val2 = "flee", val3 = "flee2" },
	contributes = { flee = { "add", "flee", at = 7654 }, flee2 = { "add", "flee2", at = 7750 } },
})

define("wideweb", { contributes = { flee = { "pct_sub", "50", group = "web", at = 7698 } } })

define("wild_storm_option", { contributes = { fix_aspd = { "sub", "50", group = "gust", at = 8404 } } })

define("wild_walk", {
	fields = { val2 = "speed_pct", val3 = "flee" },
	contributes = { flee = { "add", "flee", at = 7684 }, speed_rate = { "haste", "speed_pct", at = 8213 } },
})

define("windwalk", {
	fields = { val1 = "level", val2 = "flee" },
	contributes = { flee = { "add", "flee", at = 7656 }, speed_rate = { "haste", "2 * level", at = 8172 } },
})

define("windweapon", { contributes = { atk_ele = { "ret", "ELE_WIND", at = 8872 } } })

define("wind_insignia", {
	fields = { val1 = "insignia_type" },
	contributes = {
		aspd_pct = { "add", "10", when = "insignia_type == 2", at = 8364 },
		aspd_rate = { "sub", "100", when = "insignia_type == 2", at = 8554 },
		atk_ele = { "ret", "ELE_WIND", when = "insignia_type == 2", at = 8872 },
		watk = { "pct_add", "10", when = "insignia_type == 2", group = "insignia", at = 7408 },
	},
})

define("wind_step_option", {
	fields = { val2 = "pct" },
	contributes = { flee = { "pct_add", "pct", at = 7716 }, speed_rate = { "haste", "pct", at = 8198 } },
})

define("zangetsu", {
	fields = { val2 = "batk", val3 = "matk" },
	contributes = { batk = { "add", "batk", at = 7337 }, matk_flat = { "add", "matk", at = 7485 } },
})

define("zephyr", {
	fields = { val2 = "flee" },
	contributes = { flee = { "add", "flee", at = 7720 } },
})

-- the shadow-chaser / guillotine-cross "_" skills (their status records have no leading underscore)
define("autoshadowspell", {
	fields = { val4 = "matk" },
	contributes = { matk_flat = { "add", "matk", at = 7489 } },
})

define("bodypaint", {
	fields = { val1 = "level" },
	contributes = { aspd_pct = { "sub", "5 * level", at = 8340 }, aspd_rate = { "add", "50 * level", at = 8534 } },
})

define("enervation", {
	fields = { val2 = "atk_pct" },
	contributes = { batk = { "pct_sub", "atk_pct", at = 7335 }, watk = { "pct_sub", "atk_pct", at = 7400 } },
})

define("groomy", {
	fields = { val2 = "aspd", val3 = "hit_pct" },
	contributes = {
		aspd_pct = { "sub", "aspd", at = 8344 },
		aspd_rate = { "add", "aspd * 10", at = 8538 },
		hit = { "pct_sub", "hit_pct", at = 7594 },
	},
})

define("invisibility", {
	fields = { val2 = "aspd", val3 = "cri" },
	contributes = {
		aspd_pct = { "sub", "aspd", at = 8342 },
		aspd_rate = { "add", "aspd * 10", at = 8536 },
		atk_ele = { "ret", "ELE_GHOST", at = 8881 },
		cri = { "add", "cri * 10", at = 7544 },
		ele_lv = { "ret", "1", at = 8841 },
	},
})

define("laziness", {
	fields = { val3 = "flee_pct" },
	contributes = { flee = { "pct_sub", "flee_pct", at = 7710 }, speed_rate = { "slow", "25", at = 8135 } },
})

define("stripaccessory", {
	fields = { val2 = "pct" },
	contributes = {
		dex = { "pct_sub", "pct", when = "u.is_pc == 0", at = 7107 },
		int = { "pct_sub", "pct", when = "u.is_pc == 0", at = 7039 },
		luk = { "pct_sub", "pct", when = "u.is_pc == 0", at = 7164 },
	},
})

define("unlucky", {
	fields = { val2 = "pct" },
	contributes = { cri = { "sub", "pct", at = 7546 }, flee2 = { "pct_sub", "pct", at = 7752 } },
})

define("weakness", {
	fields = { val2 = "hp_rate_loss" },
	contributes = { hp_rate = { "sub", "hp_rate_loss", at = 3283 } },
})
