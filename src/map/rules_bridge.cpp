// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "rules_bridge.hpp"

#include <common/random.hpp>

#include "battle.hpp"

namespace {

rules::BattleConfig snapshot = rules::BattleConfig::shipped(
#ifdef RENEWAL
	rules::Mode::Renewal
#else
	rules::Mode::PreRenewal
#endif
);

class LegacyRng final : public rules::Rng {
public:
	std::uint32_t next_u32() override { return static_cast<std::uint32_t>( rnd() ); }
};

} // namespace

const rules::BattleConfig& rules_config(){
	return snapshot;
}

rules::Rng& rules_rng(){
	static LegacyRng rng;
	return rng;
}

void rules_config_snapshot(){
	rules::BattleConfig c = rules::BattleConfig::shipped( snapshot.mode );
	c.max_parameter = static_cast<std::int32_t>( battle_config.max_parameter );
	c.max_third_parameter = static_cast<std::int32_t>( battle_config.max_third_parameter );
	c.max_aspd = static_cast<std::int32_t>( battle_config.max_aspd );
	c.max_third_aspd = static_cast<std::int32_t>( battle_config.max_third_aspd );
	c.prevent_logout = static_cast<std::int32_t>( battle_config.prevent_logout );
	c.vip_storage_increase = static_cast<std::int32_t>( battle_config.vip_storage_increase );
	c.natural_healhp_interval = static_cast<std::int32_t>( battle_config.natural_healhp_interval );
	c.natural_healsp_interval = static_cast<std::int32_t>( battle_config.natural_healsp_interval );
	c.natural_heal_skill_interval = static_cast<std::int32_t>( battle_config.natural_heal_skill_interval );
	c.bone_drop = static_cast<std::int32_t>( battle_config.bone_drop );
	c.open_box_weight_rate = static_cast<std::int32_t>( battle_config.open_box_weight_rate );
	c.min_hitrate = static_cast<std::int32_t>( battle_config.min_hitrate );
	c.max_hitrate = static_cast<std::int32_t>( battle_config.max_hitrate );
	c.agi_penalty_type = static_cast<std::int32_t>( battle_config.agi_penalty_type );
	c.agi_penalty_count = static_cast<std::int32_t>( battle_config.agi_penalty_count );
	c.agi_penalty_num = static_cast<std::int32_t>( battle_config.agi_penalty_num );
	c.vit_penalty_type = static_cast<std::int32_t>( battle_config.vit_penalty_type );
	c.vit_penalty_count = static_cast<std::int32_t>( battle_config.vit_penalty_count );
	c.vit_penalty_num = static_cast<std::int32_t>( battle_config.vit_penalty_num );
	c.castrate_dex_scale = static_cast<std::int32_t>( battle_config.castrate_dex_scale );
	c.vcast_stat_scale = static_cast<std::int32_t>( battle_config.vcast_stat_scale );
	c.default_fixed_castrate = static_cast<std::int32_t>( battle_config.default_fixed_castrate );
	c.delay_rate = static_cast<std::int32_t>( battle_config.delay_rate );
	c.min_skill_delay_limit = static_cast<std::int32_t>( battle_config.min_skill_delay_limit );
	c.delay_dependon_dex = battle_config.delay_dependon_dex != 0;
	c.delay_dependon_agi = battle_config.delay_dependon_agi != 0;
	c.no_skill_delay = static_cast<std::int32_t>( battle_config.no_skill_delay );
	c.vending_max_value = static_cast<std::int32_t>( battle_config.vending_max_value );
	c.vending_tax = static_cast<std::int32_t>( battle_config.vending_tax );
	c.vending_tax_min = static_cast<std::int32_t>( battle_config.vending_tax_min );
	c.min_shop_buy = static_cast<std::int32_t>( battle_config.min_shop_buy );
	c.min_shop_sell = static_cast<std::int32_t>( battle_config.min_shop_sell );
	c.item_use_interval = static_cast<std::int32_t>( battle_config.item_use_interval );
	c.cashfood_use_interval = static_cast<std::int32_t>( battle_config.cashfood_use_interval );
	c.item_rate_common = static_cast<std::int32_t>( battle_config.item_rate_common );
	c.item_drop_common_min = static_cast<std::int32_t>( battle_config.item_drop_common_min );
	c.item_drop_common_max = static_cast<std::int32_t>( battle_config.item_drop_common_max );
	c.logarithmic_drops = battle_config.logarithmic_drops != 0;
	c.drop_rate_cap = static_cast<std::int32_t>( battle_config.drop_rate_cap );
	c.drop_rate_cap_vip = static_cast<std::int32_t>( battle_config.drop_rate_cap_vip );
	c.drop_rate0item = battle_config.drop_rate0item != 0;
	c.vip_drop_increase = static_cast<std::int32_t>( battle_config.vip_drop_increase );
	c.item_first_get_time = static_cast<std::int32_t>( battle_config.item_first_get_time );
	c.item_second_get_time = static_cast<std::int32_t>( battle_config.item_second_get_time );
	c.item_third_get_time = static_cast<std::int32_t>( battle_config.item_third_get_time );
	c.mvp_item_first_get_time = static_cast<std::int32_t>( battle_config.mvp_item_first_get_time );
	c.mvp_item_second_get_time = static_cast<std::int32_t>( battle_config.mvp_item_second_get_time );
	c.mvp_item_third_get_time = static_cast<std::int32_t>( battle_config.mvp_item_third_get_time );
	c.multi_level_up = battle_config.multi_level_up != 0;
	c.death_penalty_type = static_cast<std::int32_t>( battle_config.death_penalty_type );
	c.death_penalty_base = static_cast<std::int32_t>( battle_config.death_penalty_base );
	c.death_penalty_job = static_cast<std::int32_t>( battle_config.death_penalty_job );
	c.mobs_level_up = battle_config.mobs_level_up != 0;
	c.mobs_level_up_exp_rate = static_cast<std::int32_t>( battle_config.mobs_level_up_exp_rate );
	c.show_mob_info = static_cast<std::int32_t>( battle_config.show_mob_info );
	c.duel_allow_teleport = battle_config.duel_allow_teleport != 0;
	c.guild_exp_limit = static_cast<std::int32_t>( battle_config.guild_exp_limit );
	c.party_even_share_bonus = static_cast<std::int32_t>( battle_config.party_even_share_bonus );
	// progression/economy (BR-MAP-001..003, 021..037, 045..050)
	c.natural_heal_weight_rate = static_cast<std::int32_t>( battle_config.natural_heal_weight_rate );
	c.major_overweight_rate = static_cast<std::int32_t>( battle_config.major_overweight_rate );
	snapshot = c;
}

void rules_config_refresh( const std::int32_t* knob ){
	// combat (BR-MAP-012, 013): crowd penalties; battle_adjust_conf leaves these knobs as read
	if( knob == &battle_config.agi_penalty_type ) snapshot.agi_penalty_type = static_cast<std::int32_t>( *knob );
	if( knob == &battle_config.agi_penalty_count ) snapshot.agi_penalty_count = static_cast<std::int32_t>( *knob );
	if( knob == &battle_config.agi_penalty_num ) snapshot.agi_penalty_num = static_cast<std::int32_t>( *knob );
	if( knob == &battle_config.vit_penalty_type ) snapshot.vit_penalty_type = static_cast<std::int32_t>( *knob );
	if( knob == &battle_config.vit_penalty_count ) snapshot.vit_penalty_count = static_cast<std::int32_t>( *knob );
	if( knob == &battle_config.vit_penalty_num ) snapshot.vit_penalty_num = static_cast<std::int32_t>( *knob );
}
