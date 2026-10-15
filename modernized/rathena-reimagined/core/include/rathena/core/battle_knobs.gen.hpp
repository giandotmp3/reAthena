// GENERATED FILE - DO NOT EDIT.
// Regenerate with `python3 tools/codegen/battle_knobs.py`; CI runs it with --check.
//
// Sources: src/map/battle.cpp battle_data[] (626 live entries, 615 knobs), the shipped
// conf/battle/*.conf (read through conf/battle_athena.conf) and, for symbolic values:
//   src/config/core.hpp
//   src/common/mmo.hpp
//   src/map/path.hpp
//   src/map/map.hpp
//   src/map/mob.hpp
//   src/map/skill.hpp
//   src/map/status.hpp
//
// RATHENA_BATTLE_KNOBS_GENERATED(X)   X(Type, name, shipped_default, min, max)
//   Every battle_data[] knob not typed by hand in knobs.def.hpp, grouped by the conf file that
//   sets it. shipped_default is the conf/battle value; for knobs no conf file sets it is the
//   legacy RENEWAL code default. [min, max] is the legacy range over every build mode. The
//   trailing comment keeps the legacy expression where it was symbolic.
// RATHENA_BATTLE_KNOB_CONF_KEYS(K)     K(name, "conf.key") where the conf key is not an identifier.
// RATHENA_BATTLE_PRE_RENEWAL_DEFAULTS(P)  P(name, value): pre-renewal shipped default, where it
//   differs from the renewal one (only knobs that no conf file sets).
// RATHENA_BATTLE_KNOB_LEGACY(L)       L(name, "battle_config field", renewal_default,
//   pre_renewal_default, renewal_min, renewal_max, pre_renewal_min, pre_renewal_max, "conf file")
//   Legacy code values for every knob (hand-typed ones included), evaluated with VIP_ENABLE off
//   as shipped. Used by the drift test (core/tests/test_shipped_conf_parity.cpp).
//
// Knobs whose legacy [default, min, max] differs between RENEWAL and pre-renewal builds:
//   traps_setting: [2, 0, 2] renewal, [0, 0, 2] pre-renewal (#ifdef RENEWAL in battle_data[])
//   guild_skill_relog_type: [0, 0, 1] renewal, [1, 0, 1] pre-renewal (#ifdef RENEWAL in battle_data[])
//   natural_heal_weight_rate: [70, 0, 100] renewal, [50, 0, 100] pre-renewal (#ifdef RENEWAL in battle_data[])
//   open_box_weight_rate: [90, 0, 100] renewal, [70, 0, 100] pre-renewal (#ifdef RENEWAL in battle_data[])
//   item_enabled_npc: [0, 0, 1] renewal, [1, 0, 1] pre-renewal (#ifdef RENEWAL in battle_data[])
//   blacksmith_fame_refine_threshold: [10, 1, 20] renewal, [10, 1, 10] pre-renewal (header macros in [10, 1, MAX_REFINE])
//   feature.instance_allow_reconnect: [1, 0, 1] renewal, [0, 0, 1] pre-renewal (#ifdef RENEWAL in battle_data[])
//   hom_delay_reset_vaporize: [0, 0, 1] renewal, [1, 0, 1] pre-renewal (#ifdef RENEWAL in battle_data[])
//   hom_delay_reset_warp: [0, 0, 1] renewal, [1, 0, 1] pre-renewal (#ifdef RENEWAL in battle_data[])
//   alchemist_summon_setting: [12, 0, 15] renewal, [15, 0, 15] pre-renewal (#ifdef RENEWAL in battle_data[])
// Knobs whose legacy entry differs with VIP_ENABLE (off in the shipped src/config/core.hpp):
//   vip_storage_increase: [300, 0, MAX_STORAGE-MIN_STORAGE] = [300, 0, 300] with VIP_ENABLE, [300, 0, MAX_STORAGE] = [300, 0, 600] without
// Note: fame_refine_lv1: listed twice in battle_data[], identical; the first is used
#pragma once

// NOLINTBEGIN
#define RATHENA_BATTLE_KNOBS_GENERATED(X) \
	/* conf/battle/battle.conf */ \
	X(std::int32_t, mob_critical_rate, 100, 0, RATHENA_I32_MAX) /* legacy: 100, 0, INT_MAX */ \
	X(std::int32_t, critical_rate, 100, 0, RATHENA_I32_MAX) /* legacy: 100, 0, INT_MAX */ \
	X(std::int32_t, enable_baseatk, 9, 0, 4095) /* legacy: BL_CHAR|BL_HOM, BL_NUL, BL_ALL */ \
	X(std::int32_t, enable_baseatk_renewal, 671, 0, 4095) /* legacy: BL_ALL, BL_NUL, BL_ALL */ \
	X(std::int32_t, enable_perfect_flee, 1, 0, 4095) /* legacy: BL_PC|BL_PET, BL_NUL, BL_ALL */ \
	X(std::int32_t, player_damage_delay_rate, 100, 0, RATHENA_I32_MAX) /* legacy: 100, 0, INT_MAX */ \
	X(bool, attribute_recover, false, 0, 1) /* legacy: 1, 0, 1 */ \
	X(std::int32_t, pc_damage_walk_delay_rate, 20, 0, RATHENA_I32_MAX) /* legacy: 20, 0, INT_MAX */ \
	X(std::int32_t, damage_walk_delay_rate, 100, 0, RATHENA_I32_MAX) /* legacy: 100, 0, INT_MAX */ \
	X(std::int32_t, multihit_delay, 200, 0, RATHENA_I32_MAX) /* legacy: 80, 0, INT_MAX */ \
	X(std::int32_t, infinite_endure, 0, 0, 4095) /* legacy: BL_NUL, BL_NUL, BL_ALL */ \
	X(std::int32_t, arrow_decrement, 1, 0, 2) \
	X(bool, ammo_unequip, true, 0, 1) \
	X(bool, ammo_check_weapon, true, 0, 1) \
	X(std::int32_t, undead_detect_type, 0, 0, 3) \
	X(std::int32_t, agi_penalty_target, 1, 0, 4095) /* legacy: BL_PC, BL_NUL, BL_ALL */ \
	X(std::int32_t, vit_penalty_target, 1, 0, 4095) /* legacy: BL_PC, BL_NUL, BL_ALL */ \
	X(std::int32_t, weapon_defense_type, 0, 0, RATHENA_I32_MAX) /* legacy: 0, 0, INT_MAX */ \
	X(std::int32_t, magic_defense_type, 0, 0, RATHENA_I32_MAX) /* legacy: 0, 0, INT_MAX */ \
	X(std::int32_t, attack_direction_change, 0, 0, 4095) /* legacy: BL_ALL, BL_NUL, BL_ALL */ \
	X(std::int32_t, attack_attr_none, 14, 0, 4095) /* legacy: ~BL_PC, BL_NUL, BL_ALL */ \
	X(std::int32_t, equip_natural_break_rate, 0, 0, RATHENA_I32_MAX) /* legacy: 0, 0, INT_MAX */ \
	X(std::int32_t, equip_self_break_rate, 100, 0, RATHENA_I32_MAX) /* legacy: 100, 0, INT_MAX */ \
	X(std::int32_t, equip_skill_break_rate, 100, 0, RATHENA_I32_MAX) /* legacy: 100, 0, INT_MAX */ \
	X(std::int32_t, attack_walk_delay, 0, 0, 4095) /* legacy: BL_NUL, BL_NUL, BL_ALL */ \
	X(std::int32_t, damage_walk_delay, 0, 0, 4095) /* legacy: BL_NUL, BL_NUL, BL_ALL */ \
	X(bool, delay_battle_damage, true, 0, 1) \
	X(bool, autospell_check_range, false, 0, 1) \
	X(bool, knockback_left, true, 0, 1) \
	X(bool, warg_can_falcon, false, 0, 1) \
	X(bool, snap_dodge, false, 0, 1) \
	X(bool, break_mob_equip, false, 0, 1) \
	X(bool, synchronize_damage, false, 0, 1) \
	/* conf/battle/battleground.conf */ \
	X(std::int32_t, bg_update_interval, 1000, 100, RATHENA_I32_MAX) /* legacy: 1000, 100, INT_MAX */ \
	X(std::int32_t, bg_short_attack_damage_rate, 80, 0, RATHENA_I32_MAX) /* legacy: 80, 0, INT_MAX */ \
	X(std::int32_t, bg_long_attack_damage_rate, 80, 0, RATHENA_I32_MAX) /* legacy: 80, 0, INT_MAX */ \
	X(std::int32_t, bg_weapon_attack_damage_rate, 60, 0, RATHENA_I32_MAX) /* legacy: 60, 0, INT_MAX */ \
	X(std::int32_t, bg_magic_attack_damage_rate, 60, 0, RATHENA_I32_MAX) /* legacy: 60, 0, INT_MAX */ \
	X(std::int32_t, bg_misc_attack_damage_rate, 60, 0, RATHENA_I32_MAX) /* legacy: 60, 0, INT_MAX */ \
	X(std::int32_t, bg_flee_penalty, 20, 0, RATHENA_I32_MAX) /* legacy: 20, 0, INT_MAX */ \
	X(bool, bgqueue_nowarp_mapflag, false, 0, 1) \
	/* conf/battle/client.conf */ \
	X(bool, wedding_modifydisplay, false, 0, 1) \
	X(bool, wedding_ignorepalette, false, 0, 1) \
	X(bool, xmas_ignorepalette, false, 0, 1) \
	X(bool, summer_ignorepalette, false, 0, 1) \
	X(bool, hanbok_ignorepalette, false, 0, 1) \
	X(bool, oktoberfest_ignorepalette, false, 0, 1) \
	X(std::int32_t, max_lv, 99, 0, 275) /* legacy: 99, 0, MAX_LEVEL */ \
	X(std::int32_t, aura_lv, 99, 0, RATHENA_I32_MAX) /* legacy: 99, 0, INT_MAX */ \
	X(bool, save_clothcolor, true, 0, 1) \
	X(std::int32_t, min_hair_style, 0, 0, RATHENA_I32_MAX) /* legacy: 0, 0, INT_MAX */ \
	X(std::int32_t, max_hair_style, 42, 0, RATHENA_I32_MAX) /* legacy: 23, 0, INT_MAX */ \
	X(std::int32_t, min_hair_color, 0, 0, RATHENA_I32_MAX) /* legacy: 0, 0, INT_MAX */ \
	X(std::int32_t, max_hair_color, 8, 0, RATHENA_I32_MAX) /* legacy: 9, 0, INT_MAX */ \
	X(std::int32_t, min_cloth_color, 0, 0, RATHENA_I32_MAX) /* legacy: 0, 0, INT_MAX */ \
	X(std::int32_t, max_cloth_color, 7, 0, RATHENA_I32_MAX) /* legacy: 4, 0, INT_MAX */ \
	X(std::int32_t, pet_hair_style, 100, 0, RATHENA_I32_MAX) /* legacy: 100, 0, INT_MAX */ \
	X(std::int32_t, area_size, 14, 0, RATHENA_I32_MAX) /* legacy: 14, 0, INT_MAX */ \
	X(bool, motd_type, false, 0, 1) \
	X(bool, hide_woe_damage, false, 0, 1) \
	X(bool, display_version, true, 0, 1) \
	X(bool, display_hallucination, true, 0, 1) \
	X(bool, display_status_timers, true, 0, 1) \
	X(bool, client_reshuffle_dice, true, 0, 1) /* legacy: 0, 0, 1 */ \
	X(bool, client_sort_storage, false, 0, 1) \
	X(std::int32_t, client_limit_unit_lv, 0, 0, 4095) /* legacy: 0, 0, BL_ALL */ \
	X(std::int32_t, max_walk_path, 17, 1, 32) /* legacy: 17, 1, MAX_WALKPATH */ \
	X(bool, update_enemy_position, true, 0, 1) /* legacy: 0, 0, 1 */ \
	X(bool, spawn_direction, false, 0, 1) \
	X(bool, save_body_style, true, 0, 1) \
	X(bool, mvp_exp_reward_message, false, 0, 1) \
	X(std::int32_t, ping_timer_inverval, 30, 0, 99999999) \
	X(std::int32_t, ping_time, 20, 0, 99999999) \
	X(bool, show_skill_scale, true, 0, 1) \
	X(bool, drop_connection_on_quit, false, 0, 1) \
	X(std::int32_t, macro_detection_retry, 3, 1, RATHENA_I32_MAX) /* legacy: 3, 1, INT_MAX */ \
	X(std::int32_t, macro_detection_timeout, 60000, 0, RATHENA_I32_MAX) /* legacy: 60000, 0, INT_MAX */ \
	X(bool, macro_detection_punishment, false, 0, 1) \
	X(std::int32_t, macro_detection_punishment_time, 0, 0, RATHENA_I32_MAX) /* legacy: 0, 0, INT_MAX */ \
	X(std::int32_t, macrochecker_delay, 600000, 0, RATHENA_I32_MAX) /* legacy: 600000, 0, INT_MAX */ \
	X(std::int32_t, hide_cloaked_units, 0, 0, 4095) /* legacy: 0, 0, BL_ALL */ \
	/* conf/battle/drops.conf */ \
	X(std::int32_t, flooritem_lifetime, 60000, 1000, RATHENA_I32_MAX) /* legacy: 60000, 1000, INT_MAX */ \
	X(bool, item_auto_get, false, 0, 1) \
	X(std::int32_t, first_attack_loot_bonus, 30, 0, 100) \
	X(bool, mvp_to_loot_priority, false, 0, 1) \
	X(std::int32_t, item_drop_equip_min, 1, 0, 10000) \
	X(std::int32_t, item_drop_equip_max, 10000, 1, 10000) \
	X(std::int32_t, item_drop_card_min, 1, 0, 10000) \
	X(std::int32_t, item_drop_card_max, 10000, 1, 10000) \
	X(std::int32_t, item_drop_mvp_min, 1, 0, 10000) \
	X(std::int32_t, item_drop_mvp_max, 10000, 1, 10000) \
	X(std::int32_t, item_drop_mvp_mode, 0, 0, 2) \
	X(std::int32_t, item_drop_heal_min, 1, 0, 10000) \
	X(std::int32_t, item_drop_heal_max, 10000, 1, 10000) \
	X(std::int32_t, item_drop_use_min, 1, 0, 10000) \
	X(std::int32_t, item_drop_use_max, 10000, 1, 10000) \
	X(std::int32_t, item_drop_add_min, 1, 0, 10000) \
	X(std::int32_t, item_drop_add_max, 10000, 1, 10000) \
	X(std::int32_t, item_group_drop_min, 1, 0, 10000) \
	X(std::int32_t, item_group_drop_max, 10000, 1, 10000) \
	X(std::int32_t, item_drop_treasure_min, 1, 0, 10000) \
	X(std::int32_t, item_drop_treasure_max, 10000, 1, 10000) \
	X(std::int32_t, item_rate_mvp, 100, 0, 1000000) \
	X(std::int32_t, item_rate_common_boss, 100, 0, 1000000) \
	X(std::int32_t, item_rate_common_mvp, 100, 0, 1000000) \
	X(std::int32_t, item_rate_equip, 100, 0, 1000000) \
	X(std::int32_t, item_rate_equip_boss, 100, 0, 1000000) \
	X(std::int32_t, item_rate_equip_mvp, 100, 0, 1000000) \
	X(std::int32_t, item_rate_card, 100, 0, 1000000) \
	X(std::int32_t, item_rate_card_boss, 100, 0, 1000000) \
	X(std::int32_t, item_rate_card_mvp, 100, 0, 1000000) \
	X(std::int32_t, item_rate_heal, 100, 0, 1000000) \
	X(std::int32_t, item_rate_heal_boss, 100, 0, 1000000) \
	X(std::int32_t, item_rate_heal_mvp, 100, 0, 1000000) \
	X(std::int32_t, item_rate_use, 100, 0, 1000000) \
	X(std::int32_t, item_rate_use_boss, 100, 0, 1000000) \
	X(std::int32_t, item_rate_use_mvp, 100, 0, 1000000) \
	X(std::int32_t, item_rate_adddrop, 100, 0, 1000000) \
	X(std::int32_t, item_group_rate, 100, 0, 1000000) \
	X(std::int32_t, item_rate_treasure, 100, 0, 1000000) \
	X(std::int32_t, alchemist_summon_reward, 1, 0, 2) \
	X(std::int32_t, drops_by_luk, 0, 0, RATHENA_I32_MAX) /* legacy: 0, 0, INT_MAX */ \
	X(std::int32_t, drops_by_luk2, 0, 0, RATHENA_I32_MAX) /* legacy: 0, 0, INT_MAX */ \
	X(std::int32_t, rare_drop_announce, 0, 0, 10000) \
	X(bool, drop_rateincrease, false, 0, 1) \
	X(bool, autoloot_adjust, false, 0, 1) \
	X(bool, mercenary_autoloot, false, 0, 1) \
	X(std::int32_t, mer_idle_no_share, 0, 0, RATHENA_I32_MAX) /* legacy: 0, 0, INT_MAX */ \
	X(std::int32_t, idletime_mer_option, 31, 1, 4095) /* legacy: 0x1F, 0x1, 0xFFF */ \
	X(bool, rndopt_drop_pillar, true, 0, 1) \
	X(bool, enable_bonus_map_drops, true, 0, 1) \
	/* conf/battle/exp.conf */ \
	X(bool, pvp_exp, true, 0, 1) \
	X(std::int32_t, zeny_penalty, 0, 0, RATHENA_I32_MAX) /* legacy: 0, 0, INT_MAX */ \
	X(std::int32_t, mvp_exp_rate, 100, 0, RATHENA_I32_MAX) /* legacy: 100, 0, INT_MAX */ \
	X(std::int32_t, heal_exp, 0, 0, RATHENA_I32_MAX) /* legacy: 0, 0, INT_MAX */ \
	X(std::int32_t, resurrection_exp, 0, 0, RATHENA_I32_MAX) /* legacy: 0, 0, INT_MAX */ \
	X(std::int32_t, shop_exp, 0, 0, RATHENA_I32_MAX) /* legacy: 0, 0, INT_MAX */ \
	X(bool, disp_experience, false, 0, 1) \
	X(bool, disp_zeny, false, 0, 1) \
	X(std::int32_t, multi_level_up_base, 0, 0, 275) /* legacy: 0, 0, MAX_LEVEL */ \
	X(std::int32_t, multi_level_up_job, 0, 0, 275) /* legacy: 0, 0, MAX_LEVEL */ \
	X(std::int32_t, max_exp_gain_rate, 0, 0, RATHENA_I32_MAX) /* legacy: 0, 0, INT_MAX */ \
	X(std::int32_t, exp_calc_type, 0, 0, 2) \
	X(std::int32_t, exp_bonus_attacker, 25, 0, RATHENA_I32_MAX) /* legacy: 25, 0, INT_MAX */ \
	X(std::int32_t, exp_bonus_max_attacker, 12, 2, RATHENA_I32_MAX) /* legacy: 12, 2, INT_MAX */ \
	X(bool, exp_bonus_nodamage_attacker, false, 0, 1) \
	X(bool, use_statpoint_table, true, 0, 1) \
	X(std::int32_t, quest_exp_rate, 100, 0, RATHENA_I32_MAX) /* legacy: 100, 0, INT_MAX */ \
	X(std::int32_t, death_penalty_maxlv, 0, 0, 3) \
	X(std::int32_t, exp_cost_redemptio, 1, 0, 100) \
	X(std::int32_t, exp_cost_redemptio_limit, 5, 0, 12) /* legacy: 5, 0, MAX_PARTY */ \
	X(bool, use_traitpoint_table, true, 0, 1) \
	/* conf/battle/feature.conf */ \
	X(bool, feature_buying_store, true, 0, 1) \
	X(bool, feature_search_stores, true, 0, 1) \
	X(bool, feature_atcommand_suggestions, false, 0, 1) \
	X(std::int32_t, feature_auction, 0, 0, 2) \
	X(bool, feature_banking, true, 0, 1) \
	X(bool, feature_autotrade, true, 0, 1) \
	X(std::int32_t, feature_autotrade_direction, 4, -1, 7) \
	X(std::int32_t, feature_autotrade_head_direction, 0, -1, 2) \
	X(std::int32_t, feature_autotrade_sit, 1, -1, 1) \
	X(std::int32_t, feature_autotrade_open_delay, 5000, 1000, RATHENA_I32_MAX) /* legacy: 5000, 1000, INT_MAX */ \
	X(bool, feature_warp_suggestions, false, 0, 1) \
	X(bool, feature_roulette, true, 0, 1) \
	X(bool, feature_roulette_bonus_reward, true, 0, 1) \
	X(bool, feature_achievement, true, 0, 1) \
	X(bool, feature_petevolution, true, 0, 1) \
	X(bool, feature_petautofeed, true, 0, 1) \
	X(std::int32_t, feature_pet_autofeed_rate, 89, 0, 100) \
	X(bool, feature_homunculus_autofeed, true, 0, 1) \
	X(std::int32_t, feature_homunculus_autofeed_rate, 30, 0, 100) \
	X(bool, feature_attendance, true, 0, 1) \
	X(bool, feature_privateairship, true, 0, 1) \
	X(bool, feature_equipswitch, true, 0, 1) \
	X(bool, feature_bgqueue, true, 0, 1) \
	X(bool, feature_refineui, true, 0, 1) \
	X(bool, feature_barter, true, 0, 1) \
	X(bool, feature_barter_extended, true, 0, 1) \
	X(bool, feature_itemlink, true, 0, 1) \
	X(bool, feature_mesitemlink, true, 0, 1) \
	X(bool, feature_mesitemlink_brackets, false, 0, 1) \
	X(bool, feature_mesitemlink_dbname, false, 0, 1) \
	X(bool, feature_mesitemicon, true, 0, 1) \
	X(bool, feature_mesitemicon_dbname, false, 0, 1) \
	X(std::int32_t, feature_dynamicnpc_timeout, 60000, 60000, RATHENA_I32_MAX) /* legacy: 1000, 60000, INT_MAX */ \
	X(std::int32_t, feature_dynamicnpc_rangex, 2, 0, RATHENA_I32_MAX) /* legacy: 2, 0, INT_MAX */ \
	X(std::int32_t, feature_dynamicnpc_rangey, 2, 0, RATHENA_I32_MAX) /* legacy: 2, 0, INT_MAX */ \
	X(bool, feature_dynamicnpc_direction, false, 0, 1) \
	X(bool, feature_stylist, true, 0, 1) \
	X(bool, feature_banking_state_enforce, false, 0, 1) \
	/* conf/battle/gm.conf */ \
	X(std::int32_t, atcommand_spawn_quantity_limit, 100, 0, RATHENA_I32_MAX) /* legacy: 100, 0, INT_MAX */ \
	X(std::int32_t, atcommand_slave_clone_limit, 25, 0, RATHENA_I32_MAX) /* legacy: 25, 0, INT_MAX */ \
	X(bool, partial_name_scan, true, 0, 1) /* legacy: 0, 0, 1 */ \
	X(std::int32_t, ban_hack_trade, 5, 0, RATHENA_I32_MAX) /* legacy: 0, 0, INT_MAX */ \
	X(bool, atcommand_mobinfo_type, true, 0, 1) /* legacy: 0, 0, 1 */ \
	X(bool, atcommand_levelup_events, false, 0, 1) \
	X(bool, atcommand_disable_npc, true, 0, 1) \
	/* conf/battle/guild.conf */ \
	X(std::int32_t, guild_max_castles, 0, 0, RATHENA_I32_MAX) /* legacy: 0, 0, INT_MAX */ \
	X(std::int32_t, guild_skill_relog_delay, 300000, 0, RATHENA_I32_MAX) /* legacy: 300000, 0, INT_MAX */ \
	X(bool, guild_emperium_check, true, 0, 1) \
	X(std::int32_t, gvg_short_attack_damage_rate, 80, 0, RATHENA_I32_MAX) /* legacy: 80, 0, INT_MAX */ \
	X(std::int32_t, gvg_long_attack_damage_rate, 80, 0, RATHENA_I32_MAX) /* legacy: 80, 0, INT_MAX */ \
	X(std::int32_t, gvg_weapon_attack_damage_rate, 60, 0, RATHENA_I32_MAX) /* legacy: 60, 0, INT_MAX */ \
	X(std::int32_t, gvg_magic_attack_damage_rate, 60, 0, RATHENA_I32_MAX) /* legacy: 60, 0, INT_MAX */ \
	X(std::int32_t, gvg_misc_attack_damage_rate, 60, 0, RATHENA_I32_MAX) /* legacy: 60, 0, INT_MAX */ \
	X(std::int32_t, gvg_flee_penalty, 20, 0, RATHENA_I32_MAX) /* legacy: 20, 0, INT_MAX */ \
	X(bool, require_glory_guild, false, 0, 1) \
	X(std::int32_t, max_guild_alliance, 3, 0, 3) \
	X(std::int32_t, guild_notice_changemap, 2, 0, 2) \
	X(bool, guild_maprespawn_clones, false, 0, 1) \
	X(std::int32_t, guild_leaderchange_delay, 1440, 0, RATHENA_I32_MAX) /* legacy: 1440, 0, INT32_MAX */ \
	X(bool, guild_leaderchange_woe, false, 0, 1) \
	X(bool, guild_alliance_onlygm, false, 0, 1) \
	/* conf/battle/homunc.conf */ \
	X(std::int32_t, hom_setting, 61, 0, 65535) /* legacy: 0xFFFF, 0x0000, 0xFFFF */ \
	X(bool, hom_rename, false, 0, 1) \
	X(bool, homunculus_show_growth, true, 0, 1) /* legacy: 0, 0, 1 */ \
	X(std::int32_t, homunculus_friendly_rate, 100, 0, RATHENA_I32_MAX) /* legacy: 100, 0, INT_MAX */ \
	X(std::int32_t, hvan_explosion_intimate, 45000, 0, 100000) \
	X(bool, homunculus_autoloot, false, 0, 1) \
	X(std::int32_t, homunculus_auto_vapor, 80, 0, 100) \
	X(std::int32_t, homunculus_max_level, 99, 0, 275) /* legacy: 99, 0, MAX_LEVEL */ \
	X(std::int32_t, homunculus_S_max_level, 275, 0, 275) /* legacy: 150, 0, MAX_LEVEL */ \
	X(std::int32_t, homunculus_S_growth_level, 99, 0, 275) /* legacy: 99, 0, MAX_LEVEL */ \
	X(std::int32_t, homunculus_evo_intimacy_need, 91100, 0, RATHENA_I32_MAX) /* legacy: 91100, 0, INT_MAX */ \
	X(std::int32_t, homunculus_evo_intimacy_reset, 1000, 0, RATHENA_I32_MAX) /* legacy: 1000, 0, INT_MAX */ \
	X(bool, homunculus_autofeed_always, true, 0, 1) \
	X(std::int32_t, hom_idle_no_share, 0, 0, RATHENA_I32_MAX) /* legacy: 0, 0, INT_MAX */ \
	X(std::int32_t, idletime_hom_option, 31, 1, 4095) /* legacy: 0x1F, 0x1, 0xFFF */ \
	X(std::int32_t, homunculus_exp_gain, 10, 0, 100) \
	X(std::int32_t, homunculus_starving_rate, 10, 0, 100) \
	X(std::int32_t, homunculus_starving_delay, 20000, 0, RATHENA_I32_MAX) /* legacy: 20000, 0, INT_MAX */ \
	/* conf/battle/instance.conf */ \
	X(bool, instance_block_leave, true, 0, 1) \
	X(bool, instance_block_leaderchange, true, 0, 1) \
	X(bool, instance_block_invite, true, 0, 1) \
	X(bool, instance_block_expulsion, true, 0, 1) \
	/* conf/battle/items.conf */ \
	X(bool, cardfix_monster_physical, true, 0, 1) \
	X(bool, random_monster_checklv, false, 0, 1) \
	X(std::int32_t, gtb_sc_immunity, 50, 0, RATHENA_I32_MAX) /* legacy: 50, 0, INT_MAX */ \
	X(std::int32_t, weapon_produce_rate, 100, 0, RATHENA_I32_MAX) /* legacy: 100, 0, INT_MAX */ \
	X(std::int32_t, potion_produce_rate, 100, 0, RATHENA_I32_MAX) /* legacy: 100, 0, INT_MAX */ \
	X(std::int32_t, item_check, 0, 0, 7) /* legacy: 0x0, 0x0, 0x7 */ \
	X(std::int32_t, item_use_interval, 100, 0, RATHENA_I32_MAX) /* legacy: 100, 0, INT_MAX */ \
	X(std::int32_t, cashfood_use_interval, 60000, 0, RATHENA_I32_MAX) /* legacy: 60000, 0, INT_MAX */ \
	X(std::int32_t, produce_item_name_input, 3, 0, 159) /* legacy: 0x1|0x2, 0, 0x9F */ \
	X(bool, dead_branch_active, true, 0, 1) \
	X(bool, vending_over_max, true, 0, 1) \
	X(bool, buyer_name, true, 0, 1) \
	X(bool, autospell_stacking, false, 0, 1) \
	X(bool, allow_consume_restricted_item, false, 0, 1) /* legacy: 1, 0, 1 */ \
	X(bool, allow_equip_restricted_item, true, 0, 1) \
	X(bool, item_flooritem_check, true, 0, 1) \
	X(std::int32_t, default_bind_on_equip, 4, 0, 4) /* legacy: BOUND_CHAR, BOUND_NONE, BOUND_MAX-1 */ \
	X(std::int32_t, allow_bound_sell, 0, 0, 15) /* legacy: 0, 0, 0xF */ \
	X(std::int32_t, broadcast_hide_name, 2, 0, 24) /* legacy: 2, 0, NAME_LENGTH */ \
	X(bool, rental_transaction, true, 0, 1) \
	X(bool, rental_item_novalue, false, 0, 1) /* legacy: 1, 0, 1 */ \
	X(bool, trade_count_stackable, true, 0, 1) \
	/* conf/battle/misc.conf */ \
	X(bool, warp_point_debug, false, 0, 1) \
	X(std::int32_t, skill_log, 0, 0, 4095) /* legacy: BL_NUL, BL_NUL, BL_ALL */ \
	X(bool, battle_log, false, 0, 1) \
	X(bool, etc_log, false, 0, 1) /* legacy: 1, 0, 1 */ \
	X(std::int32_t, pk_short_attack_damage_rate, 80, 0, RATHENA_I32_MAX) /* legacy: 80, 0, INT_MAX */ \
	X(std::int32_t, pk_long_attack_damage_rate, 70, 0, RATHENA_I32_MAX) /* legacy: 70, 0, INT_MAX */ \
	X(std::int32_t, pk_weapon_attack_damage_rate, 60, 0, RATHENA_I32_MAX) /* legacy: 60, 0, INT_MAX */ \
	X(std::int32_t, pk_magic_attack_damage_rate, 60, 0, RATHENA_I32_MAX) /* legacy: 60, 0, INT_MAX */ \
	X(std::int32_t, pk_misc_attack_damage_rate, 60, 0, RATHENA_I32_MAX) /* legacy: 60, 0, INT_MAX */ \
	X(std::int32_t, official_cell_stack_limit, 1, 0, 255) \
	X(std::int32_t, custom_cell_stack_limit, 1, 1, 255) \
	X(std::int32_t, pk_mode, 0, 0, 2) \
	X(bool, pk_mode_mes, true, 0, 1) \
	X(std::int32_t, pk_level_range, 0, 0, RATHENA_I32_MAX) /* legacy: 0, 0, INT_MAX */ \
	X(std::int32_t, manner_system, 15, 0, 4095) /* legacy: 0xFFF, 0, 0xFFF */ \
	X(bool, night_at_start, false, 0, 1) \
	X(std::int32_t, pk_min_level, 55, 1, RATHENA_I32_MAX) /* legacy: 55, 1, INT_MAX */ \
	X(bool, duel_allow_pvp, false, 0, 1) \
	X(bool, duel_allow_gvg, false, 0, 1) \
	X(bool, duel_allow_teleport, false, 0, 1) \
	X(bool, duel_autoleave_when_die, true, 0, 1) \
	X(std::int32_t, duel_time_interval, 60, 0, RATHENA_I32_MAX) /* legacy: 60, 0, INT_MAX */ \
	X(bool, duel_only_on_same_map, false, 0, 1) \
	X(std::int32_t, day_duration, 0, 0, RATHENA_I32_MAX) /* legacy: 0, 0, INT_MAX */ \
	X(std::int32_t, night_duration, 0, 0, RATHENA_I32_MAX) /* legacy: 0, 0, INT_MAX */ \
	X(bool, at_mapflag, false, 0, 1) \
	X(std::int32_t, at_timeout, 0, 0, RATHENA_I32_MAX) /* legacy: 0, 0, INT_MAX */ \
	X(std::int32_t, auction_feeperhour, 12000, 0, RATHENA_I32_MAX) /* legacy: 12000, 0, INT_MAX */ \
	X(std::int32_t, auction_maximumprice, 500000000, 0, RATHENA_I32_MAX) /* legacy: 500000000, 0, MAX_ZENY */ \
	X(std::int32_t, searchstore_querydelay, 10, 0, RATHENA_I32_MAX) /* legacy: 10, 0, INT_MAX */ \
	X(std::int32_t, searchstore_maxresults, 30, 1, RATHENA_I32_MAX) /* legacy: 30, 1, INT_MAX */ \
	X(bool, cashshop_show_points, false, 0, 1) \
	X(std::int32_t, mail_show_status, 0, 0, 2) \
	X(bool, mon_trans_disable_in_gvg, false, 0, 1) \
	X(std::int32_t, discount_item_point_shop, 0, 0, 3) \
	X(bool, disp_servervip_msg, false, 0, 1) \
	X(std::int32_t, mail_delay, 1000, 1000, RATHENA_I32_MAX) /* legacy: 1000, 1000, INT_MAX */ \
	X(bool, at_monsterignore, false, 0, 1) \
	X(bool, hide_fav_sell, false, 0, 1) \
	X(std::int32_t, mail_daily_count, 100, 0, RATHENA_I32_MAX) /* legacy: 100, 0, INT32_MAX */ \
	X(std::int32_t, mail_zeny_fee, 2, 0, 100) \
	X(std::int32_t, mail_attachment_price, 2500, 0, RATHENA_I32_MAX) /* legacy: 2500, 0, INT32_MAX */ \
	X(std::int32_t, mail_attachment_weight, 2000, 0, RATHENA_I32_MAX) /* legacy: 2000, 0, INT32_MAX */ \
	X(bool, at_logout_event, true, 0, 1) \
	X(std::int32_t, map_edge_size, 15, 1, 40) \
	X(bool, item_stacking, true, 0, 1) \
	/* conf/battle/monster.conf */ \
	X(std::int32_t, mvp_hp_rate, 100, 1, RATHENA_I32_MAX) /* legacy: 100, 1, INT_MAX */ \
	X(std::int32_t, monster_hp_rate, 100, 1, RATHENA_I32_MAX) /* legacy: 100, 1, INT_MAX */ \
	X(std::int32_t, view_range_rate, 100, 0, RATHENA_I32_MAX) /* legacy: 100, 0, INT_MAX */ \
	X(std::int32_t, chase_range_rate, 100, 0, RATHENA_I32_MAX) /* legacy: 100, 0, INT_MAX */ \
	X(bool, monster_active_enable, true, 0, 1) \
	X(std::int32_t, monster_damage_delay_rate, 100, 0, RATHENA_I32_MAX) /* legacy: 100, 0, INT_MAX */ \
	X(bool, monster_loot_type, false, 0, 1) \
	X(std::int32_t, mob_skill_rate, 100, 0, RATHENA_I32_MAX) /* legacy: 100, 0, INT_MAX */ \
	X(std::int32_t, mob_skill_delay, 100, 0, RATHENA_I32_MAX) /* legacy: 100, 0, INT_MAX */ \
	X(std::int32_t, mob_count_rate, 100, 0, RATHENA_I32_MAX) /* legacy: 100, 0, INT_MAX */ \
	X(std::int32_t, mob_spawn_delay, 100, 0, RATHENA_I32_MAX) /* legacy: 100, 0, INT_MAX */ \
	X(std::int32_t, plant_spawn_delay, 100, 0, RATHENA_I32_MAX) /* legacy: 100, 0, INT_MAX */ \
	X(std::int32_t, boss_spawn_delay, 100, 0, RATHENA_I32_MAX) /* legacy: 100, 0, INT_MAX */ \
	X(std::int32_t, no_spawn_on_player, 0, 0, 100) \
	X(bool, force_random_spawn, false, 0, 1) \
	X(std::int32_t, slaves_inherit_mode, 4, 0, 4) \
	X(std::int32_t, slaves_inherit_speed, 3, 0, 3) \
	X(bool, summons_trigger_autospells, true, 0, 1) \
	X(bool, mob_changetarget_byskill, false, 0, 1) \
	X(bool, monster_class_change_full_recover, true, 0, 1) \
	X(std::int32_t, mob_warp, 0, 0, 15) /* legacy: 0, 0, 1|2|4|8 */ \
	X(std::int32_t, show_mob_info, 0, 0, 7) /* legacy: 0, 0, 1|2|4 */ \
	X(bool, zeny_from_mobs, false, 0, 1) \
	X(bool, mobs_level_up, false, 0, 1) \
	X(std::int32_t, mobs_level_up_exp_rate, 1, 1, RATHENA_I32_MAX) /* legacy: 1, 1, INT_MAX */ \
	X(std::int32_t, monster_ai, 0, 0, 16383) /* legacy: 0x0000, 0x0000, 0x3FFF */ \
	X(bool, dynamic_mobs, true, 0, 1) \
	X(bool, mob_remove_damaged, true, 0, 1) \
	X(bool, mob_npc_event_type, true, 0, 1) \
	X(bool, retaliate_to_master, true, 0, 1) \
	X(std::int32_t, override_mob_names, 0, 0, 2) \
	X(std::int32_t, mob_remove_delay, 300000, 1000, RATHENA_I32_MAX) /* legacy: 60000, 1000, INT_MAX */ \
	X(std::int32_t, mob_active_time, 0, 0, RATHENA_I32_MAX) /* legacy: 5000, 0, INT_MAX */ \
	X(std::int32_t, boss_active_time, 0, 0, RATHENA_I32_MAX) /* legacy: 5000, 0, INT_MAX */ \
	X(std::int32_t, ksprotection, 0, 0, RATHENA_I32_MAX) /* legacy: 5000, 0, INT_MAX */ \
	X(bool, mob_slave_keep_target, true, 0, 1) /* legacy: 0, 0, 1 */ \
	X(bool, mvp_tomb_enabled, true, 0, 1) \
	X(std::int32_t, mvp_tomb_delay, 9000, 0, RATHENA_I32_MAX) /* legacy: 9000, 0, INT_MAX */ \
	X(bool, mob_size_influence, false, 0, 1) \
	X(std::int32_t, monster_chase_refresh, 32, 0, 32) /* legacy: 32, 0, MAX_WALKPATH */ \
	X(std::int32_t, mob_icewall_walk_block, 75, 0, 255) \
	X(std::int32_t, boss_icewall_walk_block, 0, 0, 255) \
	X(bool, monster_loot_search_type, true, 0, 1) \
	X(bool, monster_hp_bars_info, true, 0, 1) \
	X(std::int32_t, monster_eye_range_bonus, 0, 0, 10) \
	X(bool, monster_stuck_warning, false, 0, 1) \
	X(std::int32_t, mob_nopc_idleskill_rate, 100, 0, 100) \
	X(std::int32_t, mob_nopc_move_rate, 100, 0, 100) \
	X(std::int32_t, boss_nopc_idleskill_rate, 100, 0, 100) \
	X(std::int32_t, boss_nopc_move_rate, 100, 0, 100) \
	X(bool, achievement_mob_share, false, 0, 1) \
	X(bool, slave_stick_with_master, false, 0, 1) \
	X(bool, slave_active_with_master, false, 0, 1) \
	X(std::int32_t, mob_spawn_variance, 1, 0, 3) \
	X(std::int32_t, mob_respawn_time, 1000, 1000, RATHENA_I32_MAX) /* legacy: 1000, 1000, INT_MAX */ \
	X(std::int32_t, mob_unlock_time, 2000, 0, RATHENA_I32_MAX) /* legacy: 2000, 0, INT_MAX */ \
	X(bool, randomize_center_cell, true, 0, 1) \
	X(std::int32_t, loot_range, 12, 1, 32) /* legacy: 12, 1, MAX_WALKPATH */ \
	X(std::int32_t, assist_range, 11, 1, 32) /* legacy: 11, 1, MAX_WALKPATH */ \
	/* conf/battle/party.conf */ \
	X(bool, show_steal_in_same_party, false, 0, 1) \
	X(bool, party_hp_mode, false, 0, 1) \
	X(bool, show_party_share_picker, true, 0, 1) \
	X(std::int32_t, show_picker_item_type, 112, 0, RATHENA_I32_MAX) /* legacy: 112, 0, INT_MAX */ \
	X(std::int32_t, party_update_interval, 1000, 100, RATHENA_I32_MAX) /* legacy: 1000, 100, INT_MAX */ \
	X(std::int32_t, party_item_share_type, 0, 0, 3) /* legacy: 0, 0, 1|2|3 */ \
	X(std::int32_t, idle_no_share, 0, 0, RATHENA_I32_MAX) /* legacy: 0, 0, INT_MAX */ \
	X(bool, display_party_name, true, 0, 1) /* legacy: 0, 0, 1 */ \
	X(bool, block_account_in_same_party, true, 0, 1) \
	X(bool, change_party_leader_samemap, true, 0, 1) \
	/* conf/battle/pet.conf */ \
	X(std::int32_t, pet_catch_rate, 100, 0, RATHENA_I32_MAX) /* legacy: 100, 0, INT_MAX */ \
	X(bool, pet_rename, false, 0, 1) \
	X(std::int32_t, pet_friendly_rate, 100, 0, RATHENA_I32_MAX) /* legacy: 100, 0, INT_MAX */ \
	X(std::int32_t, pet_hungry_delay_rate, 100, 10, RATHENA_I32_MAX) /* legacy: 100, 10, INT_MAX */ \
	X(bool, pet_status_support, false, 0, 1) \
	X(bool, pet_attack_support, false, 0, 1) \
	X(bool, pet_damage_support, false, 0, 1) \
	X(std::int32_t, pet_support_min_friendly, 900, 0, 950) \
	X(std::int32_t, pet_support_rate, 100, 0, RATHENA_I32_MAX) /* legacy: 100, 0, INT_MAX */ \
	X(bool, pet_attack_exp_to_master, false, 0, 1) \
	X(std::int32_t, pet_attack_exp_rate, 100, 0, RATHENA_I32_MAX) /* legacy: 100, 0, INT_MAX */ \
	X(std::int32_t, pet_lv_rate, 0, 0, RATHENA_I32_MAX) /* legacy: 0, 0, INT_MAX */ \
	X(std::int32_t, pet_max_stats, 99, 0, RATHENA_I32_MAX) /* legacy: 99, 0, INT_MAX */ \
	X(std::int32_t, pet_max_atk1, 500, 0, RATHENA_I32_MAX) /* legacy: 750, 0, INT_MAX */ \
	X(std::int32_t, pet_max_atk2, 1000, 0, RATHENA_I32_MAX) /* legacy: 1000, 0, INT_MAX */ \
	X(bool, pet_disable_in_gvg, false, 0, 1) \
	X(bool, pet_master_dead, false, 0, 1) \
	X(bool, pet_equip_required, true, 0, 1) /* legacy: 0, 0, 1 */ \
	X(bool, pet_unequip_destroy, true, 0, 1) \
	X(bool, pet_ignore_infinite_def, true, 0, 1) /* legacy: 0, 0, 1 */ \
	X(bool, pet_autofeed_always, true, 0, 1) \
	X(std::int32_t, pet_walk_speed, 1, 1, 3) \
	X(bool, pet_legacy_formula, false, 0, 1) \
	X(std::int32_t, pet_distance_check, 5, 0, 50) \
	X(bool, pet_hide_check, true, 0, 1) \
	/* conf/battle/player.conf */ \
	X(bool, left_cardfix_to_right, true, 0, 1) /* legacy: 0, 0, 1 */ \
	X(std::int32_t, hp_rate, 100, 1, RATHENA_I32_MAX) /* legacy: 100, 1, INT_MAX */ \
	X(std::int32_t, sp_rate, 100, 1, RATHENA_I32_MAX) /* legacy: 100, 1, INT_MAX */ \
	X(std::int32_t, restart_hp_rate, 0, 0, 100) \
	X(std::int32_t, restart_sp_rate, 0, 0, 100) \
	X(bool, player_skillfree, false, 0, 1) \
	X(bool, player_skillup_limit, true, 0, 1) \
	X(bool, quest_skill_learn, false, 0, 1) \
	X(bool, quest_skill_reset, false, 0, 1) \
	X(bool, basic_skill_check, true, 0, 1) \
	X(std::int32_t, player_invincible_time, 3000, 0, RATHENA_I32_MAX) /* legacy: 3000, 0, INT_MAX */ \
	X(std::int32_t, natural_heal_skill_interval, 10000, 500, RATHENA_I32_MAX) /* legacy: 10000, NATURAL_HEAL_INTERVAL, INT_MAX */ \
	X(std::int32_t, max_summoner_aspd, 193, 100, 199) \
	X(std::int32_t, max_walk_speed, 300, 100, 15000) /* legacy: 300, 100, 100*DEFAULT_WALK_SPEED */ \
	X(std::int32_t, max_hp_lv99, 330000, 100, 1000000000) \
	X(std::int32_t, max_hp_lv150, 660000, 100, 1000000000) \
	X(std::int32_t, max_hp, 1100000, 100, 1000000000) \
	X(std::int32_t, max_sp, 1000000, 100, 1000000000) /* legacy: 32500, 100, 1000000000 */ \
	X(std::int32_t, max_def, 99, 0, RATHENA_I32_MAX) /* legacy: 99, 0, INT_MAX */ \
	X(std::int32_t, over_def_bonus, 0, 0, 1000) \
	X(bool, invite_request_check, true, 0, 1) \
	X(std::int32_t, bone_drop, 0, 0, 2) \
	X(std::int32_t, prevent_logout_trigger, 14, 0, 15) /* legacy: 0xE, 0, 0xF */ \
	X(bool, show_hp_sp_drain, false, 0, 1) \
	X(bool, show_hp_sp_gain, true, 0, 1) \
	X(std::int32_t, character_size, 0, 0, 3) /* legacy: 1|2, 0, 1|2 */ \
	X(bool, friend_auto_add, true, 0, 1) \
	X(std::int32_t, idle_no_autoloot, 0, 0, RATHENA_I32_MAX) /* legacy: 0, 0, INT_MAX */ \
	X(std::int32_t, max_baby_third_parameter, 117, 10, 32767) /* legacy: 108, 10, SHRT_MAX */ \
	X(std::int32_t, max_trans_parameter, 99, 10, 32767) /* legacy: 99, 10, SHRT_MAX */ \
	X(std::int32_t, max_third_trans_parameter, 130, 10, 32767) /* legacy: 135, 10, SHRT_MAX */ \
	X(std::int32_t, max_extended_parameter, 130, 10, 32767) /* legacy: 125, 10, SHRT_MAX */ \
	X(std::int32_t, max_summoner_parameter, 130, 10, 32767) /* legacy: 120, 10, SHRT_MAX */ \
	X(std::int32_t, max_fourth_parameter, 130, 10, 32767) /* legacy: 135, 10, SHRT_MAX */ \
	X(std::int32_t, min_npc_vendchat_distance, 3, 0, 100) \
	X(std::int32_t, vip_base_exp_increase, 50, 0, RATHENA_I32_MAX) /* legacy: 50, 0, INT_MAX */ \
	X(std::int32_t, vip_job_exp_increase, 50, 0, RATHENA_I32_MAX) /* legacy: 50, 0, INT_MAX */ \
	X(std::int32_t, vip_exp_penalty_base, 100, 0, RATHENA_I32_MAX) /* legacy: 100, 0, INT_MAX */ \
	X(std::int32_t, vip_exp_penalty_job, 100, 0, RATHENA_I32_MAX) /* legacy: 100, 0, INT_MAX */ \
	X(std::int32_t, vip_zeny_penalty, 0, 0, RATHENA_I32_MAX) /* legacy: 0, 0, INT_MAX */ \
	X(std::int32_t, vip_bm_increase, 2, 0, RATHENA_I32_MAX) /* legacy: 2, 0, INT_MAX */ \
	X(std::int32_t, vip_gemstone, 2, 0, 2) \
	X(bool, vip_disp_rate, true, 0, 1) \
	X(std::int32_t, rental_mount_speed_boost, 25, 0, 100) \
	X(std::int32_t, transcendent_status_points, 52, 1, RATHENA_I32_MAX) /* legacy: 52, 1, INT_MAX */ \
	X(std::int32_t, taekwon_ranker_min_lv, 90, 1, 275) /* legacy: 90, 1, MAX_LEVEL */ \
	X(bool, revive_onwarp, true, 0, 1) \
	X(std::int32_t, fame_taekwon_mission, 1, 0, RATHENA_I32_MAX) /* legacy: 1, 0, INT_MAX */ \
	X(std::int32_t, fame_refine_lv1, 1, 0, RATHENA_I32_MAX) /* legacy: 1, 0, INT_MAX */ \
	X(std::int32_t, fame_refine_lv2, 25, 0, RATHENA_I32_MAX) /* legacy: 25, 0, INT_MAX */ \
	X(std::int32_t, fame_refine_lv3, 1000, 0, RATHENA_I32_MAX) /* legacy: 1000, 0, INT_MAX */ \
	X(std::int32_t, fame_forge, 10, 0, RATHENA_I32_MAX) /* legacy: 10, 0, INT_MAX */ \
	X(std::int32_t, fame_pharmacy_3, 1, 0, RATHENA_I32_MAX) /* legacy: 1, 0, INT_MAX */ \
	X(std::int32_t, fame_pharmacy_5, 3, 0, RATHENA_I32_MAX) /* legacy: 3, 0, INT_MAX */ \
	X(std::int32_t, fame_pharmacy_7, 10, 0, RATHENA_I32_MAX) /* legacy: 10, 0, INT_MAX */ \
	X(std::int32_t, fame_pharmacy_10, 50, 0, RATHENA_I32_MAX) /* legacy: 50, 0, INT_MAX */ \
	X(std::int32_t, idletime_option, 31775, 1, 65535) /* legacy: 0x7C1F, 1, 0xFFFF */ \
	X(std::int32_t, max_extended_aspd, 193, 100, 199) \
	X(std::int32_t, summoner_race, 11, 0, 11) /* legacy: RC_PLAYER_DORAM, RC_FORMLESS, RC_PLAYER_DORAM */ \
	X(std::int32_t, summoner_size, 0, 0, 2) /* legacy: SZ_SMALL, SZ_SMALL, SZ_BIG */ \
	X(std::int32_t, blacksmith_fame_refine_threshold, 10, 1, 20) /* legacy: 10, 1, MAX_REFINE */ \
	X(std::int32_t, trait_points_job_change, 7, 1, 1000) \
	X(std::int32_t, max_trait_parameter, 110, 10, 32767) /* legacy: 100, 10, SHRT_MAX */ \
	X(std::int32_t, max_res_mres_ignored, 50, 0, 100) \
	X(std::int32_t, max_ap, 1000, 100, 1000000000) /* legacy: 200, 100, 1000000000 */ \
	X(std::int32_t, ap_rate, 100, 1, RATHENA_I32_MAX) /* legacy: 100, 1, INT_MAX */ \
	X(std::int32_t, restart_ap_rate, 0, 0, 100) \
	X(bool, loose_ap_on_death, true, 0, 1) \
	X(bool, loose_ap_on_map, true, 0, 1) \
	X(bool, keep_ap_on_logout, true, 0, 1) \
	X(std::int32_t, major_overweight_rate, 90, 0, 100) \
	/* conf/battle/skill.conf */ \
	X(std::int32_t, casting_rate, 100, 0, RATHENA_I32_MAX) /* legacy: 100, 0, INT_MAX */ \
	X(bool, delay_dependon_dex, false, 0, 1) \
	X(bool, delay_dependon_agi, false, 0, 1) \
	X(bool, skill_delay_attack_enable, true, 0, 1) /* legacy: 0, 0, 1 */ \
	X(std::int32_t, skill_add_range, 0, 0, RATHENA_I32_MAX) /* legacy: 0, 0, INT_MAX */ \
	X(bool, skill_out_range_consume, false, 0, 1) /* legacy: 1, 0, 1 */ \
	X(std::int32_t, skillrange_by_distance, 14, 0, 4095) /* legacy: ~BL_PC, BL_NUL, BL_ALL */ \
	X(std::int32_t, skillrange_from_weapon, 0, 0, 4095) /* legacy: BL_NUL, BL_NUL, BL_ALL */ \
	X(bool, defunit_not_enemy, false, 0, 1) \
	X(std::int32_t, gvg_traps_target_all, 1, 0, 4095) /* legacy: BL_PC, BL_NUL, BL_ALL */ \
	X(std::int32_t, clear_skills_on_death, 0, 0, 4095) /* legacy: BL_NUL, BL_NUL, BL_ALL */ \
	X(std::int32_t, clear_skills_on_warp, 15, 0, 4095) /* legacy: BL_ALL, BL_NUL, BL_ALL */ \
	X(std::int32_t, guild_aura, 31, 0, 31) \
	X(std::int32_t, emergency_call, 11, 0, 31) \
	X(std::int32_t, skill_min_damage, 0, 0, 7) /* legacy: 2|4, 0, 1|2|4 */ \
	X(bool, finger_offensive_type, false, 0, 1) \
	X(std::int32_t, max_heal_lv, 11, 1, RATHENA_I32_MAX) /* legacy: 11, 1, INT_MAX */ \
	X(std::int32_t, max_heal, 9999, 0, RATHENA_I32_MAX) /* legacy: 9999, 0, INT_MAX */ \
	X(std::int32_t, combo_delay_rate, 100, 0, RATHENA_I32_MAX) /* legacy: 100, 0, INT_MAX */ \
	X(std::int32_t, auto_counter_type, 15, 0, 4095) /* legacy: BL_ALL, BL_NUL, BL_ALL */ \
	X(std::int32_t, skill_reiteration, 0, 0, 4095) /* legacy: BL_NUL, BL_NUL, BL_ALL */ \
	X(std::int32_t, skill_nofootset, 1, 0, 4095) /* legacy: BL_PC, BL_NUL, BL_ALL */ \
	X(std::int32_t, player_cloak_check_type, 1, 0, 7) /* legacy: 1, 0, 1|2|4 */ \
	X(std::int32_t, monster_cloak_check_type, 4, 0, 7) /* legacy: 4, 0, 1|2|4 */ \
	X(std::int32_t, sense_type, 1, 0, 3) /* legacy: 1|2, 0, 1|2 */ \
	X(std::int32_t, land_skill_limit, 9, 0, 4095) /* legacy: BL_ALL, BL_NUL, BL_ALL */ \
	X(std::int32_t, display_skill_fail, 2, 0, 15) /* legacy: 2, 0, 1|2|4|8 */ \
	X(bool, chat_warpportal, false, 0, 1) \
	X(bool, gx_allhit, false, 0, 1) \
	X(bool, gx_disptype, true, 0, 1) \
	X(std::int32_t, devotion_level_difference, 10, 0, RATHENA_I32_MAX) /* legacy: 10, 0, INT_MAX */ \
	X(bool, player_skill_partner_check, true, 0, 1) \
	X(bool, skill_removetrap_type, false, 0, 1) \
	X(bool, skill_wall_check, true, 0, 1) \
	X(bool, dancing_weaponswitch_fix, true, 0, 1) \
	X(bool, backstab_bow_penalty, true, 0, 1) /* legacy: 0, 0, 1 */ \
	X(std::int32_t, skill_steal_max_tries, 0, 0, 255) /* legacy: 0, 0, UCHAR_MAX */ \
	X(bool, skill_steal_random_options, false, 0, 1) \
	X(bool, amotion_min_skill_delay, false, 0, 1) \
	X(std::int32_t, default_walk_delay, 300, 0, RATHENA_I32_MAX) /* legacy: 300, 0, INT_MAX */ \
	X(std::int32_t, no_skill_delay, 2, 0, 4095) /* legacy: BL_MOB, BL_NUL, BL_ALL */ \
	X(std::int32_t, mob_max_skilllvl, 100, 1, 100) /* legacy: MAX_MOBSKILL_LEVEL, 1, MAX_MOBSKILL_LEVEL */ \
	X(bool, skip_teleport_lv1_menu, false, 0, 1) \
	X(bool, allow_skill_without_day, false, 0, 1) \
	X(bool, allow_es_magic_player, false, 0, 1) \
	X(bool, skill_caster_check, true, 0, 1) \
	X(std::int32_t, sg_miracle_skill_ratio, 1, 0, 20000) \
	X(std::int32_t, sg_angel_skill_ratio, 10, 0, 10000) \
	X(std::int32_t, sg_miracle_skill_duration, 3600000, 0, RATHENA_I32_MAX) /* legacy: 3600000, 0, INT_MAX */ \
	X(std::int32_t, skill_add_heal_rate, 487, 0, RATHENA_I32_MAX) /* legacy: 487, 0, INT_MAX */ \
	X(bool, eq_single_target_reflectable, true, 0, 1) \
	X(bool, invincible_nodamage, false, 0, 1) \
	X(bool, land_protector_behavior, false, 0, 1) \
	X(bool, npc_emotion_behavior, false, 0, 1) \
	X(std::int32_t, skill_amotion_leniency, 0, 0, 300) \
	X(std::int32_t, skill_trap_type, 0, 0, 3) \
	X(bool, multi_trigger_trap, false, 0, 1) \
	X(std::int32_t, bowling_bash_area, 0, 0, 20) \
	X(std::int32_t, devotion_rdamage, 0, 0, 100) \
	X(bool, path_blown_halt, true, 0, 1) \
	X(std::int32_t, taekwon_mission_mobname, 0, 0, 2) \
	X(bool, teleport_on_portal, false, 0, 1) \
	X(bool, cart_revo_knockback, true, 0, 1) \
	X(bool, arrow_shower_knockback, true, 0, 1) \
	X(bool, devotion_rdamage_skill_only, true, 0, 1) \
	X(bool, stormgust_knockback, true, 0, 1) \
	X(bool, skill_eightpath_algorithm, true, 0, 1) \
	X(bool, skill_eightpath_same_cell, true, 0, 1) \
	X(std::int32_t, can_damage_skill, 1, 0, 4095) /* legacy: 1, 0, BL_ALL */ \
	X(bool, tarotcard_equal_chance, false, 0, 1) \
	X(bool, dispel_song, false, 0, 1) \
	X(bool, refresh_song, false, 0, 1) \
	X(bool, refresh_song_icon, false, 0, 1) \
	X(std::int32_t, banana_bomb_duration, 0, 0, 65535) /* legacy: 0, 0, UINT16_MAX */ \
	X(bool, skill_drop_items_full, false, 0, 1) \
	X(std::int32_t, switch_remove_edp, 2, 0, 3) \
	X(bool, devotion_standup_fix, true, 0, 1) \
	X(std::int32_t, attack_machine_level_difference, 15, 0, RATHENA_I32_MAX) /* legacy: 15, 0, INT_MAX */ \
	X(bool, oridecon_research_fix, false, 0, 1) \
	/* conf/battle/status.conf */ \
	X(std::int32_t, debuff_on_logout, 0, 0, 3) /* legacy: 0, 0, 1|2 */ \
	X(std::int32_t, status_cast_cancel, 0, 0, 4095) /* legacy: BL_NUL, BL_NUL, BL_ALL */ \
	X(std::int32_t, pc_status_def_rate, 100, 0, RATHENA_I32_MAX) /* legacy: 100, 0, INT_MAX */ \
	X(std::int32_t, mob_status_def_rate, 100, 0, RATHENA_I32_MAX) /* legacy: 100, 0, INT_MAX */ \
	X(std::int32_t, pc_max_status_def, 100, 0, RATHENA_I32_MAX) /* legacy: 100, 0, INT_MAX */ \
	X(std::int32_t, mob_max_status_def, 100, 0, RATHENA_I32_MAX) /* legacy: 100, 0, INT_MAX */ \
	/* (not set by conf/battle: legacy code default) */ \
	X(std::int32_t, traps_setting, 2, 0, 2) \
	X(bool, guild_skill_relog_type, false, 0, 1) \
	X(std::int32_t, pet_hungry_friendly_decrease, 5, 0, RATHENA_I32_MAX) /* legacy: 5, 0, INT_MAX */ \
	X(std::int32_t, natural_heal_weight_rate, 70, 0, 100) \
	X(std::int32_t, open_box_weight_rate, 90, 0, 100) \
	X(bool, item_enabled_npc, false, 0, 1) \
	X(bool, feature_instance_allow_reconnect, true, 0, 1) \
	X(bool, hom_delay_reset_vaporize, false, 0, 1) \
	X(bool, hom_delay_reset_warp, false, 0, 1) \
	X(std::int32_t, alchemist_summon_setting, 12, 0, 15) \
	/* end */

#define RATHENA_BATTLE_KNOB_CONF_KEYS(K) \
	K(show_picker_item_type, "show_picker.item_type") \
	K(invincible_nodamage, "invincible.nodamage") \
	K(feature_buying_store, "feature.buying_store") \
	K(feature_search_stores, "feature.search_stores") \
	K(feature_atcommand_suggestions, "feature.atcommand_suggestions") \
	K(feature_auction, "feature.auction") \
	K(feature_banking, "feature.banking") \
	K(feature_autotrade, "feature.autotrade") \
	K(feature_autotrade_direction, "feature.autotrade_direction") \
	K(feature_autotrade_head_direction, "feature.autotrade_head_direction") \
	K(feature_autotrade_sit, "feature.autotrade_sit") \
	K(feature_autotrade_open_delay, "feature.autotrade_open_delay") \
	K(feature_warp_suggestions, "feature.warp_suggestions") \
	K(feature_roulette, "feature.roulette") \
	K(feature_roulette_bonus_reward, "feature.roulette_bonus_reward") \
	K(feature_achievement, "feature.achievement") \
	K(feature_petevolution, "feature.petevolution") \
	K(feature_petautofeed, "feature.petautofeed") \
	K(feature_pet_autofeed_rate, "feature.pet_autofeed_rate") \
	K(feature_homunculus_autofeed, "feature.homunculus_autofeed") \
	K(feature_homunculus_autofeed_rate, "feature.homunculus_autofeed_rate") \
	K(feature_attendance, "feature.attendance") \
	K(feature_privateairship, "feature.privateairship") \
	K(feature_equipswitch, "feature.equipswitch") \
	K(feature_bgqueue, "feature.bgqueue") \
	K(feature_refineui, "feature.refineui") \
	K(feature_barter, "feature.barter") \
	K(feature_barter_extended, "feature.barter_extended") \
	K(feature_itemlink, "feature.itemlink") \
	K(feature_mesitemlink, "feature.mesitemlink") \
	K(feature_mesitemlink_brackets, "feature.mesitemlink_brackets") \
	K(feature_mesitemlink_dbname, "feature.mesitemlink_dbname") \
	K(feature_mesitemicon, "feature.mesitemicon") \
	K(feature_mesitemicon_dbname, "feature.mesitemicon_dbname") \
	K(feature_dynamicnpc_timeout, "feature.dynamicnpc_timeout") \
	K(feature_dynamicnpc_rangex, "feature.dynamicnpc_rangex") \
	K(feature_dynamicnpc_rangey, "feature.dynamicnpc_rangey") \
	K(feature_dynamicnpc_direction, "feature.dynamicnpc_direction") \
	K(feature_stylist, "feature.stylist") \
	K(feature_banking_state_enforce, "feature.banking_state_enforce") \
	K(feature_instance_allow_reconnect, "feature.instance_allow_reconnect") \
	/* end */

#define RATHENA_BATTLE_PRE_RENEWAL_DEFAULTS(P) \
	P(traps_setting, 0) \
	P(guild_skill_relog_type, true) \
	P(natural_heal_weight_rate, 50) \
	P(open_box_weight_rate, 70) \
	P(item_enabled_npc, true) \
	P(feature_instance_allow_reconnect, false) \
	P(hom_delay_reset_vaporize, true) \
	P(hom_delay_reset_warp, true) \
	P(alchemist_summon_setting, 15) \
	/* end */

#define RATHENA_BATTLE_KNOB_LEGACY(L) \
	L(warp_point_debug, "warp_point_debug", 0, 0, 0, 1, 0, 1, "conf/battle/misc.conf") \
	L(enable_critical, "enable_critical", 1, 1, 0, 4095, 0, 4095, "conf/battle/battle.conf") \
	L(mob_critical_rate, "mob_critical_rate", 100, 100, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/battle.conf") \
	L(critical_rate, "critical_rate", 100, 100, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/battle.conf") \
	L(enable_baseatk, "enable_baseatk", 539, 539, 0, 4095, 0, 4095, "conf/battle/battle.conf") \
	L(enable_baseatk_renewal, "enable_baseatk_renewal", 4095, 4095, 0, 4095, 0, 4095, "conf/battle/battle.conf") \
	L(enable_perfect_flee, "enable_perfect_flee", 5, 5, 0, 4095, 0, 4095, "conf/battle/battle.conf") \
	L(casting_rate, "cast_rate", 100, 100, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/skill.conf") \
	L(delay_rate, "delay_rate", 100, 100, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/skill.conf") \
	L(delay_dependon_dex, "delay_dependon_dex", 0, 0, 0, 1, 0, 1, "conf/battle/skill.conf") \
	L(delay_dependon_agi, "delay_dependon_agi", 0, 0, 0, 1, 0, 1, "conf/battle/skill.conf") \
	L(skill_delay_attack_enable, "sdelay_attack_enable", 0, 0, 0, 1, 0, 1, "conf/battle/skill.conf") \
	L(left_cardfix_to_right, "left_cardfix_to_right", 0, 0, 0, 1, 0, 1, "conf/battle/player.conf") \
	L(cardfix_monster_physical, "cardfix_monster_physical", 1, 1, 0, 1, 0, 1, "conf/battle/items.conf") \
	L(skill_add_range, "skill_add_range", 0, 0, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/skill.conf") \
	L(skill_out_range_consume, "skill_out_range_consume", 1, 1, 0, 1, 0, 1, "conf/battle/skill.conf") \
	L(skillrange_by_distance, "skillrange_by_distance", -2, -2, 0, 4095, 0, 4095, "conf/battle/skill.conf") \
	L(skillrange_from_weapon, "use_weapon_skill_range", 0, 0, 0, 4095, 0, 4095, "conf/battle/skill.conf") \
	L(player_damage_delay_rate, "pc_damage_delay_rate", 100, 100, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/battle.conf") \
	L(defunit_not_enemy, "defnotenemy", 0, 0, 0, 1, 0, 1, "conf/battle/skill.conf") \
	L(gvg_traps_target_all, "vs_traps_bctall", 1, 1, 0, 4095, 0, 4095, "conf/battle/skill.conf") \
	L(traps_setting, "traps_setting", 2, 0, 0, 2, 0, 2, "") \
	L(clear_skills_on_death, "clear_unit_ondeath", 0, 0, 0, 4095, 0, 4095, "conf/battle/skill.conf") \
	L(clear_skills_on_warp, "clear_unit_onwarp", 4095, 4095, 0, 4095, 0, 4095, "conf/battle/skill.conf") \
	L(random_monster_checklv, "random_monster_checklv", 0, 0, 0, 1, 0, 1, "conf/battle/items.conf") \
	L(attribute_recover, "attr_recover", 1, 1, 0, 1, 0, 1, "conf/battle/battle.conf") \
	L(flooritem_lifetime, "flooritem_lifetime", 60000, 60000, 1000, RATHENA_I32_MAX, 1000, RATHENA_I32_MAX, "conf/battle/drops.conf") \
	L(item_auto_get, "item_auto_get", 0, 0, 0, 1, 0, 1, "conf/battle/drops.conf") \
	L(first_attack_loot_bonus, "first_attack_loot_bonus", 30, 30, 0, 100, 0, 100, "conf/battle/drops.conf") \
	L(mvp_to_loot_priority, "mvp_to_loot_priority", 0, 0, 0, 1, 0, 1, "conf/battle/drops.conf") \
	L(item_first_get_time, "item_first_get_time", 3000, 3000, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/drops.conf") \
	L(item_second_get_time, "item_second_get_time", 2000, 2000, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/drops.conf") \
	L(item_third_get_time, "item_third_get_time", 2000, 2000, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/drops.conf") \
	L(mvp_item_first_get_time, "mvp_item_first_get_time", 10000, 10000, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/drops.conf") \
	L(mvp_item_second_get_time, "mvp_item_second_get_time", 10000, 10000, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/drops.conf") \
	L(mvp_item_third_get_time, "mvp_item_third_get_time", 2000, 2000, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/drops.conf") \
	L(drop_rate0item, "drop_rate0item", 0, 0, 0, 1, 0, 1, "conf/battle/drops.conf") \
	L(base_exp_rate, "base_exp_rate", 100, 100, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/exp.conf") \
	L(job_exp_rate, "job_exp_rate", 100, 100, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/exp.conf") \
	L(pvp_exp, "pvp_exp", 1, 1, 0, 1, 0, 1, "conf/battle/exp.conf") \
	L(death_penalty_type, "death_penalty_type", 0, 0, 0, 2, 0, 2, "conf/battle/exp.conf") \
	L(death_penalty_base, "death_penalty_base", 0, 0, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/exp.conf") \
	L(death_penalty_job, "death_penalty_job", 0, 0, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/exp.conf") \
	L(zeny_penalty, "zeny_penalty", 0, 0, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/exp.conf") \
	L(hp_rate, "hp_rate", 100, 100, 1, RATHENA_I32_MAX, 1, RATHENA_I32_MAX, "conf/battle/player.conf") \
	L(sp_rate, "sp_rate", 100, 100, 1, RATHENA_I32_MAX, 1, RATHENA_I32_MAX, "conf/battle/player.conf") \
	L(restart_hp_rate, "restart_hp_rate", 0, 0, 0, 100, 0, 100, "conf/battle/player.conf") \
	L(restart_sp_rate, "restart_sp_rate", 0, 0, 0, 100, 0, 100, "conf/battle/player.conf") \
	L(guild_aura, "guild_aura", 31, 31, 0, 31, 0, 31, "conf/battle/skill.conf") \
	L(mvp_hp_rate, "mvp_hp_rate", 100, 100, 1, RATHENA_I32_MAX, 1, RATHENA_I32_MAX, "conf/battle/monster.conf") \
	L(mvp_exp_rate, "mvp_exp_rate", 100, 100, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/exp.conf") \
	L(monster_hp_rate, "monster_hp_rate", 100, 100, 1, RATHENA_I32_MAX, 1, RATHENA_I32_MAX, "conf/battle/monster.conf") \
	L(view_range_rate, "view_range_rate", 100, 100, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/monster.conf") \
	L(chase_range_rate, "chase_range_rate", 100, 100, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/monster.conf") \
	L(gtb_sc_immunity, "gtb_sc_immunity", 50, 50, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/items.conf") \
	L(guild_max_castles, "guild_max_castles", 0, 0, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/guild.conf") \
	L(guild_skill_relog_delay, "guild_skill_relog_delay", 300000, 300000, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/guild.conf") \
	L(guild_skill_relog_type, "guild_skill_relog_type", 0, 1, 0, 1, 0, 1, "") \
	L(emergency_call, "emergency_call", 11, 11, 0, 31, 0, 31, "conf/battle/skill.conf") \
	L(atcommand_spawn_quantity_limit, "atc_spawn_quantity_limit", 100, 100, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/gm.conf") \
	L(atcommand_slave_clone_limit, "atc_slave_clone_limit", 25, 25, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/gm.conf") \
	L(partial_name_scan, "partial_name_scan", 0, 0, 0, 1, 0, 1, "conf/battle/gm.conf") \
	L(player_skillfree, "skillfree", 0, 0, 0, 1, 0, 1, "conf/battle/player.conf") \
	L(player_skillup_limit, "skillup_limit", 1, 1, 0, 1, 0, 1, "conf/battle/player.conf") \
	L(weapon_produce_rate, "wp_rate", 100, 100, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/items.conf") \
	L(potion_produce_rate, "pp_rate", 100, 100, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/items.conf") \
	L(monster_active_enable, "monster_active_enable", 1, 1, 0, 1, 0, 1, "conf/battle/monster.conf") \
	L(monster_damage_delay_rate, "monster_damage_delay_rate", 100, 100, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/monster.conf") \
	L(monster_loot_type, "monster_loot_type", 0, 0, 0, 1, 0, 1, "conf/battle/monster.conf") \
	L(mob_skill_rate, "mob_skill_rate", 100, 100, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/monster.conf") \
	L(mob_skill_delay, "mob_skill_delay", 100, 100, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/monster.conf") \
	L(mob_count_rate, "mob_count_rate", 100, 100, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/monster.conf") \
	L(mob_spawn_delay, "mob_spawn_delay", 100, 100, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/monster.conf") \
	L(plant_spawn_delay, "plant_spawn_delay", 100, 100, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/monster.conf") \
	L(boss_spawn_delay, "boss_spawn_delay", 100, 100, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/monster.conf") \
	L(no_spawn_on_player, "no_spawn_on_player", 0, 0, 0, 100, 0, 100, "conf/battle/monster.conf") \
	L(force_random_spawn, "force_random_spawn", 0, 0, 0, 1, 0, 1, "conf/battle/monster.conf") \
	L(slaves_inherit_mode, "slaves_inherit_mode", 4, 4, 0, 4, 0, 4, "conf/battle/monster.conf") \
	L(slaves_inherit_speed, "slaves_inherit_speed", 3, 3, 0, 3, 0, 3, "conf/battle/monster.conf") \
	L(summons_trigger_autospells, "summons_trigger_autospells", 1, 1, 0, 1, 0, 1, "conf/battle/monster.conf") \
	L(pc_damage_walk_delay_rate, "pc_walk_delay_rate", 20, 20, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/battle.conf") \
	L(damage_walk_delay_rate, "walk_delay_rate", 100, 100, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/battle.conf") \
	L(multihit_delay, "multihit_delay", 80, 80, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/battle.conf") \
	L(infinite_endure, "infinite_endure", 0, 0, 0, 4095, 0, 4095, "conf/battle/battle.conf") \
	L(quest_skill_learn, "quest_skill_learn", 0, 0, 0, 1, 0, 1, "conf/battle/player.conf") \
	L(quest_skill_reset, "quest_skill_reset", 0, 0, 0, 1, 0, 1, "conf/battle/player.conf") \
	L(basic_skill_check, "basic_skill_check", 1, 1, 0, 1, 0, 1, "conf/battle/player.conf") \
	L(guild_emperium_check, "guild_emperium_check", 1, 1, 0, 1, 0, 1, "conf/battle/guild.conf") \
	L(guild_exp_limit, "guild_exp_limit", 50, 50, 0, 99, 0, 99, "conf/battle/guild.conf") \
	L(player_invincible_time, "pc_invincible_time", 3000, 3000, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/player.conf") \
	L(pet_catch_rate, "pet_catch_rate", 100, 100, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/pet.conf") \
	L(pet_rename, "pet_rename", 0, 0, 0, 1, 0, 1, "conf/battle/pet.conf") \
	L(pet_friendly_rate, "pet_friendly_rate", 100, 100, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/pet.conf") \
	L(pet_hungry_delay_rate, "pet_hungry_delay_rate", 100, 100, 10, RATHENA_I32_MAX, 10, RATHENA_I32_MAX, "conf/battle/pet.conf") \
	L(pet_hungry_friendly_decrease, "pet_hungry_friendly_decrease", 5, 5, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "") \
	L(pet_status_support, "pet_status_support", 0, 0, 0, 1, 0, 1, "conf/battle/pet.conf") \
	L(pet_attack_support, "pet_attack_support", 0, 0, 0, 1, 0, 1, "conf/battle/pet.conf") \
	L(pet_damage_support, "pet_damage_support", 0, 0, 0, 1, 0, 1, "conf/battle/pet.conf") \
	L(pet_support_min_friendly, "pet_support_min_friendly", 900, 900, 0, 950, 0, 950, "conf/battle/pet.conf") \
	L(pet_support_rate, "pet_support_rate", 100, 100, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/pet.conf") \
	L(pet_attack_exp_to_master, "pet_attack_exp_to_master", 0, 0, 0, 1, 0, 1, "conf/battle/pet.conf") \
	L(pet_attack_exp_rate, "pet_attack_exp_rate", 100, 100, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/pet.conf") \
	L(pet_lv_rate, "pet_lv_rate", 0, 0, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/pet.conf") \
	L(pet_max_stats, "pet_max_stats", 99, 99, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/pet.conf") \
	L(pet_max_atk1, "pet_max_atk1", 750, 750, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/pet.conf") \
	L(pet_max_atk2, "pet_max_atk2", 1000, 1000, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/pet.conf") \
	L(pet_disable_in_gvg, "pet_no_gvg", 0, 0, 0, 1, 0, 1, "conf/battle/pet.conf") \
	L(pet_master_dead, "pet_master_dead", 0, 0, 0, 1, 0, 1, "conf/battle/pet.conf") \
	L(skill_min_damage, "skill_min_damage", 6, 6, 0, 7, 0, 7, "conf/battle/skill.conf") \
	L(finger_offensive_type, "finger_offensive_type", 0, 0, 0, 1, 0, 1, "conf/battle/skill.conf") \
	L(heal_exp, "heal_exp", 0, 0, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/exp.conf") \
	L(resurrection_exp, "resurrection_exp", 0, 0, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/exp.conf") \
	L(shop_exp, "shop_exp", 0, 0, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/exp.conf") \
	L(max_heal_lv, "max_heal_lv", 11, 11, 1, RATHENA_I32_MAX, 1, RATHENA_I32_MAX, "conf/battle/skill.conf") \
	L(max_heal, "max_heal", 9999, 9999, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/skill.conf") \
	L(combo_delay_rate, "combo_delay_rate", 100, 100, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/skill.conf") \
	L(item_check, "item_check", 0, 0, 0, 7, 0, 7, "conf/battle/items.conf") \
	L(item_use_interval, "item_use_interval", 100, 100, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/items.conf") \
	L(cashfood_use_interval, "cashfood_use_interval", 60000, 60000, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/items.conf") \
	L(wedding_modifydisplay, "wedding_modifydisplay", 0, 0, 0, 1, 0, 1, "conf/battle/client.conf") \
	L(wedding_ignorepalette, "wedding_ignorepalette", 0, 0, 0, 1, 0, 1, "conf/battle/client.conf") \
	L(xmas_ignorepalette, "xmas_ignorepalette", 0, 0, 0, 1, 0, 1, "conf/battle/client.conf") \
	L(summer_ignorepalette, "summer_ignorepalette", 0, 0, 0, 1, 0, 1, "conf/battle/client.conf") \
	L(hanbok_ignorepalette, "hanbok_ignorepalette", 0, 0, 0, 1, 0, 1, "conf/battle/client.conf") \
	L(oktoberfest_ignorepalette, "oktoberfest_ignorepalette", 0, 0, 0, 1, 0, 1, "conf/battle/client.conf") \
	L(natural_healhp_interval, "natural_healhp_interval", 6000, 6000, 500, RATHENA_I32_MAX, 500, RATHENA_I32_MAX, "conf/battle/player.conf") \
	L(natural_healsp_interval, "natural_healsp_interval", 8000, 8000, 500, RATHENA_I32_MAX, 500, RATHENA_I32_MAX, "conf/battle/player.conf") \
	L(natural_heal_skill_interval, "natural_heal_skill_interval", 10000, 10000, 500, RATHENA_I32_MAX, 500, RATHENA_I32_MAX, "conf/battle/player.conf") \
	L(natural_heal_weight_rate, "natural_heal_weight_rate", 70, 50, 0, 100, 0, 100, "") \
	L(open_box_weight_rate, "open_box_weight_rate", 90, 70, 0, 100, 0, 100, "") \
	L(arrow_decrement, "arrow_decrement", 1, 1, 0, 2, 0, 2, "conf/battle/battle.conf") \
	L(ammo_unequip, "ammo_unequip", 1, 1, 0, 1, 0, 1, "conf/battle/battle.conf") \
	L(ammo_check_weapon, "ammo_check_weapon", 1, 1, 0, 1, 0, 1, "conf/battle/battle.conf") \
	L(max_aspd, "max_aspd", 190, 190, 100, 199, 100, 199, "conf/battle/player.conf") \
	L(max_third_aspd, "max_third_aspd", 193, 193, 100, 199, 100, 199, "conf/battle/player.conf") \
	L(max_summoner_aspd, "max_summoner_aspd", 193, 193, 100, 199, 100, 199, "conf/battle/player.conf") \
	L(max_walk_speed, "max_walk_speed", 300, 300, 100, 15000, 100, 15000, "conf/battle/player.conf") \
	L(max_lv, "max_lv", 99, 99, 0, 275, 0, 275, "conf/battle/client.conf") \
	L(aura_lv, "aura_lv", 99, 99, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/client.conf") \
	L(max_hp_lv99, "max_hp_lv99", 330000, 330000, 100, 1000000000, 100, 1000000000, "conf/battle/player.conf") \
	L(max_hp_lv150, "max_hp_lv150", 660000, 660000, 100, 1000000000, 100, 1000000000, "conf/battle/player.conf") \
	L(max_hp, "max_hp", 1100000, 1100000, 100, 1000000000, 100, 1000000000, "conf/battle/player.conf") \
	L(max_sp, "max_sp", 32500, 32500, 100, 1000000000, 100, 1000000000, "conf/battle/player.conf") \
	L(max_cart_weight, "max_cart_weight", 8000, 8000, 100, 1000000, 100, 1000000, "conf/battle/player.conf") \
	L(max_parameter, "max_parameter", 99, 99, 10, 32767, 10, 32767, "conf/battle/player.conf") \
	L(max_baby_parameter, "max_baby_parameter", 80, 80, 10, 32767, 10, 32767, "conf/battle/player.conf") \
	L(max_def, "max_def", 99, 99, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/player.conf") \
	L(over_def_bonus, "over_def_bonus", 0, 0, 0, 1000, 0, 1000, "conf/battle/player.conf") \
	L(skill_log, "skill_log", 0, 0, 0, 4095, 0, 4095, "conf/battle/misc.conf") \
	L(battle_log, "battle_log", 0, 0, 0, 1, 0, 1, "conf/battle/misc.conf") \
	L(etc_log, "etc_log", 1, 1, 0, 1, 0, 1, "conf/battle/misc.conf") \
	L(save_clothcolor, "save_clothcolor", 1, 1, 0, 1, 0, 1, "conf/battle/client.conf") \
	L(undead_detect_type, "undead_detect_type", 0, 0, 0, 3, 0, 3, "conf/battle/battle.conf") \
	L(auto_counter_type, "auto_counter_type", 4095, 4095, 0, 4095, 0, 4095, "conf/battle/skill.conf") \
	L(min_hitrate, "min_hitrate", 5, 5, 0, 100, 0, 100, "conf/battle/battle.conf") \
	L(max_hitrate, "max_hitrate", 100, 100, 0, 100, 0, 100, "conf/battle/battle.conf") \
	L(agi_penalty_target, "agi_penalty_target", 1, 1, 0, 4095, 0, 4095, "conf/battle/battle.conf") \
	L(agi_penalty_type, "agi_penalty_type", 1, 1, 0, 2, 0, 2, "conf/battle/battle.conf") \
	L(agi_penalty_count, "agi_penalty_count", 3, 3, 2, RATHENA_I32_MAX, 2, RATHENA_I32_MAX, "conf/battle/battle.conf") \
	L(agi_penalty_num, "agi_penalty_num", 10, 10, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/battle.conf") \
	L(vit_penalty_target, "vit_penalty_target", 1, 1, 0, 4095, 0, 4095, "conf/battle/battle.conf") \
	L(vit_penalty_type, "vit_penalty_type", 1, 1, 0, 2, 0, 2, "conf/battle/battle.conf") \
	L(vit_penalty_count, "vit_penalty_count", 3, 3, 2, RATHENA_I32_MAX, 2, RATHENA_I32_MAX, "conf/battle/battle.conf") \
	L(vit_penalty_num, "vit_penalty_num", 5, 5, 1, RATHENA_I32_MAX, 1, RATHENA_I32_MAX, "conf/battle/battle.conf") \
	L(weapon_defense_type, "weapon_defense_type", 0, 0, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/battle.conf") \
	L(magic_defense_type, "magic_defense_type", 0, 0, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/battle.conf") \
	L(skill_reiteration, "skill_reiteration", 0, 0, 0, 4095, 0, 4095, "conf/battle/skill.conf") \
	L(skill_nofootset, "skill_nofootset", 1, 1, 0, 4095, 0, 4095, "conf/battle/skill.conf") \
	L(player_cloak_check_type, "pc_cloak_check_type", 1, 1, 0, 7, 0, 7, "conf/battle/skill.conf") \
	L(monster_cloak_check_type, "monster_cloak_check_type", 4, 4, 0, 7, 0, 7, "conf/battle/skill.conf") \
	L(sense_type, "estimation_type", 3, 3, 0, 3, 0, 3, "conf/battle/skill.conf") \
	L(gvg_short_attack_damage_rate, "gvg_short_damage_rate", 80, 80, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/guild.conf") \
	L(gvg_long_attack_damage_rate, "gvg_long_damage_rate", 80, 80, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/guild.conf") \
	L(gvg_weapon_attack_damage_rate, "gvg_weapon_damage_rate", 60, 60, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/guild.conf") \
	L(gvg_magic_attack_damage_rate, "gvg_magic_damage_rate", 60, 60, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/guild.conf") \
	L(gvg_misc_attack_damage_rate, "gvg_misc_damage_rate", 60, 60, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/guild.conf") \
	L(gvg_flee_penalty, "gvg_flee_penalty", 20, 20, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/guild.conf") \
	L(pk_short_attack_damage_rate, "pk_short_damage_rate", 80, 80, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/misc.conf") \
	L(pk_long_attack_damage_rate, "pk_long_damage_rate", 70, 70, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/misc.conf") \
	L(pk_weapon_attack_damage_rate, "pk_weapon_damage_rate", 60, 60, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/misc.conf") \
	L(pk_magic_attack_damage_rate, "pk_magic_damage_rate", 60, 60, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/misc.conf") \
	L(pk_misc_attack_damage_rate, "pk_misc_damage_rate", 60, 60, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/misc.conf") \
	L(mob_changetarget_byskill, "mob_changetarget_byskill", 0, 0, 0, 1, 0, 1, "conf/battle/monster.conf") \
	L(attack_direction_change, "attack_direction_change", 4095, 4095, 0, 4095, 0, 4095, "conf/battle/battle.conf") \
	L(land_skill_limit, "land_skill_limit", 4095, 4095, 0, 4095, 0, 4095, "conf/battle/skill.conf") \
	L(monster_class_change_full_recover, "monster_class_change_recover", 1, 1, 0, 1, 0, 1, "conf/battle/monster.conf") \
	L(produce_item_name_input, "produce_item_name_input", 3, 3, 0, 159, 0, 159, "conf/battle/items.conf") \
	L(display_skill_fail, "display_skill_fail", 2, 2, 0, 15, 0, 15, "conf/battle/skill.conf") \
	L(chat_warpportal, "chat_warpportal", 0, 0, 0, 1, 0, 1, "conf/battle/skill.conf") \
	L(mob_warp, "mob_warp", 0, 0, 0, 15, 0, 15, "conf/battle/monster.conf") \
	L(dead_branch_active, "dead_branch_active", 1, 1, 0, 1, 0, 1, "conf/battle/items.conf") \
	L(vending_max_value, "vending_max_value", 10000000, 10000000, 1, RATHENA_I32_MAX, 1, RATHENA_I32_MAX, "conf/battle/items.conf") \
	L(vending_over_max, "vending_over_max", 1, 1, 0, 1, 0, 1, "conf/battle/items.conf") \
	L(show_steal_in_same_party, "show_steal_in_same_party", 0, 0, 0, 1, 0, 1, "conf/battle/party.conf") \
	L(party_hp_mode, "party_hp_mode", 0, 0, 0, 1, 0, 1, "conf/battle/party.conf") \
	L(show_party_share_picker, "party_show_share_picker", 1, 1, 0, 1, 0, 1, "conf/battle/party.conf") \
	L(show_picker_item_type, "show_picker_item_type", 112, 112, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/party.conf") \
	L(party_update_interval, "party_update_interval", 1000, 1000, 100, RATHENA_I32_MAX, 100, RATHENA_I32_MAX, "conf/battle/party.conf") \
	L(party_item_share_type, "party_share_type", 0, 0, 0, 3, 0, 3, "conf/battle/party.conf") \
	L(attack_attr_none, "attack_attr_none", -2, -2, 0, 4095, 0, 4095, "conf/battle/battle.conf") \
	L(gx_allhit, "gx_allhit", 0, 0, 0, 1, 0, 1, "conf/battle/skill.conf") \
	L(gx_disptype, "gx_disptype", 1, 1, 0, 1, 0, 1, "conf/battle/skill.conf") \
	L(devotion_level_difference, "devotion_level_difference", 10, 10, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/skill.conf") \
	L(player_skill_partner_check, "player_skill_partner_check", 1, 1, 0, 1, 0, 1, "conf/battle/skill.conf") \
	L(invite_request_check, "invite_request_check", 1, 1, 0, 1, 0, 1, "conf/battle/player.conf") \
	L(skill_removetrap_type, "skill_removetrap_type", 0, 0, 0, 1, 0, 1, "conf/battle/skill.conf") \
	L(disp_experience, "disp_experience", 0, 0, 0, 1, 0, 1, "conf/battle/exp.conf") \
	L(disp_zeny, "disp_zeny", 0, 0, 0, 1, 0, 1, "conf/battle/exp.conf") \
	L(bone_drop, "bone_drop", 0, 0, 0, 2, 0, 2, "conf/battle/player.conf") \
	L(buyer_name, "buyer_name", 1, 1, 0, 1, 0, 1, "conf/battle/items.conf") \
	L(skill_wall_check, "skill_wall_check", 1, 1, 0, 1, 0, 1, "conf/battle/skill.conf") \
	L(official_cell_stack_limit, "official_cell_stack_limit", 1, 1, 0, 255, 0, 255, "conf/battle/misc.conf") \
	L(custom_cell_stack_limit, "custom_cell_stack_limit", 1, 1, 1, 255, 1, 255, "conf/battle/misc.conf") \
	L(dancing_weaponswitch_fix, "dancing_weaponswitch_fix", 1, 1, 0, 1, 0, 1, "conf/battle/skill.conf") \
	L(item_logarithmic_drops, "logarithmic_drops", 0, 0, 0, 1, 0, 1, "conf/battle/drops.conf") \
	L(item_drop_common_min, "item_drop_common_min", 1, 1, 0, 10000, 0, 10000, "conf/battle/drops.conf") \
	L(item_drop_common_max, "item_drop_common_max", 10000, 10000, 1, 10000, 1, 10000, "conf/battle/drops.conf") \
	L(item_drop_equip_min, "item_drop_equip_min", 1, 1, 0, 10000, 0, 10000, "conf/battle/drops.conf") \
	L(item_drop_equip_max, "item_drop_equip_max", 10000, 10000, 1, 10000, 1, 10000, "conf/battle/drops.conf") \
	L(item_drop_card_min, "item_drop_card_min", 1, 1, 0, 10000, 0, 10000, "conf/battle/drops.conf") \
	L(item_drop_card_max, "item_drop_card_max", 10000, 10000, 1, 10000, 1, 10000, "conf/battle/drops.conf") \
	L(item_drop_mvp_min, "item_drop_mvp_min", 1, 1, 0, 10000, 0, 10000, "conf/battle/drops.conf") \
	L(item_drop_mvp_max, "item_drop_mvp_max", 10000, 10000, 1, 10000, 1, 10000, "conf/battle/drops.conf") \
	L(item_drop_mvp_mode, "item_drop_mvp_mode", 0, 0, 0, 2, 0, 2, "conf/battle/drops.conf") \
	L(item_drop_heal_min, "item_drop_heal_min", 1, 1, 0, 10000, 0, 10000, "conf/battle/drops.conf") \
	L(item_drop_heal_max, "item_drop_heal_max", 10000, 10000, 1, 10000, 1, 10000, "conf/battle/drops.conf") \
	L(item_drop_use_min, "item_drop_use_min", 1, 1, 0, 10000, 0, 10000, "conf/battle/drops.conf") \
	L(item_drop_use_max, "item_drop_use_max", 10000, 10000, 1, 10000, 1, 10000, "conf/battle/drops.conf") \
	L(item_drop_add_min, "item_drop_adddrop_min", 1, 1, 0, 10000, 0, 10000, "conf/battle/drops.conf") \
	L(item_drop_add_max, "item_drop_adddrop_max", 10000, 10000, 1, 10000, 1, 10000, "conf/battle/drops.conf") \
	L(item_group_drop_min, "item_group_drop_min", 1, 1, 0, 10000, 0, 10000, "conf/battle/drops.conf") \
	L(item_group_drop_max, "item_group_drop_max", 10000, 10000, 1, 10000, 1, 10000, "conf/battle/drops.conf") \
	L(item_drop_treasure_min, "item_drop_treasure_min", 1, 1, 0, 10000, 0, 10000, "conf/battle/drops.conf") \
	L(item_drop_treasure_max, "item_drop_treasure_max", 10000, 10000, 1, 10000, 1, 10000, "conf/battle/drops.conf") \
	L(item_rate_mvp, "item_rate_mvp", 100, 100, 0, 1000000, 0, 1000000, "conf/battle/drops.conf") \
	L(item_rate_common, "item_rate_common", 100, 100, 0, 1000000, 0, 1000000, "conf/battle/drops.conf") \
	L(item_rate_common_boss, "item_rate_common_boss", 100, 100, 0, 1000000, 0, 1000000, "conf/battle/drops.conf") \
	L(item_rate_common_mvp, "item_rate_common_mvp", 100, 100, 0, 1000000, 0, 1000000, "conf/battle/drops.conf") \
	L(item_rate_equip, "item_rate_equip", 100, 100, 0, 1000000, 0, 1000000, "conf/battle/drops.conf") \
	L(item_rate_equip_boss, "item_rate_equip_boss", 100, 100, 0, 1000000, 0, 1000000, "conf/battle/drops.conf") \
	L(item_rate_equip_mvp, "item_rate_equip_mvp", 100, 100, 0, 1000000, 0, 1000000, "conf/battle/drops.conf") \
	L(item_rate_card, "item_rate_card", 100, 100, 0, 1000000, 0, 1000000, "conf/battle/drops.conf") \
	L(item_rate_card_boss, "item_rate_card_boss", 100, 100, 0, 1000000, 0, 1000000, "conf/battle/drops.conf") \
	L(item_rate_card_mvp, "item_rate_card_mvp", 100, 100, 0, 1000000, 0, 1000000, "conf/battle/drops.conf") \
	L(item_rate_heal, "item_rate_heal", 100, 100, 0, 1000000, 0, 1000000, "conf/battle/drops.conf") \
	L(item_rate_heal_boss, "item_rate_heal_boss", 100, 100, 0, 1000000, 0, 1000000, "conf/battle/drops.conf") \
	L(item_rate_heal_mvp, "item_rate_heal_mvp", 100, 100, 0, 1000000, 0, 1000000, "conf/battle/drops.conf") \
	L(item_rate_use, "item_rate_use", 100, 100, 0, 1000000, 0, 1000000, "conf/battle/drops.conf") \
	L(item_rate_use_boss, "item_rate_use_boss", 100, 100, 0, 1000000, 0, 1000000, "conf/battle/drops.conf") \
	L(item_rate_use_mvp, "item_rate_use_mvp", 100, 100, 0, 1000000, 0, 1000000, "conf/battle/drops.conf") \
	L(item_rate_adddrop, "item_rate_adddrop", 100, 100, 0, 1000000, 0, 1000000, "conf/battle/drops.conf") \
	L(item_group_rate, "item_group_rate", 100, 100, 0, 1000000, 0, 1000000, "conf/battle/drops.conf") \
	L(item_rate_treasure, "item_rate_treasure", 100, 100, 0, 1000000, 0, 1000000, "conf/battle/drops.conf") \
	L(prevent_logout, "prevent_logout", 10000, 10000, 0, 60000, 0, 60000, "conf/battle/player.conf") \
	L(prevent_logout_trigger, "prevent_logout_trigger", 14, 14, 0, 15, 0, 15, "conf/battle/player.conf") \
	L(alchemist_summon_reward, "alchemist_summon_reward", 1, 1, 0, 2, 0, 2, "conf/battle/drops.conf") \
	L(drops_by_luk, "drops_by_luk", 0, 0, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/drops.conf") \
	L(drops_by_luk2, "drops_by_luk2", 0, 0, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/drops.conf") \
	L(equip_natural_break_rate, "equip_natural_break_rate", 0, 0, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/battle.conf") \
	L(equip_self_break_rate, "equip_self_break_rate", 100, 100, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/battle.conf") \
	L(equip_skill_break_rate, "equip_skill_break_rate", 100, 100, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/battle.conf") \
	L(pk_mode, "pk_mode", 0, 0, 0, 2, 0, 2, "conf/battle/misc.conf") \
	L(pk_mode_mes, "pk_mode_mes", 1, 1, 0, 1, 0, 1, "conf/battle/misc.conf") \
	L(pk_level_range, "pk_level_range", 0, 0, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/misc.conf") \
	L(manner_system, "manner_system", 4095, 4095, 0, 4095, 0, 4095, "conf/battle/misc.conf") \
	L(pet_equip_required, "pet_equip_required", 0, 0, 0, 1, 0, 1, "conf/battle/pet.conf") \
	L(pet_unequip_destroy, "pet_unequip_destroy", 1, 1, 0, 1, 0, 1, "conf/battle/pet.conf") \
	L(multi_level_up, "multi_level_up", 0, 0, 0, 1, 0, 1, "conf/battle/exp.conf") \
	L(multi_level_up_base, "multi_level_up_base", 0, 0, 0, 275, 0, 275, "conf/battle/exp.conf") \
	L(multi_level_up_job, "multi_level_up_job", 0, 0, 0, 275, 0, 275, "conf/battle/exp.conf") \
	L(max_exp_gain_rate, "max_exp_gain_rate", 0, 0, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/exp.conf") \
	L(backstab_bow_penalty, "backstab_bow_penalty", 0, 0, 0, 1, 0, 1, "conf/battle/skill.conf") \
	L(night_at_start, "night_at_start", 0, 0, 0, 1, 0, 1, "conf/battle/misc.conf") \
	L(show_mob_info, "show_mob_info", 0, 0, 0, 7, 0, 7, "conf/battle/monster.conf") \
	L(ban_hack_trade, "ban_hack_trade", 0, 0, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/gm.conf") \
	L(min_hair_style, "min_hair_style", 0, 0, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/client.conf") \
	L(max_hair_style, "max_hair_style", 23, 23, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/client.conf") \
	L(min_hair_color, "min_hair_color", 0, 0, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/client.conf") \
	L(max_hair_color, "max_hair_color", 9, 9, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/client.conf") \
	L(min_cloth_color, "min_cloth_color", 0, 0, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/client.conf") \
	L(max_cloth_color, "max_cloth_color", 4, 4, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/client.conf") \
	L(pet_hair_style, "pet_hair_style", 100, 100, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/client.conf") \
	L(castrate_dex_scale, "castrate_dex_scale", 150, 150, 1, RATHENA_I32_MAX, 1, RATHENA_I32_MAX, "conf/battle/skill.conf") \
	L(vcast_stat_scale, "vcast_stat_scale", 530, 530, 1, RATHENA_I32_MAX, 1, RATHENA_I32_MAX, "conf/battle/skill.conf") \
	L(area_size, "area_size", 14, 14, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/client.conf") \
	L(zeny_from_mobs, "zeny_from_mobs", 0, 0, 0, 1, 0, 1, "conf/battle/monster.conf") \
	L(mobs_level_up, "mobs_level_up", 0, 0, 0, 1, 0, 1, "conf/battle/monster.conf") \
	L(mobs_level_up_exp_rate, "mobs_level_up_exp_rate", 1, 1, 1, RATHENA_I32_MAX, 1, RATHENA_I32_MAX, "conf/battle/monster.conf") \
	L(pk_min_level, "pk_min_level", 55, 55, 1, RATHENA_I32_MAX, 1, RATHENA_I32_MAX, "conf/battle/misc.conf") \
	L(skill_steal_max_tries, "skill_steal_max_tries", 0, 0, 0, 255, 0, 255, "conf/battle/skill.conf") \
	L(skill_steal_random_options, "skill_steal_random_options", 0, 0, 0, 1, 0, 1, "conf/battle/skill.conf") \
	L(motd_type, "motd_type", 0, 0, 0, 1, 0, 1, "conf/battle/client.conf") \
	L(exp_calc_type, "exp_calc_type", 0, 0, 0, 2, 0, 2, "conf/battle/exp.conf") \
	L(exp_bonus_attacker, "exp_bonus_attacker", 25, 25, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/exp.conf") \
	L(exp_bonus_max_attacker, "exp_bonus_max_attacker", 12, 12, 2, RATHENA_I32_MAX, 2, RATHENA_I32_MAX, "conf/battle/exp.conf") \
	L(exp_bonus_nodamage_attacker, "exp_bonus_nodamage_attacker", 0, 0, 0, 1, 0, 1, "conf/battle/exp.conf") \
	L(min_skill_delay_limit, "min_skill_delay_limit", 100, 100, 10, RATHENA_I32_MAX, 10, RATHENA_I32_MAX, "conf/battle/skill.conf") \
	L(amotion_min_skill_delay, "amotion_min_skill_delay", 0, 0, 0, 1, 0, 1, "conf/battle/skill.conf") \
	L(default_walk_delay, "default_walk_delay", 300, 300, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/skill.conf") \
	L(no_skill_delay, "no_skill_delay", 2, 2, 0, 4095, 0, 4095, "conf/battle/skill.conf") \
	L(attack_walk_delay, "attack_walk_delay", 0, 0, 0, 4095, 0, 4095, "conf/battle/battle.conf") \
	L(damage_walk_delay, "damage_walk_delay", 0, 0, 0, 4095, 0, 4095, "conf/battle/battle.conf") \
	L(require_glory_guild, "require_glory_guild", 0, 0, 0, 1, 0, 1, "conf/battle/guild.conf") \
	L(idle_no_share, "idle_no_share", 0, 0, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/party.conf") \
	L(party_even_share_bonus, "party_even_share_bonus", 0, 0, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/party.conf") \
	L(delay_battle_damage, "delay_battle_damage", 1, 1, 0, 1, 0, 1, "conf/battle/battle.conf") \
	L(hide_woe_damage, "hide_woe_damage", 0, 0, 0, 1, 0, 1, "conf/battle/client.conf") \
	L(display_version, "display_version", 1, 1, 0, 1, 0, 1, "conf/battle/client.conf") \
	L(display_hallucination, "display_hallucination", 1, 1, 0, 1, 0, 1, "conf/battle/client.conf") \
	L(use_statpoint_table, "use_statpoint_table", 1, 1, 0, 1, 0, 1, "conf/battle/exp.conf") \
	L(debuff_on_logout, "debuff_on_logout", 0, 0, 0, 3, 0, 3, "conf/battle/status.conf") \
	L(monster_ai, "mob_ai", 0, 0, 0, 16383, 0, 16383, "conf/battle/monster.conf") \
	L(hom_setting, "hom_setting", 65535, 65535, 0, 65535, 0, 65535, "conf/battle/homunc.conf") \
	L(dynamic_mobs, "dynamic_mobs", 1, 1, 0, 1, 0, 1, "conf/battle/monster.conf") \
	L(mob_remove_damaged, "mob_remove_damaged", 1, 1, 0, 1, 0, 1, "conf/battle/monster.conf") \
	L(show_hp_sp_drain, "show_hp_sp_drain", 0, 0, 0, 1, 0, 1, "conf/battle/player.conf") \
	L(show_hp_sp_gain, "show_hp_sp_gain", 1, 1, 0, 1, 0, 1, "conf/battle/player.conf") \
	L(mob_npc_event_type, "mob_npc_event_type", 1, 1, 0, 1, 0, 1, "conf/battle/monster.conf") \
	L(character_size, "character_size", 3, 3, 0, 3, 0, 3, "conf/battle/player.conf") \
	L(mob_max_skilllvl, "mob_max_skilllvl", 100, 100, 1, 100, 1, 100, "conf/battle/skill.conf") \
	L(retaliate_to_master, "retaliate_to_master", 1, 1, 0, 1, 0, 1, "conf/battle/monster.conf") \
	L(rare_drop_announce, "rare_drop_announce", 0, 0, 0, 10000, 0, 10000, "conf/battle/drops.conf") \
	L(drop_rate_cap, "drop_rate_cap", 9000, 9000, 0, 10000, 0, 10000, "conf/battle/drops.conf") \
	L(drop_rate_cap_vip, "drop_rate_cap_vip", 9000, 9000, 0, 10000, 0, 10000, "conf/battle/drops.conf") \
	L(duel_allow_pvp, "duel_allow_pvp", 0, 0, 0, 1, 0, 1, "conf/battle/misc.conf") \
	L(duel_allow_gvg, "duel_allow_gvg", 0, 0, 0, 1, 0, 1, "conf/battle/misc.conf") \
	L(duel_allow_teleport, "duel_allow_teleport", 0, 0, 0, 1, 0, 1, "conf/battle/misc.conf") \
	L(duel_autoleave_when_die, "duel_autoleave_when_die", 1, 1, 0, 1, 0, 1, "conf/battle/misc.conf") \
	L(duel_time_interval, "duel_time_interval", 60, 60, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/misc.conf") \
	L(duel_only_on_same_map, "duel_only_on_same_map", 0, 0, 0, 1, 0, 1, "conf/battle/misc.conf") \
	L(skip_teleport_lv1_menu, "skip_teleport_lv1_menu", 0, 0, 0, 1, 0, 1, "conf/battle/skill.conf") \
	L(allow_skill_without_day, "allow_skill_without_day", 0, 0, 0, 1, 0, 1, "conf/battle/skill.conf") \
	L(allow_es_magic_player, "allow_es_magic_pc", 0, 0, 0, 1, 0, 1, "conf/battle/skill.conf") \
	L(skill_caster_check, "skill_caster_check", 1, 1, 0, 1, 0, 1, "conf/battle/skill.conf") \
	L(status_cast_cancel, "sc_castcancel", 0, 0, 0, 4095, 0, 4095, "conf/battle/status.conf") \
	L(pc_status_def_rate, "pc_sc_def_rate", 100, 100, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/status.conf") \
	L(mob_status_def_rate, "mob_sc_def_rate", 100, 100, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/status.conf") \
	L(pc_max_status_def, "pc_max_sc_def", 100, 100, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/status.conf") \
	L(mob_max_status_def, "mob_max_sc_def", 100, 100, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/status.conf") \
	L(sg_miracle_skill_ratio, "sg_miracle_skill_ratio", 1, 1, 0, 20000, 0, 20000, "conf/battle/skill.conf") \
	L(sg_angel_skill_ratio, "sg_angel_skill_ratio", 10, 10, 0, 10000, 0, 10000, "conf/battle/skill.conf") \
	L(autospell_stacking, "autospell_stacking", 0, 0, 0, 1, 0, 1, "conf/battle/items.conf") \
	L(override_mob_names, "override_mob_names", 0, 0, 0, 2, 0, 2, "conf/battle/monster.conf") \
	L(min_chat_delay, "min_chat_delay", 0, 0, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/client.conf") \
	L(friend_auto_add, "friend_auto_add", 1, 1, 0, 1, 0, 1, "conf/battle/player.conf") \
	L(hom_rename, "hom_rename", 0, 0, 0, 1, 0, 1, "conf/battle/homunc.conf") \
	L(homunculus_show_growth, "homunculus_show_growth", 0, 0, 0, 1, 0, 1, "conf/battle/homunc.conf") \
	L(homunculus_friendly_rate, "homunculus_friendly_rate", 100, 100, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/homunc.conf") \
	L(vending_tax, "vending_tax", 0, 0, 0, 10000, 0, 10000, "conf/battle/items.conf") \
	L(vending_tax_min, "vending_tax_min", 0, 0, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/items.conf") \
	L(day_duration, "day_duration", 0, 0, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/misc.conf") \
	L(night_duration, "night_duration", 0, 0, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/misc.conf") \
	L(mob_remove_delay, "mob_remove_delay", 60000, 60000, 1000, RATHENA_I32_MAX, 1000, RATHENA_I32_MAX, "conf/battle/monster.conf") \
	L(mob_active_time, "mob_active_time", 5000, 5000, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/monster.conf") \
	L(boss_active_time, "boss_active_time", 5000, 5000, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/monster.conf") \
	L(sg_miracle_skill_duration, "sg_miracle_skill_duration", 3600000, 3600000, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/skill.conf") \
	L(hvan_explosion_intimate, "hvan_explosion_intimate", 45000, 45000, 0, 100000, 0, 100000, "conf/battle/homunc.conf") \
	L(quest_exp_rate, "quest_exp_rate", 100, 100, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/exp.conf") \
	L(at_mapflag, "autotrade_mapflag", 0, 0, 0, 1, 0, 1, "conf/battle/misc.conf") \
	L(at_timeout, "at_timeout", 0, 0, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/misc.conf") \
	L(homunculus_autoloot, "homunculus_autoloot", 0, 0, 0, 1, 0, 1, "conf/battle/homunc.conf") \
	L(idle_no_autoloot, "idle_no_autoloot", 0, 0, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/player.conf") \
	L(max_guild_alliance, "max_guild_alliance", 3, 3, 0, 3, 0, 3, "conf/battle/guild.conf") \
	L(ksprotection, "ksprotection", 5000, 5000, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/monster.conf") \
	L(auction_feeperhour, "auction_feeperhour", 12000, 12000, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/misc.conf") \
	L(auction_maximumprice, "auction_maximumprice", 500000000, 500000000, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/misc.conf") \
	L(homunculus_auto_vapor, "homunculus_auto_vapor", 80, 80, 0, 100, 0, 100, "conf/battle/homunc.conf") \
	L(display_status_timers, "display_status_timers", 1, 1, 0, 1, 0, 1, "conf/battle/client.conf") \
	L(skill_add_heal_rate, "skill_add_heal_rate", 487, 487, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/skill.conf") \
	L(eq_single_target_reflectable, "eq_single_target_reflectable", 1, 1, 0, 1, 0, 1, "conf/battle/skill.conf") \
	L(invincible_nodamage, "invincible_nodamage", 0, 0, 0, 1, 0, 1, "conf/battle/skill.conf") \
	L(mob_slave_keep_target, "mob_slave_keep_target", 0, 0, 0, 1, 0, 1, "conf/battle/monster.conf") \
	L(autospell_check_range, "autospell_check_range", 0, 0, 0, 1, 0, 1, "conf/battle/battle.conf") \
	L(knockback_left, "knockback_left", 1, 1, 0, 1, 0, 1, "conf/battle/battle.conf") \
	L(client_reshuffle_dice, "client_reshuffle_dice", 0, 0, 0, 1, 0, 1, "conf/battle/client.conf") \
	L(client_sort_storage, "client_sort_storage", 0, 0, 0, 1, 0, 1, "conf/battle/client.conf") \
	L(feature_buying_store, "feature_buying_store", 1, 1, 0, 1, 0, 1, "conf/battle/feature.conf") \
	L(feature_search_stores, "feature_search_stores", 1, 1, 0, 1, 0, 1, "conf/battle/feature.conf") \
	L(searchstore_querydelay, "searchstore_querydelay", 10, 10, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/misc.conf") \
	L(searchstore_maxresults, "searchstore_maxresults", 30, 30, 1, RATHENA_I32_MAX, 1, RATHENA_I32_MAX, "conf/battle/misc.conf") \
	L(display_party_name, "display_party_name", 0, 0, 0, 1, 0, 1, "conf/battle/party.conf") \
	L(cashshop_show_points, "cashshop_show_points", 0, 0, 0, 1, 0, 1, "conf/battle/misc.conf") \
	L(mail_show_status, "mail_show_status", 0, 0, 0, 2, 0, 2, "conf/battle/misc.conf") \
	L(client_limit_unit_lv, "client_limit_unit_lv", 0, 0, 0, 4095, 0, 4095, "conf/battle/client.conf") \
	L(land_protector_behavior, "land_protector_behavior", 0, 0, 0, 1, 0, 1, "conf/battle/skill.conf") \
	L(npc_emotion_behavior, "npc_emotion_behavior", 0, 0, 0, 1, 0, 1, "conf/battle/skill.conf") \
	L(bg_update_interval, "bg_update_interval", 1000, 1000, 100, RATHENA_I32_MAX, 100, RATHENA_I32_MAX, "conf/battle/battleground.conf") \
	L(bg_short_attack_damage_rate, "bg_short_damage_rate", 80, 80, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/battleground.conf") \
	L(bg_long_attack_damage_rate, "bg_long_damage_rate", 80, 80, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/battleground.conf") \
	L(bg_weapon_attack_damage_rate, "bg_weapon_damage_rate", 60, 60, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/battleground.conf") \
	L(bg_magic_attack_damage_rate, "bg_magic_damage_rate", 60, 60, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/battleground.conf") \
	L(bg_misc_attack_damage_rate, "bg_misc_damage_rate", 60, 60, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/battleground.conf") \
	L(bg_flee_penalty, "bg_flee_penalty", 20, 20, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/battleground.conf") \
	L(max_third_parameter, "max_third_parameter", 135, 135, 10, 32767, 10, 32767, "conf/battle/player.conf") \
	L(max_baby_third_parameter, "max_baby_third_parameter", 108, 108, 10, 32767, 10, 32767, "conf/battle/player.conf") \
	L(max_trans_parameter, "max_trans_parameter", 99, 99, 10, 32767, 10, 32767, "conf/battle/player.conf") \
	L(max_third_trans_parameter, "max_third_trans_parameter", 135, 135, 10, 32767, 10, 32767, "conf/battle/player.conf") \
	L(max_extended_parameter, "max_extended_parameter", 125, 125, 10, 32767, 10, 32767, "conf/battle/player.conf") \
	L(max_summoner_parameter, "max_summoner_parameter", 120, 120, 10, 32767, 10, 32767, "conf/battle/player.conf") \
	L(max_fourth_parameter, "max_fourth_parameter", 135, 135, 10, 32767, 10, 32767, "conf/battle/player.conf") \
	L(skill_amotion_leniency, "skill_amotion_leniency", 0, 0, 0, 300, 0, 300, "conf/battle/skill.conf") \
	L(mvp_tomb_enabled, "mvp_tomb_enabled", 1, 1, 0, 1, 0, 1, "conf/battle/monster.conf") \
	L(mvp_tomb_delay, "mvp_tomb_delay", 9000, 9000, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/monster.conf") \
	L(feature_atcommand_suggestions, "atcommand_suggestions_enabled", 0, 0, 0, 1, 0, 1, "conf/battle/feature.conf") \
	L(min_npc_vendchat_distance, "min_npc_vendchat_distance", 3, 3, 0, 100, 0, 100, "conf/battle/player.conf") \
	L(atcommand_mobinfo_type, "atcommand_mobinfo_type", 0, 0, 0, 1, 0, 1, "conf/battle/gm.conf") \
	L(homunculus_max_level, "hom_max_level", 99, 99, 0, 275, 0, 275, "conf/battle/homunc.conf") \
	L(homunculus_S_max_level, "hom_S_max_level", 150, 150, 0, 275, 0, 275, "conf/battle/homunc.conf") \
	L(mob_size_influence, "mob_size_influence", 0, 0, 0, 1, 0, 1, "conf/battle/monster.conf") \
	L(skill_trap_type, "skill_trap_type", 0, 0, 0, 3, 0, 3, "conf/battle/skill.conf") \
	L(multi_trigger_trap, "multi_trigger_trap", 0, 0, 0, 1, 0, 1, "conf/battle/skill.conf") \
	L(allow_consume_restricted_item, "allow_consume_restricted_item", 1, 1, 0, 1, 0, 1, "conf/battle/items.conf") \
	L(allow_equip_restricted_item, "allow_equip_restricted_item", 1, 1, 0, 1, 0, 1, "conf/battle/items.conf") \
	L(max_walk_path, "max_walk_path", 17, 17, 1, 32, 1, 32, "conf/battle/client.conf") \
	L(item_enabled_npc, "item_enabled_npc", 0, 1, 0, 1, 0, 1, "") \
	L(item_flooritem_check, "item_onfloor", 1, 1, 0, 1, 0, 1, "conf/battle/items.conf") \
	L(bowling_bash_area, "bowling_bash_area", 0, 0, 0, 20, 0, 20, "conf/battle/skill.conf") \
	L(drop_rateincrease, "drop_rateincrease", 0, 0, 0, 1, 0, 1, "conf/battle/drops.conf") \
	L(feature_auction, "feature_auction", 0, 0, 0, 2, 0, 2, "conf/battle/feature.conf") \
	L(feature_banking, "feature_banking", 1, 1, 0, 1, 0, 1, "conf/battle/feature.conf") \
	L(vip_storage_increase, "vip_storage_increase", 300, 300, 0, 600, 0, 600, "conf/battle/player.conf") \
	L(vip_base_exp_increase, "vip_base_exp_increase", 50, 50, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/player.conf") \
	L(vip_job_exp_increase, "vip_job_exp_increase", 50, 50, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/player.conf") \
	L(vip_exp_penalty_base, "vip_exp_penalty_base", 100, 100, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/player.conf") \
	L(vip_exp_penalty_job, "vip_exp_penalty_job", 100, 100, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/player.conf") \
	L(vip_zeny_penalty, "vip_zeny_penalty", 0, 0, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/player.conf") \
	L(vip_bm_increase, "vip_bm_increase", 2, 2, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/player.conf") \
	L(vip_drop_increase, "vip_drop_increase", 50, 50, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/player.conf") \
	L(vip_gemstone, "vip_gemstone", 2, 2, 0, 2, 0, 2, "conf/battle/player.conf") \
	L(vip_disp_rate, "vip_disp_rate", 1, 1, 0, 1, 0, 1, "conf/battle/player.conf") \
	L(mon_trans_disable_in_gvg, "mon_trans_disable_in_gvg", 0, 0, 0, 1, 0, 1, "conf/battle/misc.conf") \
	L(homunculus_S_growth_level, "hom_S_growth_level", 99, 99, 0, 275, 0, 275, "conf/battle/homunc.conf") \
	L(discount_item_point_shop, "discount_item_point_shop", 0, 0, 0, 3, 0, 3, "conf/battle/misc.conf") \
	L(update_enemy_position, "update_enemy_position", 0, 0, 0, 1, 0, 1, "conf/battle/client.conf") \
	L(devotion_rdamage, "devotion_rdamage", 0, 0, 0, 100, 0, 100, "conf/battle/skill.conf") \
	L(feature_autotrade, "feature_autotrade", 1, 1, 0, 1, 0, 1, "conf/battle/feature.conf") \
	L(feature_autotrade_direction, "feature_autotrade_direction", 4, 4, -1, 7, -1, 7, "conf/battle/feature.conf") \
	L(feature_autotrade_head_direction, "feature_autotrade_head_direction", 0, 0, -1, 2, -1, 2, "conf/battle/feature.conf") \
	L(feature_autotrade_sit, "feature_autotrade_sit", 1, 1, -1, 1, -1, 1, "conf/battle/feature.conf") \
	L(feature_autotrade_open_delay, "feature_autotrade_open_delay", 5000, 5000, 1000, RATHENA_I32_MAX, 1000, RATHENA_I32_MAX, "conf/battle/feature.conf") \
	L(disp_servervip_msg, "disp_servervip_msg", 0, 0, 0, 1, 0, 1, "conf/battle/misc.conf") \
	L(warg_can_falcon, "warg_can_falcon", 0, 0, 0, 1, 0, 1, "conf/battle/battle.conf") \
	L(path_blown_halt, "path_blown_halt", 1, 1, 0, 1, 0, 1, "conf/battle/skill.conf") \
	L(rental_mount_speed_boost, "rental_mount_speed_boost", 25, 25, 0, 100, 0, 100, "conf/battle/player.conf") \
	L(feature_warp_suggestions, "warp_suggestions_enabled", 0, 0, 0, 1, 0, 1, "conf/battle/feature.conf") \
	L(taekwon_mission_mobname, "taekwon_mission_mobname", 0, 0, 0, 2, 0, 2, "conf/battle/skill.conf") \
	L(teleport_on_portal, "teleport_on_portal", 0, 0, 0, 1, 0, 1, "conf/battle/skill.conf") \
	L(cart_revo_knockback, "cart_revo_knockback", 1, 1, 0, 1, 0, 1, "conf/battle/skill.conf") \
	L(guild_notice_changemap, "guild_notice_changemap", 2, 2, 0, 2, 0, 2, "conf/battle/guild.conf") \
	L(transcendent_status_points, "transcendent_status_points", 52, 52, 1, RATHENA_I32_MAX, 1, RATHENA_I32_MAX, "conf/battle/player.conf") \
	L(taekwon_ranker_min_lv, "taekwon_ranker_min_lv", 90, 90, 1, 275, 1, 275, "conf/battle/player.conf") \
	L(revive_onwarp, "revive_onwarp", 1, 1, 0, 1, 0, 1, "conf/battle/player.conf") \
	L(fame_taekwon_mission, "fame_taekwon_mission", 1, 1, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/player.conf") \
	L(fame_refine_lv1, "fame_refine_lv1", 1, 1, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/player.conf") \
	L(fame_refine_lv2, "fame_refine_lv2", 25, 25, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/player.conf") \
	L(fame_refine_lv3, "fame_refine_lv3", 1000, 1000, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/player.conf") \
	L(fame_forge, "fame_forge", 10, 10, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/player.conf") \
	L(fame_pharmacy_3, "fame_pharmacy_3", 1, 1, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/player.conf") \
	L(fame_pharmacy_5, "fame_pharmacy_5", 3, 3, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/player.conf") \
	L(fame_pharmacy_7, "fame_pharmacy_7", 10, 10, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/player.conf") \
	L(fame_pharmacy_10, "fame_pharmacy_10", 50, 50, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/player.conf") \
	L(mail_delay, "mail_delay", 1000, 1000, 1000, RATHENA_I32_MAX, 1000, RATHENA_I32_MAX, "conf/battle/misc.conf") \
	L(at_monsterignore, "autotrade_monsterignore", 0, 0, 0, 1, 0, 1, "conf/battle/misc.conf") \
	L(idletime_option, "idletime_option", 31775, 31775, 1, 65535, 1, 65535, "conf/battle/player.conf") \
	L(spawn_direction, "spawn_direction", 0, 0, 0, 1, 0, 1, "conf/battle/client.conf") \
	L(arrow_shower_knockback, "arrow_shower_knockback", 1, 1, 0, 1, 0, 1, "conf/battle/skill.conf") \
	L(devotion_rdamage_skill_only, "devotion_rdamage_skill_only", 1, 1, 0, 1, 0, 1, "conf/battle/skill.conf") \
	L(max_extended_aspd, "max_extended_aspd", 193, 193, 100, 199, 100, 199, "conf/battle/player.conf") \
	L(monster_chase_refresh, "mob_chase_refresh", 32, 32, 0, 32, 0, 32, "conf/battle/monster.conf") \
	L(mob_icewall_walk_block, "mob_icewall_walk_block", 75, 75, 0, 255, 0, 255, "conf/battle/monster.conf") \
	L(boss_icewall_walk_block, "boss_icewall_walk_block", 0, 0, 0, 255, 0, 255, "conf/battle/monster.conf") \
	L(snap_dodge, "snap_dodge", 0, 0, 0, 1, 0, 1, "conf/battle/battle.conf") \
	L(stormgust_knockback, "stormgust_knockback", 1, 1, 0, 1, 0, 1, "conf/battle/skill.conf") \
	L(default_fixed_castrate, "default_fixed_castrate", 20, 20, 0, 100, 0, 100, "conf/battle/skill.conf") \
	L(default_bind_on_equip, "default_bind_on_equip", 4, 4, 0, 4, 0, 4, "conf/battle/items.conf") \
	L(pet_ignore_infinite_def, "pet_ignore_infinite_def", 0, 0, 0, 1, 0, 1, "conf/battle/pet.conf") \
	L(homunculus_evo_intimacy_need, "homunculus_evo_intimacy_need", 91100, 91100, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/homunc.conf") \
	L(homunculus_evo_intimacy_reset, "homunculus_evo_intimacy_reset", 1000, 1000, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/homunc.conf") \
	L(monster_loot_search_type, "monster_loot_search_type", 1, 1, 0, 1, 0, 1, "conf/battle/monster.conf") \
	L(feature_roulette, "feature_roulette", 1, 1, 0, 1, 0, 1, "conf/battle/feature.conf") \
	L(feature_roulette_bonus_reward, "feature_roulette_bonus_reward", 1, 1, 0, 1, 0, 1, "conf/battle/feature.conf") \
	L(monster_hp_bars_info, "monster_hp_bars_info", 1, 1, 0, 1, 0, 1, "conf/battle/monster.conf") \
	L(save_body_style, "save_body_style", 1, 1, 0, 1, 0, 1, "conf/battle/client.conf") \
	L(monster_eye_range_bonus, "mob_eye_range_bonus", 0, 0, 0, 10, 0, 10, "conf/battle/monster.conf") \
	L(monster_stuck_warning, "mob_stuck_warning", 0, 0, 0, 1, 0, 1, "conf/battle/monster.conf") \
	L(skill_eightpath_algorithm, "skill_eightpath_algorithm", 1, 1, 0, 1, 0, 1, "conf/battle/skill.conf") \
	L(skill_eightpath_same_cell, "skill_eightpath_same_cell", 1, 1, 0, 1, 0, 1, "conf/battle/skill.conf") \
	L(death_penalty_maxlv, "death_penalty_maxlv", 0, 0, 0, 3, 0, 3, "conf/battle/exp.conf") \
	L(exp_cost_redemptio, "exp_cost_redemptio", 1, 1, 0, 100, 0, 100, "conf/battle/exp.conf") \
	L(exp_cost_redemptio_limit, "exp_cost_redemptio_limit", 5, 5, 0, 12, 0, 12, "conf/battle/exp.conf") \
	L(mvp_exp_reward_message, "mvp_exp_reward_message", 0, 0, 0, 1, 0, 1, "conf/battle/client.conf") \
	L(can_damage_skill, "can_damage_skill", 1, 1, 0, 4095, 0, 4095, "conf/battle/skill.conf") \
	L(atcommand_levelup_events, "atcommand_levelup_events", 0, 0, 0, 1, 0, 1, "conf/battle/gm.conf") \
	L(atcommand_disable_npc, "atcommand_disable_npc", 1, 1, 0, 1, 0, 1, "conf/battle/gm.conf") \
	L(block_account_in_same_party, "block_account_in_same_party", 1, 1, 0, 1, 0, 1, "conf/battle/party.conf") \
	L(tarotcard_equal_chance, "tarotcard_equal_chance", 0, 0, 0, 1, 0, 1, "conf/battle/skill.conf") \
	L(change_party_leader_samemap, "change_party_leader_samemap", 1, 1, 0, 1, 0, 1, "conf/battle/party.conf") \
	L(dispel_song, "dispel_song", 0, 0, 0, 1, 0, 1, "conf/battle/skill.conf") \
	L(refresh_song, "refresh_song", 0, 0, 0, 1, 0, 1, "conf/battle/skill.conf") \
	L(refresh_song_icon, "refresh_song_icon", 0, 0, 0, 1, 0, 1, "conf/battle/skill.conf") \
	L(guild_maprespawn_clones, "guild_maprespawn_clones", 0, 0, 0, 1, 0, 1, "conf/battle/guild.conf") \
	L(hide_fav_sell, "hide_fav_sell", 0, 0, 0, 1, 0, 1, "conf/battle/misc.conf") \
	L(mail_daily_count, "mail_daily_count", 100, 100, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/misc.conf") \
	L(mail_zeny_fee, "mail_zeny_fee", 2, 2, 0, 100, 0, 100, "conf/battle/misc.conf") \
	L(mail_attachment_price, "mail_attachment_price", 2500, 2500, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/misc.conf") \
	L(mail_attachment_weight, "mail_attachment_weight", 2000, 2000, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/misc.conf") \
	L(banana_bomb_duration, "banana_bomb_duration", 0, 0, 0, 65535, 0, 65535, "conf/battle/skill.conf") \
	L(guild_leaderchange_delay, "guild_leaderchange_delay", 1440, 1440, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/guild.conf") \
	L(guild_leaderchange_woe, "guild_leaderchange_woe", 0, 0, 0, 1, 0, 1, "conf/battle/guild.conf") \
	L(guild_alliance_onlygm, "guild_alliance_onlygm", 0, 0, 0, 1, 0, 1, "conf/battle/guild.conf") \
	L(feature_achievement, "feature_achievement", 1, 1, 0, 1, 0, 1, "conf/battle/feature.conf") \
	L(allow_bound_sell, "allow_bound_sell", 0, 0, 0, 15, 0, 15, "conf/battle/items.conf") \
	L(autoloot_adjust, "autoloot_adjust", 0, 0, 0, 1, 0, 1, "conf/battle/drops.conf") \
	L(feature_petevolution, "feature_petevolution", 1, 1, 0, 1, 0, 1, "conf/battle/feature.conf") \
	L(feature_petautofeed, "feature_pet_autofeed", 1, 1, 0, 1, 0, 1, "conf/battle/feature.conf") \
	L(feature_pet_autofeed_rate, "feature_pet_autofeed_rate", 89, 89, 0, 100, 0, 100, "conf/battle/feature.conf") \
	L(pet_autofeed_always, "pet_autofeed_always", 1, 1, 0, 1, 0, 1, "conf/battle/pet.conf") \
	L(broadcast_hide_name, "broadcast_hide_name", 2, 2, 0, 24, 0, 24, "conf/battle/items.conf") \
	L(skill_drop_items_full, "skill_drop_items_full", 0, 0, 0, 1, 0, 1, "conf/battle/skill.conf") \
	L(switch_remove_edp, "switch_remove_edp", 2, 2, 0, 3, 0, 3, "conf/battle/skill.conf") \
	L(feature_homunculus_autofeed, "feature_homunculus_autofeed", 1, 1, 0, 1, 0, 1, "conf/battle/feature.conf") \
	L(feature_homunculus_autofeed_rate, "feature_homunculus_autofeed_rate", 30, 30, 0, 100, 0, 100, "conf/battle/feature.conf") \
	L(summoner_race, "summoner_race", 11, 11, 0, 11, 0, 11, "conf/battle/player.conf") \
	L(summoner_size, "summoner_size", 0, 0, 0, 2, 0, 2, "conf/battle/player.conf") \
	L(homunculus_autofeed_always, "homunculus_autofeed_always", 1, 1, 0, 1, 0, 1, "conf/battle/homunc.conf") \
	L(feature_attendance, "feature_attendance", 1, 1, 0, 1, 0, 1, "conf/battle/feature.conf") \
	L(feature_privateairship, "feature_privateairship", 1, 1, 0, 1, 0, 1, "conf/battle/feature.conf") \
	L(rental_transaction, "rental_transaction", 1, 1, 0, 1, 0, 1, "conf/battle/items.conf") \
	L(min_shop_buy, "min_shop_buy", 1, 1, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/items.conf") \
	L(min_shop_sell, "min_shop_sell", 0, 0, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/items.conf") \
	L(feature_equipswitch, "feature_equipswitch", 1, 1, 0, 1, 0, 1, "conf/battle/feature.conf") \
	L(pet_walk_speed, "pet_walk_speed", 1, 1, 1, 3, 1, 3, "conf/battle/pet.conf") \
	L(blacksmith_fame_refine_threshold, "blacksmith_fame_refine_threshold", 10, 10, 1, 20, 1, 10, "conf/battle/player.conf") \
	L(mob_nopc_idleskill_rate, "mob_nopc_idleskill_rate", 100, 100, 0, 100, 0, 100, "conf/battle/monster.conf") \
	L(mob_nopc_move_rate, "mob_nopc_move_rate", 100, 100, 0, 100, 0, 100, "conf/battle/monster.conf") \
	L(boss_nopc_idleskill_rate, "boss_nopc_idleskill_rate", 100, 100, 0, 100, 0, 100, "conf/battle/monster.conf") \
	L(boss_nopc_move_rate, "boss_nopc_move_rate", 100, 100, 0, 100, 0, 100, "conf/battle/monster.conf") \
	L(hom_idle_no_share, "hom_idle_no_share", 0, 0, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/homunc.conf") \
	L(idletime_hom_option, "idletime_hom_option", 31, 31, 1, 4095, 1, 4095, "conf/battle/homunc.conf") \
	L(devotion_standup_fix, "devotion_standup_fix", 1, 1, 0, 1, 0, 1, "conf/battle/skill.conf") \
	L(feature_bgqueue, "feature_bgqueue", 1, 1, 0, 1, 0, 1, "conf/battle/feature.conf") \
	L(bgqueue_nowarp_mapflag, "bgqueue_nowarp_mapflag", 0, 0, 0, 1, 0, 1, "conf/battle/battleground.conf") \
	L(homunculus_exp_gain, "homunculus_exp_gain", 10, 10, 0, 100, 0, 100, "conf/battle/homunc.conf") \
	L(rental_item_novalue, "rental_item_novalue", 1, 1, 0, 1, 0, 1, "conf/battle/items.conf") \
	L(ping_timer_inverval, "ping_timer_interval", 30, 30, 0, 99999999, 0, 99999999, "conf/battle/client.conf") \
	L(ping_time, "ping_time", 20, 20, 0, 99999999, 0, 99999999, "conf/battle/client.conf") \
	L(show_skill_scale, "show_skill_scale", 1, 1, 0, 1, 0, 1, "conf/battle/client.conf") \
	L(achievement_mob_share, "achievement_mob_share", 0, 0, 0, 1, 0, 1, "conf/battle/monster.conf") \
	L(slave_stick_with_master, "slave_stick_with_master", 0, 0, 0, 1, 0, 1, "conf/battle/monster.conf") \
	L(slave_active_with_master, "slave_active_with_master", 0, 0, 0, 1, 0, 1, "conf/battle/monster.conf") \
	L(at_logout_event, "at_logout_event", 1, 1, 0, 1, 0, 1, "conf/battle/misc.conf") \
	L(homunculus_starving_rate, "homunculus_starving_rate", 10, 10, 0, 100, 0, 100, "conf/battle/homunc.conf") \
	L(homunculus_starving_delay, "homunculus_starving_delay", 20000, 20000, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/homunc.conf") \
	L(drop_connection_on_quit, "drop_connection_on_quit", 0, 0, 0, 1, 0, 1, "conf/battle/client.conf") \
	L(mob_spawn_variance, "mob_spawn_variance", 1, 1, 0, 3, 0, 3, "conf/battle/monster.conf") \
	L(mercenary_autoloot, "mercenary_autoloot", 0, 0, 0, 1, 0, 1, "conf/battle/drops.conf") \
	L(mer_idle_no_share, "mer_idle_no_share", 0, 0, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/drops.conf") \
	L(idletime_mer_option, "idletime_mer_option", 31, 31, 1, 4095, 1, 4095, "conf/battle/drops.conf") \
	L(feature_refineui, "feature_refineui", 1, 1, 0, 1, 0, 1, "conf/battle/feature.conf") \
	L(rndopt_drop_pillar, "rndopt_drop_pillar", 1, 1, 0, 1, 0, 1, "conf/battle/drops.conf") \
	L(pet_legacy_formula, "pet_legacy_formula", 0, 0, 0, 1, 0, 1, "conf/battle/pet.conf") \
	L(pet_distance_check, "pet_distance_check", 5, 5, 0, 50, 0, 50, "conf/battle/pet.conf") \
	L(pet_hide_check, "pet_hide_check", 1, 1, 0, 1, 0, 1, "conf/battle/pet.conf") \
	L(instance_block_leave, "instance_block_leave", 1, 1, 0, 1, 0, 1, "conf/battle/instance.conf") \
	L(instance_block_leaderchange, "instance_block_leaderchange", 1, 1, 0, 1, 0, 1, "conf/battle/instance.conf") \
	L(instance_block_invite, "instance_block_invite", 1, 1, 0, 1, 0, 1, "conf/battle/instance.conf") \
	L(instance_block_expulsion, "instance_block_expulsion", 1, 1, 0, 1, 0, 1, "conf/battle/instance.conf") \
	L(use_traitpoint_table, "use_traitpoint_table", 1, 1, 0, 1, 0, 1, "conf/battle/exp.conf") \
	L(trait_points_job_change, "trait_points_job_change", 7, 7, 1, 1000, 1, 1000, "conf/battle/player.conf") \
	L(max_trait_parameter, "max_trait_parameter", 100, 100, 10, 32767, 10, 32767, "conf/battle/player.conf") \
	L(max_res_mres_ignored, "max_res_mres_ignored", 50, 50, 0, 100, 0, 100, "conf/battle/player.conf") \
	L(max_ap, "max_ap", 200, 200, 100, 1000000000, 100, 1000000000, "conf/battle/player.conf") \
	L(ap_rate, "ap_rate", 100, 100, 1, RATHENA_I32_MAX, 1, RATHENA_I32_MAX, "conf/battle/player.conf") \
	L(restart_ap_rate, "restart_ap_rate", 0, 0, 0, 100, 0, 100, "conf/battle/player.conf") \
	L(loose_ap_on_death, "loose_ap_on_death", 1, 1, 0, 1, 0, 1, "conf/battle/player.conf") \
	L(loose_ap_on_map, "loose_ap_on_map", 1, 1, 0, 1, 0, 1, "conf/battle/player.conf") \
	L(keep_ap_on_logout, "keep_ap_on_logout", 1, 1, 0, 1, 0, 1, "conf/battle/player.conf") \
	L(attack_machine_level_difference, "attack_machine_level_difference", 15, 15, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/skill.conf") \
	L(feature_barter, "feature_barter", 1, 1, 0, 1, 0, 1, "conf/battle/feature.conf") \
	L(feature_barter_extended, "feature_barter_extended", 1, 1, 0, 1, 0, 1, "conf/battle/feature.conf") \
	L(feature_itemlink, "feature_itemlink", 1, 1, 0, 1, 0, 1, "conf/battle/feature.conf") \
	L(feature_mesitemlink, "feature_mesitemlink", 1, 1, 0, 1, 0, 1, "conf/battle/feature.conf") \
	L(feature_mesitemlink_brackets, "feature_mesitemlink_brackets", 0, 0, 0, 1, 0, 1, "conf/battle/feature.conf") \
	L(feature_mesitemlink_dbname, "feature_mesitemlink_dbname", 0, 0, 0, 1, 0, 1, "conf/battle/feature.conf") \
	L(feature_mesitemicon, "feature_mesitemicon", 1, 1, 0, 1, 0, 1, "conf/battle/feature.conf") \
	L(feature_mesitemicon_dbname, "feature_mesitemicon_dbname", 0, 0, 0, 1, 0, 1, "conf/battle/feature.conf") \
	L(break_mob_equip, "break_mob_equip", 0, 0, 0, 1, 0, 1, "conf/battle/battle.conf") \
	L(macro_detection_retry, "macro_detection_retry", 3, 3, 1, RATHENA_I32_MAX, 1, RATHENA_I32_MAX, "conf/battle/client.conf") \
	L(macro_detection_timeout, "macro_detection_timeout", 60000, 60000, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/client.conf") \
	L(macro_detection_punishment, "macro_detection_punishment", 0, 0, 0, 1, 0, 1, "conf/battle/client.conf") \
	L(macro_detection_punishment_time, "macro_detection_punishment_time", 0, 0, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/client.conf") \
	L(macrochecker_delay, "macrochecker_delay", 600000, 600000, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/client.conf") \
	L(feature_dynamicnpc_timeout, "feature_dynamicnpc_timeout", 1000, 1000, 60000, RATHENA_I32_MAX, 60000, RATHENA_I32_MAX, "conf/battle/feature.conf") \
	L(feature_dynamicnpc_rangex, "feature_dynamicnpc_rangex", 2, 2, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/feature.conf") \
	L(feature_dynamicnpc_rangey, "feature_dynamicnpc_rangey", 2, 2, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/feature.conf") \
	L(feature_dynamicnpc_direction, "feature_dynamicnpc_direction", 0, 0, 0, 1, 0, 1, "conf/battle/feature.conf") \
	L(mob_respawn_time, "mob_respawn_time", 1000, 1000, 1000, RATHENA_I32_MAX, 1000, RATHENA_I32_MAX, "conf/battle/monster.conf") \
	L(mob_unlock_time, "mob_unlock_time", 2000, 2000, 0, RATHENA_I32_MAX, 0, RATHENA_I32_MAX, "conf/battle/monster.conf") \
	L(map_edge_size, "map_edge_size", 15, 15, 1, 40, 1, 40, "conf/battle/misc.conf") \
	L(randomize_center_cell, "randomize_center_cell", 1, 1, 0, 1, 0, 1, "conf/battle/monster.conf") \
	L(feature_stylist, "feature_stylist", 1, 1, 0, 1, 0, 1, "conf/battle/feature.conf") \
	L(feature_banking_state_enforce, "feature_banking_state_enforce", 0, 0, 0, 1, 0, 1, "conf/battle/feature.conf") \
	L(feature_instance_allow_reconnect, "instance_allow_reconnect", 1, 0, 0, 1, 0, 1, "") \
	L(synchronize_damage, "synchronize_damage", 0, 0, 0, 1, 0, 1, "conf/battle/battle.conf") \
	L(item_stacking, "item_stacking", 1, 1, 0, 1, 0, 1, "conf/battle/misc.conf") \
	L(hom_delay_reset_vaporize, "hom_delay_reset_vaporize", 0, 1, 0, 1, 0, 1, "") \
	L(hom_delay_reset_warp, "hom_delay_reset_warp", 0, 1, 0, 1, 0, 1, "") \
	L(alchemist_summon_setting, "alchemist_summon_setting", 12, 15, 0, 15, 0, 15, "") \
	L(loot_range, "loot_range", 12, 12, 1, 32, 1, 32, "conf/battle/monster.conf") \
	L(assist_range, "assist_range", 11, 11, 1, 32, 1, 32, "conf/battle/monster.conf") \
	L(major_overweight_rate, "major_overweight_rate", 90, 90, 0, 100, 0, 100, "conf/battle/player.conf") \
	L(trade_count_stackable, "trade_count_stackable", 1, 1, 0, 1, 0, 1, "conf/battle/items.conf") \
	L(enable_bonus_map_drops, "enable_bonus_map_drops", 1, 1, 0, 1, 0, 1, "conf/battle/drops.conf") \
	L(hide_cloaked_units, "hide_cloaked_units", 0, 0, 0, 4095, 0, 4095, "conf/battle/client.conf") \
	L(oridecon_research_fix, "oridecon_research_fix", 0, 0, 0, 1, 0, 1, "conf/battle/skill.conf") \
	/* end */
// NOLINTEND
