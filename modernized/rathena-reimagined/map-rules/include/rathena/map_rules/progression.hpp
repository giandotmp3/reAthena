// rules/progression: CAP-15 progression, BR-MAP-021..034.
// Legacy homes: src/map/pc.cpp (exp/level/stat), guild.cpp (tax), party.cpp (share).
#pragma once

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

// --- Pending extractions (acceptance tests are GTEST_SKIP'd with their BR-ID) -------------------

/// BR-MAP-032 (pending): mob level after a kill when mob level-up is enabled.
std::optional<std::int32_t> mob_level_after_kill(std::int32_t mob_level, bool enabled);
/// BR-MAP-033 (pending): whether a PK kill drops the victim's skull.
std::optional<bool> drops_skull_on_pk(bool pk_mode, bool killer_is_player);
/// BR-MAP-034 (pending): nightmare-mode item drop selection; nullopt until extracted.
std::optional<std::vector<std::int32_t>> nightmare_drop_indices(std::int32_t inventory_size, Rng& rng);

} // namespace rathena::map_rules::progression
