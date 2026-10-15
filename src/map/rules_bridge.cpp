// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "rules_bridge.hpp"

#include <common/random.hpp>

#include "battle.hpp"

// Every BattleConfig knob the rules read; each has a battle_config field of the same name.
#define RULES_BRIDGE_KNOBS( X ) \
	X( max_parameter ) \
	X( max_third_parameter ) \
	X( max_aspd ) \
	X( max_third_aspd ) \
	X( prevent_logout ) \
	X( vip_storage_increase ) \
	X( natural_healhp_interval ) \
	X( natural_healsp_interval ) \
	X( natural_heal_skill_interval ) \
	X( bone_drop ) \
	X( open_box_weight_rate ) \
	X( natural_heal_weight_rate ) \
	X( major_overweight_rate ) \
	X( min_hitrate ) \
	X( max_hitrate ) \
	X( agi_penalty_type ) \
	X( agi_penalty_count ) \
	X( agi_penalty_num ) \
	X( vit_penalty_type ) \
	X( vit_penalty_count ) \
	X( vit_penalty_num ) \
	X( castrate_dex_scale ) \
	X( vcast_stat_scale ) \
	X( default_fixed_castrate ) \
	X( delay_rate ) \
	X( min_skill_delay_limit ) \
	X( delay_dependon_dex ) \
	X( delay_dependon_agi ) \
	X( no_skill_delay ) \
	X( vending_max_value ) \
	X( vending_tax ) \
	X( vending_tax_min ) \
	X( min_shop_buy ) \
	X( min_shop_sell ) \
	X( item_use_interval ) \
	X( cashfood_use_interval ) \
	X( item_rate_common ) \
	X( item_drop_common_min ) \
	X( item_drop_common_max ) \
	X( logarithmic_drops ) \
	X( drop_rate_cap ) \
	X( drop_rate_cap_vip ) \
	X( drop_rate0item ) \
	X( vip_drop_increase ) \
	X( item_first_get_time ) \
	X( item_second_get_time ) \
	X( item_third_get_time ) \
	X( mvp_item_first_get_time ) \
	X( mvp_item_second_get_time ) \
	X( mvp_item_third_get_time ) \
	X( multi_level_up ) \
	X( death_penalty_type ) \
	X( death_penalty_base ) \
	X( death_penalty_job ) \
	X( mobs_level_up ) \
	X( mobs_level_up_exp_rate ) \
	X( show_mob_info ) \
	X( duel_allow_teleport ) \
	X( guild_exp_limit ) \
	X( party_even_share_bonus )

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

void assign( std::int32_t& to, int32 from ){ to = static_cast<std::int32_t>( from ); }
void assign( bool& to, int32 from ){ to = from != 0; }

bool converted_by_adjust_conf( const int32* knob ){
	return knob == &battle_config.max_aspd || knob == &battle_config.max_third_aspd || knob == &battle_config.min_hitrate;
}

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
#define RULES_BRIDGE_COPY( knob ) assign( c.knob, battle_config.knob );
	RULES_BRIDGE_KNOBS( RULES_BRIDGE_COPY )
#undef RULES_BRIDGE_COPY
	snapshot = c;
}

void rules_config_refresh( const int32* knob ){
	if( converted_by_adjust_conf( knob ) ){
		return;
	}
#define RULES_BRIDGE_REFRESH( name ) if( knob == &battle_config.name ) assign( snapshot.name, battle_config.name );
	RULES_BRIDGE_KNOBS( RULES_BRIDGE_REFRESH )
#undef RULES_BRIDGE_REFRESH
}
