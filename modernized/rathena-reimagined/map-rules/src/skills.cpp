#include "rathena/map_rules/skills.hpp"

#include <algorithm>
#include <cmath>

namespace rathena::map_rules::skills {

std::int32_t cast_time_pre_re(std::int32_t base_ms, std::int32_t dex, const BattleConfig& cfg) {
	if (base_ms <= 0 || cfg.castrate_dex_scale <= 0) {
		return 0;
	}
	const std::int64_t scale = static_cast<std::int64_t>(cfg.castrate_dex_scale) - dex;
	if (scale <= 0) {
		return 0;
	}
	return static_cast<std::int32_t>(static_cast<std::int64_t>(base_ms) * scale / cfg.castrate_dex_scale);
}

std::int32_t cast_time_re(const CastReInput& in, const BattleConfig& cfg) {
	double var = std::max(in.base_ms, 0);
	double fixed = in.fixed_ms;
	if (in.fixed_ms < 0) {
		fixed = var * cfg.default_fixed_castrate / 100.0;
		var -= fixed;
	}
	if (cfg.vcast_stat_scale > 0) {
		const double stat = static_cast<double>(in.dex * 2 + in.int_) / cfg.vcast_stat_scale;
		var *= std::max(0.0, 1.0 - std::sqrt(std::max(stat, 0.0)));
	}
	var *= 1.0 - std::clamp(in.var_reduction_pct, 0, 100) / 100.0;
	const double fixed_part = (1.0 - std::clamp(in.fixed_reduction_pct, 0, 100) / 100.0) * std::max(fixed, 0.0);
	return static_cast<std::int32_t>(std::max(static_cast<std::int32_t>(var), 0) + fixed_part);
}

std::optional<std::int32_t> after_cast_delay(std::int32_t, std::int32_t, std::int32_t, std::int32_t,
                                             const BattleConfig&) {
	return std::nullopt;
}

bool CooldownList::on_cooldown(std::uint16_t skill_id, Tick now) const {
	return std::any_of(entries_.begin(), entries_.end(),
	                   [&](const Entry& e) { return e.skill_id == skill_id && e.until > now; });
}

std::size_t CooldownList::active(Tick now) const {
	return static_cast<std::size_t>(
	    std::count_if(entries_.begin(), entries_.end(), [&](const Entry& e) { return e.until > now; }));
}

bool CooldownList::may_cast(std::uint16_t skill_id, std::int32_t cooldown_ms, Tick now) const {
	if (on_cooldown(skill_id, now)) {
		return false;
	}
	if (cooldown_ms <= 0) {
		return true;
	}
	const bool has_entry = std::any_of(entries_.begin(), entries_.end(),
	                                   [&](const Entry& e) { return e.skill_id == skill_id; });
	return has_entry || active(now) < kMaxSkillCooldowns;
}

void CooldownList::purge(Tick now) {
	entries_.erase(std::remove_if(entries_.begin(), entries_.end(), [&](const Entry& e) { return e.until <= now; }),
	               entries_.end());
}

CooldownList::StartResult CooldownList::start(std::uint16_t skill_id, std::int32_t cooldown_ms, Tick now) {
	if (cooldown_ms <= 0) {
		return StartResult::NoCooldown;
	}
	purge(now);
	for (auto& e : entries_) {
		if (e.skill_id == skill_id) {
			e.until = std::max(e.until, now + cooldown_ms);
			return StartResult::Refreshed;
		}
	}
	if (entries_.size() >= kMaxSkillCooldowns) {
		return StartResult::RejectedFull;
	}
	entries_.push_back({skill_id, now + cooldown_ms});
	return StartResult::Started;
}

std::optional<RegenTicks> natural_regen(Tick, bool, const BattleConfig&) { return std::nullopt; }

} // namespace rathena::map_rules::skills
