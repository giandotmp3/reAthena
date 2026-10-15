// Typed, immutable BattleConfig subset consumed by map rules (architecture §2, §9 M10).
//
// ONE source of defaults (spec §5.6): every default below is the value *shipped* in
// conf/battle/*.conf, not the legacy code default. Field names match the conf keys so the
// generated rathena::core::BattleConfig (581 knobs) can replace this struct field-for-field.
//
// Every knob here also exists in core/include/rathena/core/knobs.def.hpp with the same
// shipped default; when this unit moves into src/rules/ this struct becomes core's BattleConfig.
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

	// items.conf
	std::int32_t vending_max_value = 1000000000; // BR-MAP-042 [§5.6 drift: 10,000,000 -> 1,000,000,000]
	std::int32_t vending_tax = 500;              // BR-MAP-044 (1/10000 units) [§5.6 drift: 0 -> 500]
	std::int32_t vending_tax_min = 100000000;    // BR-MAP-044
	std::int32_t min_shop_buy = 1;               // BR-MAP-047
	std::int32_t min_shop_sell = 0;              // BR-MAP-047

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

	// guild.conf / party.conf
	std::int32_t guild_exp_limit = 50;       // BR-MAP-027 (max tax %)
	std::int32_t party_even_share_bonus = 0; // BR-MAP-028
};

} // namespace rathena::map_rules
