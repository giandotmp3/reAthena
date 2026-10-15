// rules/skills: CAP-16 skills, BR-MAP-051..055.
// Legacy homes: src/map/skill.cpp (cast/delay/cooldown), battle.cpp (regen cadence).
// Upstream's src/map/skills/ SkillFactory is adopted, not replaced (architecture §3.1, K6):
// these are the cross-skill timing rules only.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "rathena/map_rules/battle_config.hpp"
#include "rathena/map_rules/domain.hpp"

namespace rathena::map_rules::skills {

/// BR-MAP-051: pre-RE cast time = base x (castrate_dex_scale - DEX) / castrate_dex_scale; DEX at or
/// above the scale casts instantly.
std::int32_t cast_time_pre_re(std::int32_t base_ms, std::int32_t dex, const BattleConfig& cfg);

struct CastReInput {
	std::int32_t base_ms = 0;
	std::int32_t fixed_ms = -1;          // skill_db FixedCastTime; < 0 = default_fixed_castrate% of base
	std::int32_t dex = 0;
	std::int32_t int_ = 0;
	std::int32_t var_reduction_pct = 0;  // reduce_cast_rate (capped at 100)
	std::int32_t fixed_reduction_pct = 0; // fixcast_r (capped at 100)
};
/// BR-MAP-052: RE cast = variable x (1 - sqrt((2 x DEX + INT) / vcast_stat_scale)) + fixed
/// (default fixed = 20% of base). Never negative.
std::int32_t cast_time_re(const CastReInput& in, const BattleConfig& cfg);

/// BR-MAP-053 (pending): after-cast delay; nullopt until extracted with golden vectors.
std::optional<std::int32_t> after_cast_delay(std::int32_t base_delay_ms, std::int32_t agi, std::int32_t dex,
                                             std::int32_t reduction_pct, const BattleConfig& cfg);

inline constexpr std::size_t kMaxSkillCooldowns = 20; // MAX_SKILLCOOLDOWN

/// BR-MAP-054: per-character cooldown list, at most 20 concurrent entries.
/// [DEFECT fixed] Legacy silently skipped a new cooldown when the list was full, so the skill
/// could be re-cast immediately. Here the cast gate (`may_cast`) refuses a skill that has a
/// cooldown while the list is full, so no skill can ever bypass its cooldown.
class CooldownList {
public:
	enum class StartResult : std::uint8_t { Started, Refreshed, NoCooldown, RejectedFull };

	bool on_cooldown(std::uint16_t skill_id, Tick now) const;
	/// Call before the skill executes. False if on cooldown, or if it would need a new entry
	/// while the list is full of unexpired cooldowns.
	bool may_cast(std::uint16_t skill_id, std::int32_t cooldown_ms, Tick now) const;
	/// Records a cooldown (purging expired entries first).
	StartResult start(std::uint16_t skill_id, std::int32_t cooldown_ms, Tick now);
	std::size_t active(Tick now) const;

private:
	struct Entry {
		std::uint16_t skill_id;
		Tick until;
	};
	void purge(Tick now);
	std::vector<Entry> entries_;
};

/// BR-MAP-055 (pending): natural HP/SP regeneration cadence; nullopt until extracted.
struct RegenTicks {
	std::int32_t hp_events = 0;
	std::int32_t sp_events = 0;
};
std::optional<RegenTicks> natural_regen(Tick elapsed_ms, bool sitting, const BattleConfig& cfg);

} // namespace rathena::map_rules::skills
