#include "rathena/map_rules/progression.hpp"

#include <algorithm>
#include <limits>

namespace rathena::map_rules::progression {

std::int32_t stat_cost(std::int32_t x, Mode mode) {
	if (mode == Mode::PreRenewal) {
		return 1 + (x + 9) / 10;
	}
	if (x < 100) {
		return 2 + (x - 1) / 10;
	}
	return 16 + 4 * ((x - 100) / 5);
}

StatRaiseResult raise_stat(std::int32_t current, std::int32_t amount, std::int32_t status_points, ClassTier tier,
                           const BattleConfig& cfg) {
	StatRaiseResult r;
	r.new_value = current;
	if (amount <= 0) {
		r.error = StatRaiseError::NonPositiveAmount;
		return r;
	}
	const bool third = tier == ClassTier::Third || tier == ClassTier::Fourth;
	const std::int32_t max = third ? cfg.max_third_parameter : cfg.max_parameter;
	if (static_cast<std::int64_t>(current) + amount > max) {
		r.error = StatRaiseError::AboveJobMax;
		return r;
	}
	std::int64_t cost = 0;
	for (std::int32_t v = current; v < current + amount; ++v) {
		cost += stat_cost(v, cfg.mode);
	}
	if (cost > status_points) {
		r.error = StatRaiseError::NotEnoughPoints;
		return r;
	}
	r.new_value = current + amount;
	r.points_spent = static_cast<std::int32_t>(cost);
	return r;
}

std::int32_t status_points_gained(const StatPointTable& table, std::int32_t from_level, std::int32_t to_level) {
	const auto at = [&](std::int32_t lv) -> std::int32_t {
		if (table.cumulative.empty() || lv < 0) {
			return 0;
		}
		const auto i = std::min<std::size_t>(static_cast<std::size_t>(lv), table.cumulative.size() - 1);
		return table.cumulative[i];
	};
	return std::max(0, at(to_level) - at(from_level));
}

namespace {

std::int64_t next_of(const ExpTable& t, std::int32_t level) {
	if (level < 0 || level >= t.max_level()) {
		return 0; // max level: no further level-up
	}
	return t.next[static_cast<std::size_t>(level)];
}

LevelUpResult apply_exp(std::int32_t level, std::int64_t exp, std::int64_t gained, const ExpTable& table,
                        const BattleConfig& cfg) {
	LevelUpResult r;
	r.level = level;
	r.exp = exp + std::max<std::int64_t>(gained, 0);
	while (true) {
		const std::int64_t need = next_of(table, r.level);
		if (need <= 0 || r.exp < need) {
			break;
		}
		r.exp -= need;
		++r.level;
		++r.levels_gained;
		if (!cfg.multi_level_up) {
			const std::int64_t need_next = next_of(table, r.level);
			if (need_next > 0 && r.exp >= need_next) {
				r.exp = need_next - 1;
			}
			break;
		}
	}
	if (next_of(table, r.level) <= 0) {
		r.exp = 0; // at max level, surplus exp is discarded
	}
	return r;
}

} // namespace

LevelUpResult apply_base_exp(std::int32_t level, std::int64_t exp, std::int64_t gained, const ExpTable& table,
                             const BattleConfig& cfg) {
	auto r = apply_exp(level, exp, gained, table, cfg);
	if (r.levels_gained > 0) {
		r.full_heal = true;
		r.recheck_equipment = true;
	}
	return r;
}

LevelUpResult apply_job_exp(std::int32_t level, std::int64_t exp, std::int64_t gained, const ExpTable& table,
                            const BattleConfig& cfg) {
	auto r = apply_exp(level, exp, gained, table, cfg);
	r.skill_points_gained = r.levels_gained;
	return r;
}

GuildTaxSplit guild_tax(std::int64_t exp, std::int32_t tax_pct, const BattleConfig& cfg) {
	const std::int64_t pct = std::clamp(tax_pct, 0, std::max(cfg.guild_exp_limit, 0));
	const std::int64_t e = std::max<std::int64_t>(exp, 0);
	const std::int64_t tax = e * pct / 100;
	return {e - tax, tax};
}

ExpGain gain_exp(const ExpGainInput& in, const BattleConfig& cfg) {
	ExpGain g;
	if (in.dead || !in.on_map) {
		return g;
	}
	std::int64_t base = std::max<std::int64_t>(in.base_exp, 0);
	std::int64_t job = std::max<std::int64_t>(in.job_exp, 0);
	if (in.in_guild) {
		const auto split = guild_tax(base, in.guild_tax_pct, cfg);
		base = split.player;
		g.guild_exp = split.guild;
	}
	const auto boost = [&](std::int64_t v) {
		const std::int64_t pct = std::max<std::int64_t>(100 + in.bonus_pct, 0);
		constexpr std::int64_t kCap = std::numeric_limits<std::int64_t>::max() / 1000;
		return std::min(v, kCap) * pct / 100;
	};
	g.base_exp = in.at_max_base_level ? 0 : boost(base);
	g.job_exp = in.at_max_job_level ? 0 : boost(job);
	return g;
}

std::int64_t even_share(std::int64_t exp, std::int32_t members, const BattleConfig& cfg) {
	if (members <= 0 || exp <= 0) {
		return 0;
	}
	const std::int64_t bonus = static_cast<std::int64_t>(cfg.party_even_share_bonus) * (members - 1);
	return exp * (100 + bonus) / 100 / members;
}

std::int32_t level_penalty_pct(const LevelPenaltyTable& table, std::int32_t mob_level, std::int32_t player_level,
                               const BattleConfig& cfg) {
	if (cfg.mode == Mode::PreRenewal || table.rate_pct.empty()) {
		return 100;
	}
	const std::int32_t diff = std::clamp(mob_level - player_level, -table.max_diff, table.max_diff);
	const auto i = static_cast<std::size_t>(diff + table.max_diff);
	return i < table.rate_pct.size() ? table.rate_pct[i] : 100;
}

DeathPenalty death_penalty(const DeathPenaltyInput& in, const BattleConfig& cfg) {
	DeathPenalty p;
	if (in.novice || in.in_gvg || in.has_insurance || cfg.death_penalty_type == 0) {
		return p;
	}
	const auto loss = [&](std::int64_t current, std::int64_t next, std::int32_t rate) -> std::int64_t {
		if (rate <= 0) {
			return 0;
		}
		const std::int64_t basis = (cfg.death_penalty_type == 1) ? next : current;
		return std::clamp<std::int64_t>(basis * rate / 10000, 0, std::max<std::int64_t>(current, 0));
	};
	p.base_loss = loss(in.base_exp, in.base_next, cfg.death_penalty_base);
	p.job_loss = loss(in.job_exp, in.job_next, cfg.death_penalty_job);
	return p;
}

bool super_novice_angel(std::int64_t exp, std::int64_t next, bool already_triggered_this_level) {
	if (already_triggered_this_level || next <= 0) {
		return false;
	}
	return exp * 1000 / next >= 990;
}

std::optional<std::int32_t> mob_level_after_kill(std::int32_t, bool) { return std::nullopt; }

std::optional<bool> drops_skull_on_pk(bool, bool) { return std::nullopt; }

std::optional<std::vector<std::int32_t>> nightmare_drop_indices(std::int32_t, Rng&) { return std::nullopt; }

} // namespace rathena::map_rules::progression
