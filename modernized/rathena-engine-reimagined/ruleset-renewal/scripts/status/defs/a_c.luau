--!nonstrict
-- Status definitions, A to C (hand-written from src/map/status.cpp; `at` is the line of the legacy statement, see scripts/status/status.luau).
-- Field names: the val1..val4 slots as the legacy code reads them (and, where it assigns them in status_change_start, as it computes them);
-- a slot that no status_calc_* function reads is listed only when its meaning is known (docs/STATUS_FIELDS.md).
local status = import("scripts/status/status")
local define = status.define

define("abyss_slayer", {
	fields = { val1 = "level", val2 = "patk_smatk", val3 = "hit" }, -- status_change_start 12907: val2 = 10 + 2 * lv, val3 = 100 + 20 * lv
	contributes = {
		hit = { "add", "hit", at = 7618 },
		patk = { "add", "patk_smatk", at = 8578 },
		smatk = { "add", "patk_smatk", at = 8615 },
	},
})

define("acceleration", { -- Madogear: -25 % walk delay on top of the license bonus
	contributes = { speed = { "acceleration", at = 8054 } },
})

define("adjustment", {
	contributes = {
		flee = { "add", "30", at = 7666 },
		hit = { "sub", "30", at = 7580 },
	},
})

define("adoramus", {
	fields = { val1 = "level", val2 = "agi_loss" }, -- 10875: val2 = 2 + lv
	contributes = { agi = { "sub", "agi_loss", at = 6881 } },
})

define("adrenaline", {
	fields = { val1 = "level", val2 = "self_cast", val3 = "aspd_rate" }, -- 11624: val3 = self_cast ? 300 : 200
	contributes = {
		aspd_fixed = { "quicken7", at = 8270 },
		aspd_rate = { "max_cand", "aspd_rate", at = 8454 },
		hit = { "add", "level * 3 + 5", at = 7609 },
	},
})

define("adrenaline2", {
	fields = { val1 = "level", val2 = "self_cast", val3 = "aspd_rate" },
	contributes = {
		aspd_fixed = { "quicken6", at = 8272 },
		aspd_rate = { "max_cand", "aspd_rate", at = 8450 },
	},
})

define("agiup", {
	note = "No status_change_start case: the item script passes val1 (speed %) and val2 (flee %) itself.",
	fields = { val1 = "speed_pct", val2 = "flee_pct" }, -- no status_change_start case: the item script passes both
	contributes = {
		flee = { "pct_add", "flee_pct", at = 7696 },
		speed_rate = { "haste", "speed_pct", at = 8168 },
	},
})

define("ain_rhapsody", {
	fields = { val1 = "level", val2 = "res_loss", val3 = "partner_flags" }, -- 12920: val2 = 10 * lv (x2 when val3 & 2)
	contributes = { res = { "sub", "res_loss", at = 8660 } },
})

define("all_riding", { -- rental mount: the speed bonus is battle_config.rental_mount_speed_boost
	contributes = { speed_rate = { "haste2_allriding", at = 8077 } },
})

define("all_stat_down", {
	fields = { val1 = "level", val2 = "stat_loss" }, -- 11236: val2 = 20 * lv (-10 below the skill's max level)
	contributes = {
		str = { "sub", "stat_loss", at = 6834 }, agi = { "sub", "stat_loss", at = 6907 }, vit = { "sub", "stat_loss", at = 6968 },
		int = { "sub", "stat_loss", at = 7046 }, dex = { "sub", "stat_loss", at = 7121 }, luk = { "sub", "stat_loss", at = 7182 },
	},
})

define("analyze", {
	fields = { val1 = "level" },
	contributes = {
		def = { "pct_sub", "14 * level", at = 7833 }, def2 = { "pct_sub", "14 * level", at = 7919 },
		mdef = { "pct_sub", "14 * level", at = 7979 }, mdef2 = { "pct_sub", "14 * level", at = 8022 },
	},
})

define("angelus", {
	note = "Renewal: the DEF2 term is a fraction of the unit's VIT (the battle status VIT), not of the DEF2 being built.",
	fields = { val1 = "level", val2 = "def_pct" }, -- 11642: val2 = 5 * lv
	contributes = {
		def2 = { "add", "u.vit / 2 * def_pct / 100", at = 7900 }, -- RENEWAL: the VIT bonus of the soft DEF is boosted (the unit's battle VIT)
		hp_fix = { "add", "level * 50", at = 3218 },
	},
})

define("angriffs_modus", {
	fields = { val1 = "level", val2 = "atk_bonus", val3 = "flee_loss", val4 = "tick_count" }, -- 12401: 50 + 20 * lv, 25 + 10 * lv
	contributes = {
		batk = { "add", "atk_bonus", at = 7333 },
		def = { "sub", "20 + 10 * level", at = 7809 },
		flee = { "sub", "flee_loss", at = 7664 },
		hp_rate = { "add", "5 * level", at = 3255 },
		watk = { "pct_add", "atk_bonus", at = 7423 },
	},
})

define("appleidun", {
	fields = { val1 = "level", val2 = "hp_rate", val3 = "potion_rate" }, -- 11130
	contributes = { hp_rate = { "add", "hp_rate", at = 3233 } },
})

define("aquaplay_option", {
	fields = { val2 = "matk" }, -- 12231: 40
	contributes = { matk_flat = { "add", "matk", at = 7473 } },
})

define("arclousedash", {
	fields = { val1 = "level", val2 = "agi", val3 = "speed_pct", val4 = "doram_ranged_atk" }, -- 12644
	contributes = {
		agi = { "add", "agi", at = 6897 },
		speed_rate = { "haste", "speed_pct", at = 8202 },
	},
})

define("arcwandclan", {
	contributes = {
		dex = { "add", "1", at = 7101 }, int = { "add", "1", at = 7023 },
		hp_fix = { "add", "30", at = 3211 }, sp_fix = { "add", "10", at = 3369 },
	},
})

define("armor", { -- Armor of the weapon-shield stance: damage divided by val2, range val3, type val4
	fields = { val2 = "damage_divisor", val3 = "range_flags", val4 = "type_flags" }, -- 11760
	contributes = { speed = { "min200", at = 8240 } },
})

define("ash", {
	fields = { val2 = "hit_pct", val3 = "def_pct", val4 = "atk_flee_pct" }, -- 12465-12473: 50 each, or 0 against a boss
	contributes = {
		batk = { "pct_sub", "atk_flee_pct", at = 7329 },
		def = { "pct_sub", "def_pct", at = 7851 },
		def2 = { "pct_sub", "def_pct", at = 7921 },
		flee = { "pct_sub", "atk_flee_pct", at = 7722 },
		hit = { "pct_sub", "hit_pct", at = 7598 },
	},
})

for i, rank in ipairs({ 4, 3, 2, 1 }) do -- ASPDPOTION0..3: the first present of 3, 2, 1, 0 applies (status.cpp:8306, 8497)
	define("aspdpotion" .. (i - 1), {
		note = "ASPD potions: only the best present one (3 > 2 > 1 > 0) applies, to both the fixed ASPD and the rate.",
		fields = { val1 = "aspd_bonus", val2 = "rate_bonus" }, -- 11156: val2 = 50 * (2 + type - ASPDPOTION0)
		contributes = {
			aspd_fixed = { "potion", "aspd_bonus", at = 8306, group = "aspdpotion", rank = rank },
			aspd_rate = { "potion", "rate_bonus", at = 8497, group = "aspdpotion", rank = rank },
		},
	})
end

define("aspersio", { contributes = { atk_ele = { "ret", "ELE_HOLY", at = 8877 } } })

define("assncros", {
	fields = { val1 = "level", val2 = "aspd_bonus" }, -- 11123: lv < 10 ? 2 * lv - 1 : 20
	contributes = {
		aspd_fixed = { "assncros", "aspd_bonus", at = 8278 },
		aspd_rate = { "assncros_max", "aspd_bonus", at = 8474 },
	},
})

define("assumptio", {
	fields = { val1 = "level" },
	contributes = { def = { "add", "level * 50", at = 7798 } }, -- RENEWAL: Assumptio raises DEF (no damage reduction)
})

define("attack_stance", {
	fields = { val1 = "level", val2 = "def_loss", val3 = "patk_smatk" }, -- 12855: 40 * lv, 3 * lv
	contributes = {
		def = { "sub", "def_loss", at = 7865 },
		patk = { "add", "patk_smatk", at = 8584 },
		smatk = { "add", "patk_smatk", at = 8621 },
	},
})

define("avoid", {
	fields = { val1 = "level", val2 = "speed_pct" }, -- 11696: 40 * lv (self) or 10 * lv
	contributes = { speed_rate = { "haste", "speed_pct", at = 8184 } },
})

define("banana_bomb", { contributes = { luk = { "sub", "75", at = 7166 } } })

define("banding", {
	fields = { val1 = "level", val2 = "band_count" }, -- 12184: val2 = number of Royal Guards banded
	contributes = { def = { "add", "6 * level", when = "band_count > 1", at = 7839 } },
})

define("battleorders", {
	contributes = { str = { "add", "5", at = 6786 }, int = { "add", "5", at = 6995 }, dex = { "add", "5", at = 7073 } },
})

define("benedictio", {
	contributes = { def_ele = { "ret", "ELE_HOLY", at = 8805 }, ele_lv = { "ret", "1", at = 8833 } },
})

define("benedictum", {
	fields = { val1 = "level", val2 = "trait_bonus" }, -- 12879: 2 * lv
	contributes = {
		pow = { "add", "trait_bonus", at = 7201 }, con = { "add", "trait_bonus", at = 7281 }, crt = { "add", "trait_bonus", at = 7301 },
	},
})

define("berserk", {
	fields = { val1 = "level", val3 = "ticks", val4 = "damage_interval" }, -- 11384-11387
	contributes = {
		aspd_fixed = { "berserk", at = 8303 },
		aspd_rate = { "berserk", at = 8492 },
		def = { "ret", "0", at = 7774 }, def2 = { "ret", "0", at = 7887 },
		flee = { "pct_sub", "50", at = 7700 },
		hp_rate = { "add", "200", at = 3237 },
		mdef = { "ret", "0", at = 7949 }, mdef2 = { "ret", "0", at = 8011 },
		speed_rate = { "haste", "25", at = 8180 },
	},
})

define("beyondofwarcry", {
	note = "One slot, two meanings: val3 is the MaxHP reduction (hp_rate, 3281) and is also ADDED to the critical value (status.cpp:7550).",
	fields = { val1 = "level", val2 = "str_loss", val3 = "hp_rate_loss" }, -- 12139: 10 + 10 * lv, 4 * lv; the critical bonus reads val3 too
	contributes = {
		cri = { "add", "hp_rate_loss", at = 7550 },
		hp_rate = { "sub", "hp_rate_loss", at = 3281 },
		str = { "sub", "str_loss", at = 6812 },
	},
})

define("blast_option", {
	fields = { val2 = "unknown_20", val3 = "element" }, -- 12245: 20; ELE_WIND
	contributes = { fix_aspd = { "sub", "50", at = 8404, group = "gust" } }, -- Gust / Blast / Wild Storm option: +5 ASPD
})

define("blessing", {
	note = "val2 = 0 on demon and undead targets: the stat is halved instead of raised.",
	fields = { val1 = "level", val2 = "stat_bonus" }, -- 11590: val2 = lv; 0 on a demon / undead target: half the stat
	contributes = {
		str = { { "add", "stat_bonus", when = "stat_bonus != 0", at = 6798 }, { "div_sub", "2", when = "stat_bonus == 0", at = 6798 } },
		int = { { "add", "stat_bonus", when = "stat_bonus != 0", at = 6999 }, { "div_sub", "2", when = "stat_bonus == 0", at = 6999 } },
		dex = { { "add", "stat_bonus", when = "stat_bonus != 0", at = 7081 }, { "div_sub", "2", when = "stat_bonus == 0", at = 7081 } },
		hit = { "add", "level * 2", at = 7605 },
	},
})

define("blessing_of_m_creatures", {
	fields = { val2 = "patk_smatk" }, -- 13023: 10 * lv
	contributes = { patk = { "add", "patk_smatk", at = 8593 }, smatk = { "add", "patk_smatk", at = 8630 } },
})

define("blind", {
	contributes = { flee = { "pct_sub", "25", at = 7702 }, hit = { "pct_sub", "25", at = 7590 } },
})

define("burning", {
	fields = { val4 = "remaining_tick" }, -- 11189
	contributes = { mdef = { "pct_sub", "25", at = 7975 }, mdef2 = { "pct_sub", "25", at = 8020 } },
})

define("b_trap", {
	fields = { val1 = "level", val2 = "caster_id", val3 = "speed_pct" }, -- 12585: val3 = 25 * lv
	contributes = { speed_rate = { "slow", "speed_pct", at = 8145 } },
})

define("camouflage", {
	note = "Legacy quirk: val3 is never assigned (status_change_start 11970 sets only val4; camouflage.cpp:29 passes val1), so the two DEF terms (status.cpp:7843, 7927) always read 0.",
	-- val3 is never assigned by status_change_start / camouflage.cpp (sc_start with val1 only): the DEF terms below read 0 in practice
	fields = { val1 = "level", val3 = "def_steps", val4 = "ticks" },
	contributes = {
		def = { "pct_sub", "5 * def_steps", at = 7843 },
		def2 = { "pct_sub", "5 * def_steps", at = 7927 },
		speed_rate = { "slow", "25 * (5 - level)", when = "level > 2", at = 8131 },
	},
	state_when = { nomove = "level < 3" }, -- status.cpp:5631
})

define("cartboost", { contributes = { speed_rate = { "haste", "20", at = 8174 } } })

define("catnippowder", {
	fields = { val2 = "atk_matk_pct", val3 = "speed_pct", val4 = "level_bonus" }, -- 12630: -50, 25 * lv, base level / 12
	contributes = {
		speed_rate = { "slow", "speed_pct", at = 8147 },
		watk = { "pct_add", "atk_matk_pct", at = 7429 },
	},
})

define("change", {
	fields = { val1 = "level", val2 = "vit_bonus", val3 = "int_bonus" }, -- 11751: 30 * lv, 20 * lv
	contributes = { int = { "add", "int_bonus", at = 6993 }, vit = { "add", "vit_bonus", at = 6932 } },
})

define("changeundead", {
	contributes = { def_ele = { "ret", "ELE_UNDEAD", at = 8807 }, ele_lv = { "ret", "1", at = 8835 } },
})

define("chasewalk", {
	note = "val3 is signed: negative values (a Rogue soul link) are a speed bonus used by the first speed branch, non-negative ones are a slow.",
	fields = { val1 = "level", val2 = "sp_interval", val3 = "speed_adjust", val4 = "sp_cost" }, -- 11280: val3 = 35 - 5 * lv (-40 for a Soul-linked Rogue)
	contributes = {
		speed_rate = {
			{ "slow_chasewalk_neg", "speed_adjust", when = "u.is_pc && speed_adjust < 0", at = 8085 },
			{ "slow", "speed_adjust", at = 8109 },
		},
	},
})

define("chasewalk2", {
	fields = { val1 = "str_bonus" },
	contributes = { str = { "add", "str_bonus", at = 6784 } },
})

define("chattering", {
	fields = { val2 = "eatk_ematk" }, -- 12705: 100
	contributes = { watk = { "add", "eatk_ematk", at = 7431 } },
})

define("cheerup", {
	contributes = {
		str = { "add", "3", at = 6826 }, agi = { "add", "3", at = 6899 }, vit = { "add", "3", at = 6960 },
		int = { "add", "3", at = 7031 }, dex = { "add", "3", at = 7113 }, luk = { "add", "3", at = 7174 },
	},
})

define("chilly_air_option", {
	fields = { val2 = "matk", val3 = "skill" }, -- 12238: 120; MG_COLDBOLT
	contributes = { matk_flat = { "add", "matk", at = 7475 } },
})

define("climax_cryimp", {
	contributes = { def = { "add", "300", at = 7861 }, mdef = { "add", "100", at = 7989 } },
})

define("climax_des_hu", { contributes = { matk_flat = { "add", "100", at = 7497 } } })

define("cloaking", {
	note = "val4 bit 1 = next to a wall: the slow applies away from a wall, the haste next to one.",
	fields = { val1 = "level", val2 = "sp_interval", val4 = "flags" }, -- 11289; val4 & 1: next to a wall, & 2 / & 4: keeps cloak on attack / skill
	contributes = {
		cri = { "pct_add", "100", at = 7536 }, -- critical += critical
		speed_rate = {
			{ "slow", "(level < 3) * 300 + (level >= 3) * (30 - 3 * level)", when = "(flags & 1) == 0", at = 8115 },
			{ "haste", "(level >= 10) * 25 + (level < 10) * (3 * level - 3)", when = "(flags & 1) == 1", at = 8178 },
		},
	},
})

define("cloakingexceed", {
	fields = { val1 = "level", val2 = "hits", val3 = "speed_pct", val4 = "flags" }, -- 11911: (lv + 1) / 2, (lv - 1) * 10
	contributes = { speed_rate = { "haste", "speed_pct", at = 8188 } },
})

define("closeconfine", {
	fields = { val3 = "flee_bonus" },
	contributes = { flee = { "add", "flee_bonus", at = 7662 } },
})

define("competentia", {
	fields = { val2 = "patk_smatk" }, -- 12875: 10 * lv
	contributes = { patk = { "add", "patk_smatk", at = 8576 }, smatk = { "add", "patk_smatk", at = 8613 } },
})

define("concentrate", { -- Loud Exclamation counterpart for archers (Improve Concentration): a percentage of the stat above the card / item part
	note = "val3 / val4 are the player's item AGI / DEX bonus (sd->indexed_bonus.param_bonus) captured when the status started; the rate applies to the stat without it.",
	fields = { val1 = "level", val2 = "stat_pct", val3 = "agi_offset", val4 = "dex_offset" }, -- 11599: 2 + lv; val3 / val4 = the player's AGI / DEX item bonus
	contributes = {
		agi = { "pct_add_off", "stat_pct", arg = "agi_offset", when = "u.sc.quagmire == 0", at = 6857 },
		dex = { "pct_add_off", "stat_pct", arg = "dex_offset", when = "u.sc.quagmire == 0", at = 7069 },
	},
})

define("concentration", {
	fields = { val1 = "level", val2 = "atk_pct", val3 = "hit", val4 = "def_pct" }, -- 11632-11638
	contributes = {
		def = { "pct_sub", "def_pct", at = 7821 },
		hit = { "add", "hit", at = 7576 },
	},
})

define("cooler_option", {
	fields = { val2 = "matk", val3 = "element" }, -- 12234: 80; ELE_WATER
	contributes = { matk_flat = { "add", "matk", at = 7477 } },
})

define("creatingstar", {
	fields = { val4 = "ticks" },
	contributes = { speed_rate = { "slow", "90", at = 8151 } },
})

define("crossbowclan", {
	contributes = {
		agi = { "add", "1", at = 6891 }, dex = { "add", "1", at = 7103 },
		hp_fix = { "add", "30", at = 3215 }, sp_fix = { "add", "10", at = 3373 },
	},
})

define("crystal_armor_option", {
	contributes = { def_ele = { "ret", "ELE_WATER", at = 8795 }, ele_lv = { "ret", "1", at = 8843 } },
})

define("curse", {
	contributes = {
		luk = { "ret", "0", at = 7144 },
		speed_rate = { "slow", "300", at = 8107 },
	},
})

define("cursed_soil_option", {
	fields = { val2 = "hp_rate", val3 = "element" }, -- 12259: 10; ELE_EARTH
	contributes = { hp_rate = { "add", "hp_rate", at = 3261 } },
})

define("c_marker", {
	fields = { val1 = "level", val2 = "caster_id", val3 = "flee_loss", val4 = "ticks" }, -- 12590: val3 = 10
	contributes = { flee = { "sub", "flee_loss", at = 7682 } },
})
