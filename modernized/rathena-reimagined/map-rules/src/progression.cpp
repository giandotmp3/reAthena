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

std::uint64_t even_share(std::uint64_t exp, std::uint32_t members, std::uint64_t max_exp,
                         const BattleConfig& cfg) {
	if (members == 0) {
		return 0;
	}
	std::uint64_t share = exp / members;
	if (cfg.party_even_share_bonus != 0 && members > 1 && share != 0) {
		// `100 + party_even_share_bonus*(c-1)` with c uint32: unsigned arithmetic, then double.
		const double bonus =
		    static_cast<double>(100u + static_cast<std::uint32_t>(cfg.party_even_share_bonus) * (members - 1));
		const double scaled = static_cast<double>(share) * bonus / 100;
		// cap_value(scaled, 0, MAX_EXP) is a double expression; the bound converts to double too.
		share = static_cast<std::uint64_t>(scaled >= static_cast<double>(max_exp) ? static_cast<double>(max_exp)
		                                   : scaled <= 0                          ? 0.0
		                                                                          : scaled);
	}
	return share;
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
	// get_percentage_exp(exp, next) >= 99: floor(x) >= 99 <=> x >= 99 (and its UINT32_MAX clamp is
	// above 99 too).
	return 100.0 * (static_cast<double>(exp) / static_cast<double>(next)) >= 99.0;
}

MobLevelUp mob_level_after_kill(const MobKillLevelUpInput& in, const BattleConfig& cfg) {
	MobLevelUp r;
	r.level = in.mob_level;
	// pc_dead: mobs_level_up && md->status.hp && (uint32)md->level < pc_maxbaselv(sd)
	//          && !md->guardian_data && !md->special_state.ai
	if (!cfg.mobs_level_up || in.mob_hp == 0 ||
	    static_cast<std::uint32_t>(in.mob_level) >= static_cast<std::uint32_t>(in.victim_max_base_level) ||
	    in.guardian || in.summoned_ai) {
		return r;
	}
	r.levelled = true;
	r.level = in.mob_level + 1;
	r.refresh_name = (cfg.show_mob_info & 4) != 0;
	return r;
}

std::int32_t mob_level_up_heal(std::int32_t hp, std::int32_t max_hp) {
	// status_percent_change(hp_rate = 10): apply_rate(status->hp, 10), at least 1.
	std::int64_t heal = hp > 100000 ? hp / 100 * 10 : static_cast<std::int64_t>(hp) * 10 / 100;
	if (heal == 0) {
		heal = 1;
	}
	// status_heal never heals past max_hp.
	return static_cast<std::int32_t>(std::clamp<std::int64_t>(heal, 0, std::max<std::int64_t>(
	                                                                       static_cast<std::int64_t>(max_hp) - hp, 0)));
}

MobBaseStatus levelled_mob_status(const MobBaseStatus& db, std::int32_t level, std::int32_t db_level,
                                  const BattleConfig& cfg) {
	MobBaseStatus s = db;
	if (!cfg.mobs_level_up || level <= db_level) {
		return s;
	}
	const std::int32_t diff = level - db_level;
	s.str += diff;
	s.agi += diff;
	s.vit += diff;
	s.int_ += diff;
	s.dex += diff;
	s.luk += diff;
	s.max_hp += static_cast<std::int64_t>(diff) * s.vit;
	s.max_sp += static_cast<std::int64_t>(diff) * s.int_;
	// cap_value(diff, 0, speed - 10), in the macro's order (the max bound is tested first).
	const std::int32_t hi = s.speed - 10;
	s.speed -= diff >= hi ? hi : (diff <= 0 ? 0 : diff);
	return s;
}

std::int32_t mob_level_up_exp_bonus(std::int32_t level, std::int32_t db_level, const BattleConfig& cfg) {
	if (!cfg.mobs_level_up || level <= db_level) {
		return 0;
	}
	return (level - db_level) * cfg.mobs_level_up_exp_rate;
}

std::optional<Item> death_skull_drop(CharId victim, bool pvp_map, const BattleConfig& cfg) {
	if (!(cfg.bone_drop == 2 || (cfg.bone_drop == 1 && pvp_map))) {
		return std::nullopt;
	}
	const auto id = static_cast<std::uint32_t>(victim.value);
	Item skull;
	skull.nameid = kItemIdSkull;
	skull.amount = 1;
	skull.identify = true;
	// GetWord(char_id, 0) / GetWord(char_id, 1).
	skull.card = {kCard0Create, 0, static_cast<std::int32_t>(id & 0xFFFFu), static_cast<std::int32_t>(id >> 16)};
	return skull;
}

namespace {

bool nightmare_type_matches(std::uint8_t type, const NightmareSlot& slot) {
	return ((type & kNightmareDropInventory) != 0 && !slot.equipped) ||
	       ((type & kNightmareDropEquip) != 0 && slot.equipped) || type == kNightmareDropAll;
}

// Legacy `rnd()%10000 < per` always consumes a roll, even when per >= 10000 (unlike Rng::chance).
bool nightmare_roll(std::int32_t per, Rng& rng) { return static_cast<std::int64_t>(rng.below(10000)) < per; }

} // namespace

std::optional<std::size_t> nightmare_drop_pick(const NightmareDropRule& rule, const std::vector<NightmareSlot>& slots,
                                               Rng& rng) {
	if (rule.drop_id == -1) {
		std::vector<std::size_t> eligible;
		for (std::size_t i = 0; i < slots.size(); ++i) {
			if (slots[i].nameid == 0 || !slots[i].droppable) {
				continue;
			}
			if (nightmare_type_matches(rule.type, slots[i])) {
				eligible.push_back(i);
			}
		}
		if (eligible.empty() || !nightmare_roll(rule.drop_per, rng)) {
			return std::nullopt;
		}
		return eligible[rng.below(static_cast<std::uint32_t>(eligible.size()))];
	}
	if (rule.drop_id > 0) {
		for (std::size_t i = 0; i < slots.size(); ++i) {
			if (slots[i].nameid == rule.drop_id && slots[i].droppable && nightmare_roll(rule.drop_per, rng) &&
			    nightmare_type_matches(rule.type, slots[i])) {
				return i;
			}
		}
	}
	return std::nullopt; // drop_id 0 (an invalid item id in the mapflag) is skipped
}

std::vector<std::size_t> nightmare_drops(const std::vector<NightmareDropRule>& rules, std::vector<NightmareSlot> slots,
                                         Rng& rng) {
	std::vector<std::size_t> dropped;
	for (const auto& rule : rules) {
		const auto pick = nightmare_drop_pick(rule, slots, rng);
		if (!pick) {
			continue;
		}
		dropped.push_back(*pick);
		auto& slot = slots[*pick];
		slot.equipped = false; // pc_unequipitem(sd, n, 3)
		if (--slot.amount <= 0) {
			slot = NightmareSlot{}; // pc_dropitem -> pc_delitem empties the slot
		}
	}
	return dropped;
}

} // namespace rathena::map_rules::progression
