// rules/progression: CAP-15 progression, BR-MAP-021..034.
// Legacy homes: src/map/pc.cpp (exp/level/stat), guild.cpp (tax), party.cpp (share).
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "rathena/map_rules/battle_config.hpp"
#include "rathena/map_rules/domain.hpp"
#include "rathena/map_rules/rng.hpp"

namespace rathena::map_rules::progression {

/// BR-MAP-021: cost to raise a stat from `current` to current+1.
/// RE: x<100 -> 2+(x-1)/10; x>=100 -> 16+4*((x-100)/5). Pre-RE: 1+(x+9)/10.
std::int32_t stat_cost(std::int32_t current, Mode mode);

enum class StatRaiseError : std::uint8_t { None, NonPositiveAmount, AboveJobMax, NotEnoughPoints };
struct StatRaiseResult {
	StatRaiseError error = StatRaiseError::None;
	std::int32_t new_value = 0;
	std::int32_t points_spent = 0;
	bool ok() const noexcept { return error == StatRaiseError::None; }
};
/// BR-MAP-022: positive amount, result <= job max (max_parameter 99 / max_third_parameter 130),
/// and enough status points for the summed cost.
StatRaiseResult raise_stat(std::int32_t current, std::int32_t amount, std::int32_t status_points, ClassTier tier,
                           const BattleConfig& cfg);

/// Static data: db/{re,pre-re}/statpoint.yml, cumulative points at each base level (index = level).
struct StatPointTable {
	std::vector<std::int32_t> cumulative; // cumulative[level]
};
/// BR-MAP-023: status points granted when going from `from_level` to `to_level`.
std::int32_t status_points_gained(const StatPointTable& table, std::int32_t from_level, std::int32_t to_level);

/// Static data: job exp table; next[level] = exp needed to leave `level`. Size = max level + 1.
struct ExpTable {
	std::vector<std::int64_t> next;
	std::int32_t max_level() const noexcept { return static_cast<std::int32_t>(next.size()) - 1; }
};

struct LevelUpResult {
	std::int32_t level = 1;
	std::int64_t exp = 0;
	std::int32_t levels_gained = 0;
	bool full_heal = false;          // BR-MAP-024: full heal on base level-up
	bool recheck_equipment = false;  // BR-MAP-024: over-level equipment is removed by the caller
	std::int32_t skill_points_gained = 0; // BR-MAP-025 (job only)
};
/// BR-MAP-024: base level-up. Without multi_level_up at most one level is gained and the
/// overflow is capped at next(new level) - 1.
LevelUpResult apply_base_exp(std::int32_t level, std::int64_t exp, std::int64_t gained, const ExpTable& table,
                             const BattleConfig& cfg);
/// BR-MAP-025: job level-up; +1 skill point per job level gained.
LevelUpResult apply_job_exp(std::int32_t level, std::int64_t exp, std::int64_t gained, const ExpTable& table,
                            const BattleConfig& cfg);

struct ExpGainInput {
	bool dead = false;
	bool on_map = true;
	std::int64_t base_exp = 0;
	std::int64_t job_exp = 0;
	bool in_guild = false;
	std::int32_t guild_tax_pct = 0;
	std::int32_t bonus_pct = 0; // summed exp bonuses (additive percent)
	bool at_max_base_level = false;
	bool at_max_job_level = false;
};
struct ExpGain {
	std::int64_t base_exp = 0;
	std::int64_t job_exp = 0;
	std::int64_t guild_exp = 0;
};
/// BR-MAP-026: no exp when dead or off-map; guild tax first; bonuses; cap; max-level clamp.
ExpGain gain_exp(const ExpGainInput& in, const BattleConfig& cfg);

struct GuildTaxSplit {
	std::int64_t player = 0;
	std::int64_t guild = 0;
};
/// BR-MAP-027: tax percent clamped to [0, guild_exp_limit]. 1000 at 10% -> 900 / 100.
GuildTaxSplit guild_tax(std::int64_t exp, std::int32_t tax_pct, const BattleConfig& cfg);

/// BR-MAP-028: even share among eligible members, plus party_even_share_bonus x (n-1)%.
std::int64_t even_share(std::int64_t exp, std::int32_t eligible_members, const BattleConfig& cfg);

/// Static data: level_penalty.yml bucket for one race/type; rate_pct indexed by
/// (mob_level - player_level) + max_diff, clamped to the table edges.
struct LevelPenaltyTable {
	std::int32_t max_diff = 0;
	std::vector<std::int32_t> rate_pct; // size = 2*max_diff + 1
};
/// BR-MAP-029: RE level-difference exp/drop rate in percent (pre-RE: always 100).
std::int32_t level_penalty_pct(const LevelPenaltyTable& table, std::int32_t mob_level, std::int32_t player_level,
                               const BattleConfig& cfg);

struct DeathPenaltyInput {
	std::int64_t base_exp = 0;
	std::int64_t base_next = 0;
	std::int64_t job_exp = 0;
	std::int64_t job_next = 0;
	bool novice = false;
	bool in_gvg = false;
	bool has_insurance = false;
};
struct DeathPenalty {
	std::int64_t base_loss = 0;
	std::int64_t job_loss = 0;
};
/// BR-MAP-030: type 1 = rate (1/10000) of next-level exp; type 2 = of current exp; never more
/// than the current exp. Novices, GvG and insurance are exempt.
DeathPenalty death_penalty(const DeathPenaltyInput& in, const BattleConfig& cfg);

/// BR-MAP-031: Super Novice guardian angel at >= 99.0% of next-level exp, once per level.
bool super_novice_angel(std::int64_t exp, std::int64_t next, bool already_triggered_this_level);

// --- Death of a player (pc.cpp pc_dead) --------------------------------------------------------

/// A monster that just killed a player (pc_dead, killer of type BL_MOB).
struct MobKillLevelUpInput {
	std::int32_t mob_level = 1;
	std::int32_t mob_hp = 1;                 // md->status.hp when the player dies
	std::int32_t victim_max_base_level = 99; // pc_maxbaselv(victim): the cap is the *victim's* max level
	bool guardian = false;                   // md->guardian_data (WoE guardian)
	bool summoned_ai = false;                // md->special_state.ai != AI_NONE (player summon)
};
struct MobLevelUp {
	bool levelled = false;
	std::int32_t level = 1;    // the mob's level afterwards
	bool refresh_name = false; // show_mob_info & 4: the shown name carries the level
};
/// BR-MAP-032: with mobs_level_up on, a living, non-guardian, non-summoned monster that kills a
/// player gains one level while its level is below the victim's max base level.
MobLevelUp mob_level_after_kill(const MobKillLevelUpInput& in, const BattleConfig& cfg);

/// BR-MAP-032: HP healed right after the level-up: status_percent_heal(mob, 10, 0), which is 10%
/// of the mob's *current* HP (apply_rate on status->hp, not max_hp), at least 1, capped at max_hp.
std::int32_t mob_level_up_heal(std::int32_t hp, std::int32_t max_hp);

struct MobBaseStatus {
	std::int32_t str = 0, agi = 0, vit = 0, int_ = 0, dex = 0, luk = 0;
	std::int64_t max_hp = 0;
	std::int64_t max_sp = 0;
	std::int32_t speed = 0;
};
/// BR-MAP-032: status_calc_mob for a levelled monster. With mobs_level_up on and the level above
/// the mob_db level by diff: every stat +diff, then max_hp += diff x VIT and max_sp += diff x INT
/// using the already raised VIT/INT, and speed -= cap_value(diff, 0, speed - 10).
MobBaseStatus levelled_mob_status(const MobBaseStatus& db, std::int32_t level, std::int32_t db_level,
                                  const BattleConfig& cfg);

/// BR-MAP-032: exp bonus (percent points added to mob_dead's `bonus`, which starts at 100) for a
/// levelled monster: (level - db_level) x mobs_level_up_exp_rate.
std::int32_t mob_level_up_exp_bonus(std::int32_t level, std::int32_t db_level, const BattleConfig& cfg);

inline constexpr std::int32_t kItemIdSkull = 7420;   // ITEMID_SKULL_
inline constexpr std::int32_t kCard0Create = 0x00FE; // CARD0_CREATE (made-by-character marker)

/// BR-MAP-033: the skull a dead player leaves on the floor (amount 1, identified, card0
/// CARD0_CREATE, card2/card3 the low/high 16 bits of the victim's char id), or nullopt.
/// bone_drop 2: on every death; 1: only on PvP maps; 0: never. Legacy looks neither at the killer
/// nor at pk_mode, so a death to a monster on a PvP map drops a skull too.
std::optional<Item> death_skull_drop(CharId victim, bool pvp_map, const BattleConfig& cfg);

/// Legacy e_nightmare_drop_type (map.hpp).
inline constexpr std::uint8_t kNightmareDropInventory = 0x1; // NMDT_INVENTORY: unequipped items
inline constexpr std::uint8_t kNightmareDropEquip = 0x2;     // NMDT_EQUIP: equipped items
inline constexpr std::uint8_t kNightmareDropAll = 0x3;       // NMDT_ALL

/// One `pvp_nightmaredrop` mapflag entry (s_drop_list).
struct NightmareDropRule {
	std::int32_t drop_id = 0;  // -1 = "random" (any eligible item), 0 = invalid (skipped), > 0 = item id
	std::int32_t drop_per = 0; // chance in 1/10000
	std::uint8_t type = kNightmareDropAll;
};
/// One inventory slot as the drop check sees it.
struct NightmareSlot {
	std::int32_t nameid = 0; // 0 = empty slot
	std::int32_t amount = 0;
	bool equipped = false;
	bool droppable = true; // pc_candrop(sd, item)
};
/// BR-MAP-034: the slot one rule drops (one unit; the caller unequips it first), or nullopt.
/// - random (-1): the eligible slots (non-empty, droppable, matching `type`) in slot order; if
///   there are any, roll rnd()%10000 < drop_per, then pick rnd() % count.
/// - item id: walk the slots in order; for each slot holding that id that may be dropped, roll
///   rnd()%10000 < drop_per *before* the type check (legacy `&&` order, so a slot of the wrong
///   type still consumes a roll); the first slot that passes both drops.
std::optional<std::size_t> nightmare_drop_pick(const NightmareDropRule& rule, const std::vector<NightmareSlot>& slots,
                                               Rng& rng);
/// BR-MAP-034: every rule of the mapflag in order (the pc_dead loop), assuming each drop succeeds:
/// a dropped slot is unequipped and loses one unit (empty at 0), and later rules see that.
std::vector<std::size_t> nightmare_drops(const std::vector<NightmareDropRule>& rules, std::vector<NightmareSlot> slots,
                                         Rng& rng);

} // namespace rathena::map_rules::progression
