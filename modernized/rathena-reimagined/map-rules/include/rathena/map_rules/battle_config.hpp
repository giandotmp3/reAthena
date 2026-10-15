// Typed, immutable BattleConfig subset consumed by map rules (architecture §2, §9 M10).
//
// ONE source of defaults (spec §5.6): every default below is the value *shipped* in
// conf/battle/*.conf, not the legacy code default. Field names match the conf keys so the
// generated rathena::core::BattleConfig (581 knobs) can replace this struct field-for-field.
//
// Every knob here also exists in core's BattleConfig with the same shipped default (hand-typed in
// core/include/rathena/core/knobs.def.hpp, or generated into battle_knobs.gen.hpp); when this unit
// moves into src/rules/ this struct becomes core's BattleConfig.
//
// A few knobs ship a different value per mode because their conf line is commented out and the
// legacy code default depends on RENEWAL. The field initialisers are the renewal values;
// `BattleConfig::shipped(Mode::PreRenewal)` gives the pre-renewal ones.
#pragma once

#include <cstdint>
#include <limits>

namespace rathena::map_rules {

/// Server formula pipeline. Pre-renewal stays supported (spec §1.1 "Retained").
enum class Mode : std::uint8_t { Renewal, PreRenewal };

struct BattleConfig {
	Mode mode = Mode::Renewal;

	// player.conf
	std::int32_t max_parameter = 99;        // BR-MAP-022
	std::int32_t max_third_parameter = 130; // BR-MAP-022 [§5.6 drift: 135 -> 130]
	std::int32_t max_aspd = 190;            // BR-MAP-019
	std::int32_t max_third_aspd = 193;      // BR-MAP-019
	std::int32_t prevent_logout = 10000;    // BR-MAP-056 (ms; 0 disables the lock)
	std::int32_t vip_storage_increase = 300; // BR-MAP-050
	std::int32_t natural_healhp_interval = 6000; // BR-MAP-055
	std::int32_t natural_healsp_interval = 8000; // BR-MAP-055
	std::int32_t natural_heal_skill_interval = 10000; // BR-MAP-055
	std::int32_t bone_drop = 0;             // BR-MAP-033 (0 off, 1 PvP maps only, 2 everywhere)
	std::int32_t open_box_weight_rate = 90; // BR-MAP-049 (%; pre-renewal 70, conf line commented out)
	// progression/economy (Phase 4 delegation)
	std::int32_t natural_heal_weight_rate = 70; // BR-MAP-002 (%; pre-renewal 50, conf line commented out)
	std::int32_t major_overweight_rate = 90;    // BR-MAP-002 (%)

	// battle.conf
	std::int32_t min_hitrate = 5;     // BR-MAP-011
	std::int32_t max_hitrate = 100;   // BR-MAP-011
	std::int32_t agi_penalty_type = 1; // BR-MAP-012 (0 off, 1 percent, 2 absolute)
	std::int32_t agi_penalty_count = 3;
	std::int32_t agi_penalty_num = 10;
	std::int32_t vit_penalty_type = 1; // BR-MAP-013
	std::int32_t vit_penalty_count = 3;
	std::int32_t vit_penalty_num = 5;

	// skill.conf
	std::int32_t castrate_dex_scale = 150;   // BR-MAP-051
	std::int32_t vcast_stat_scale = 530;     // BR-MAP-052
	std::int32_t default_fixed_castrate = 20; // BR-MAP-052
	std::int32_t delay_rate = 100;            // BR-MAP-053
	std::int32_t min_skill_delay_limit = 100; // BR-MAP-053
	bool delay_dependon_dex = false;          // BR-MAP-053
	bool delay_dependon_agi = false;          // BR-MAP-053
	std::int32_t no_skill_delay = 0x2;        // BR-MAP-053 (BL_* mask; shipped 2 = BL_MOB)

	// items.conf
	std::int32_t vending_max_value = 1000000000; // BR-MAP-042 [§5.6 drift: 10,000,000 -> 1,000,000,000]
	std::int32_t vending_tax = 500;              // BR-MAP-044 (1/10000 units) [§5.6 drift: 0 -> 500]
	std::int32_t vending_tax_min = 100000000;    // BR-MAP-044
	std::int32_t min_shop_buy = 1;               // BR-MAP-047
	std::int32_t min_shop_sell = 0;              // BR-MAP-047
	std::int32_t item_use_interval = 100;        // BR-MAP-049 (ms)
	std::int32_t cashfood_use_interval = 60000;  // BR-MAP-049 (ms)

	// drops.conf
	std::int32_t item_rate_common = 100;       // BR-MAP-035
	std::int32_t item_drop_common_min = 1;
	std::int32_t item_drop_common_max = 10000;
	bool logarithmic_drops = false;
	std::int32_t drop_rate_cap = 9000;     // BR-MAP-036
	std::int32_t drop_rate_cap_vip = 9000; // BR-MAP-036
	bool drop_rate0item = false;           // BR-MAP-036
	std::int32_t vip_drop_increase = 50;   // BR-MAP-036
	std::int32_t item_first_get_time = 3000;      // BR-MAP-037
	std::int32_t item_second_get_time = 2000;     // BR-MAP-037
	std::int32_t item_third_get_time = 2000;      // BR-MAP-037
	std::int32_t mvp_item_first_get_time = 10000; // BR-MAP-037
	std::int32_t mvp_item_second_get_time = 10000; // BR-MAP-037
	std::int32_t mvp_item_third_get_time = 2000;  // BR-MAP-037

	// exp.conf
	bool multi_level_up = false;          // BR-MAP-024
	std::int32_t death_penalty_type = 1;  // BR-MAP-030 [§5.6 drift: 0 -> 1]
	std::int32_t death_penalty_base = 100; // 1/10000 units [§5.6 drift: 0 -> 100]
	std::int32_t death_penalty_job = 100;  // [§5.6 drift: 0 -> 100]

	// monster.conf
	bool mobs_level_up = false;             // BR-MAP-032
	std::int32_t mobs_level_up_exp_rate = 1; // BR-MAP-032 (exp bonus % per level gained)
	std::int32_t show_mob_info = 0;         // BR-MAP-032 (bit 4: the name shows the level)

	// misc.conf
	bool duel_allow_teleport = false;       // BR-MAP-049

	// guild.conf / party.conf
	std::int32_t guild_exp_limit = 50;       // BR-MAP-027 (max tax %)
	std::int32_t party_even_share_bonus = 0; // BR-MAP-028

	/// Shipped defaults for `mode`: the field initialisers plus the mode-dependent knobs.
	static BattleConfig shipped(Mode mode) {
		BattleConfig c;
		c.mode = mode;
		if (mode == Mode::PreRenewal) {
			c.open_box_weight_rate = 70; // battle.cpp battle_data[] #else RENEWAL
			c.natural_heal_weight_rate = 50; // battle.cpp battle_data[] #else RENEWAL
		}
		return c;
	}
};

} // namespace rathena::map_rules
