// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

// Bridge between the legacy map-server and the pure rule library
// (modernized/rathena-reimagined/map-rules, REIMAGINED_ARCHITECTURE.md §3.2 "Delegate").
// Legacy call sites hand their inputs to a rathena::map_rules function and get the result back;
// these helpers supply the two things every rule needs from the server:
//  - rules_config(): the typed BattleConfig, a snapshot of battle_config as read from the conf
//    files (taken before battle_adjust_conf() converts some values into internal units);
//  - rules_rng(): the legacy random stream. next_u32() is rnd(), so a rule's `rng.below(n)` is
//    exactly the legacy `rnd() % n`. Call sites that draw with rnd_value() (a library-defined
//    distribution) keep drawing themselves and pass the value in.
#ifndef MAP_RULES_BRIDGE_HPP
#define MAP_RULES_BRIDGE_HPP

#include <cstdint>

#include <rathena/map_rules/battle_config.hpp>
#include <rathena/map_rules/rng.hpp>

namespace rules = rathena::map_rules;

/// The typed battle configuration the rules read.
const rules::BattleConfig& rules_config();

/// The legacy random stream as a rules::Rng.
rules::Rng& rules_rng();

/// Takes the snapshot from battle_config (battle_config_read, before battle_adjust_conf).
void rules_config_snapshot();

/// Re-copies the knob at `knob` (a battle_config field) after a runtime change by battle_set_value
/// (setbattleflag, @setbattleflag), so the rules keep seeing what legacy sees. Knobs that are not
/// listed keep their snapshot value.
void rules_config_refresh( const std::int32_t* knob );

#endif /* MAP_RULES_BRIDGE_HPP */
