#include "rathena/map_rules/combat.hpp"

#include <algorithm>
#include <cmath>

namespace rathena::map_rules::combat {
namespace {

std::int32_t to_i32(std::int64_t v) {
	return static_cast<std::int32_t>(std::clamp<std::int64_t>(v, INT32_MIN, INT32_MAX));
}

std::size_t idx(Element e) { return static_cast<std::size_t>(e); }

} // namespace

std::int32_t max_hp(const MaxHpInput& in) {
	std::int64_t hp = static_cast<std::int64_t>(in.base_hp) * (100 + in.vit) / 100;
	if (in.tier == ClassTier::Transcendent) {
		hp = hp * 125 / 100;
	}
	hp += in.flat_bonus;
	if (in.rate_bonus_pct != 0) {
		hp += hp * in.rate_bonus_pct / 100;
	}
	return to_i32(std::max<std::int64_t>(hp, 1));
}

std::int32_t base_atk_re(const Stats& s, std::int32_t lv) {
	return s.str + s.dex / 5 + s.luk / 3 + lv / 4 + 5 * s.pow;
}

std::int32_t matk_re(const Stats& s, std::int32_t lv) {
	return s.int_ + s.int_ / 2 + s.dex / 5 + s.luk / 3 + lv / 4 + 5 * s.spl;
}

MatkRange matk_pre_re(const Stats& s) {
	const std::int32_t a = s.int_ / 7;
	const std::int32_t b = s.int_ / 5;
	return {s.int_ + a * a, s.int_ + b * b};
}

std::int32_t hit_re(const Stats& s, std::int32_t lv) { return 175 + lv + s.dex + s.luk / 3 + 2 * s.con; }

std::int32_t flee_re(const Stats& s, std::int32_t lv) { return 100 + lv + s.agi + s.luk / 5 + 2 * s.con; }

std::int32_t soft_def_re(const Stats& s, std::int32_t lv) { return (lv + s.vit) / 2 + s.agi / 5; }

std::int32_t soft_mdef_re(const Stats& s, std::int32_t lv) { return s.int_ + lv / 4 + (s.dex + s.vit) / 5; }

std::int32_t cri_re(const Stats& s, std::int32_t lv) { return 10 + 3 * s.luk + lv / 10; }

std::int32_t crit_chance_permille(const CritInput& in) {
	std::int32_t cri = in.cri;
	cri -= in.target_luk * ((!in.attacker_is_player && in.target_is_player) ? 3 : 2);
	if (in.target_sleeping) {
		cri *= 2;
	}
	return std::max(cri, 0);
}

bool roll_critical(std::int32_t permille, Rng& rng) {
	if (permille <= 0) {
		return false;
	}
	return rng.below(1000) < static_cast<std::uint32_t>(permille);
}

std::int32_t hit_chance_pct(std::int32_t hit, std::int32_t flee, const BattleConfig& cfg) {
	const std::int64_t base = (cfg.mode == Mode::PreRenewal) ? 80 : 0;
	const std::int64_t rate = base + static_cast<std::int64_t>(hit) - flee;
	return to_i32(std::clamp<std::int64_t>(rate, cfg.min_hitrate, cfg.max_hitrate));
}

bool roll_hit(std::int32_t hit_pct, Rng& rng) {
	if (hit_pct <= 0) {
		return false;
	}
	return rng.below(100) < static_cast<std::uint32_t>(hit_pct);
}

namespace {
std::int32_t crowd_penalty(std::int32_t value, std::int32_t attackers, std::int32_t type, std::int32_t count,
                           std::int32_t num) {
	if (type == 0 || attackers < count) {
		return value;
	}
	const std::int64_t extra = static_cast<std::int64_t>(attackers) - (count - 1);
	std::int64_t out = value;
	if (type == 1) {
		out = out * std::max<std::int64_t>(0, 100 - extra * num) / 100;
	} else {
		out -= extra * num;
	}
	return to_i32(std::max<std::int64_t>(out, 0));
}
} // namespace

std::int32_t crowd_flee(std::int32_t flee, std::int32_t attackers, const BattleConfig& cfg) {
	return crowd_penalty(flee, attackers, cfg.agi_penalty_type, cfg.agi_penalty_count, cfg.agi_penalty_num);
}

std::int32_t crowd_def(std::int32_t def, std::int32_t attackers, const BattleConfig& cfg) {
	return crowd_penalty(def, attackers, cfg.vit_penalty_type, cfg.vit_penalty_count, cfg.vit_penalty_num);
}

std::int64_t apply_defense_re(std::int64_t damage, std::int32_t hard_def, std::int32_t soft_def) {
	const std::int64_t def = std::max<std::int64_t>(hard_def, 0);
	const std::int64_t reduced = damage * (4000 + def) / (4000 + 10 * def) - soft_def;
	return std::max<std::int64_t>(reduced, 0);
}

Range vit_def_pre_re_player_range(std::int32_t def2) {
	const std::int64_t d = def2;
	const std::int64_t base = 3 * d / 10 + d / 2;
	const std::int64_t spread = std::max<std::int64_t>(0, d * d / 150 - 3 * d / 10 - 1);
	return {to_i32(base), to_i32(base + spread)};
}

Range vit_def_pre_re_monster_range(std::int32_t def2) {
	const std::int64_t n = static_cast<std::int64_t>(def2 / 20) * (def2 / 20);
	return {def2, to_i32(def2 + std::max<std::int64_t>(0, n - 1))};
}

std::int32_t roll_vit_def(Range r, Rng& rng) { return r.max > r.min ? r.min + static_cast<std::int32_t>(rng.below(static_cast<std::uint32_t>(r.max - r.min) + 1u)) : r.min; }

std::int64_t apply_defense_pre_re(std::int64_t damage, std::int32_t hard_def, std::int32_t vit_def) {
	const std::int64_t def = std::min<std::int64_t>(hard_def, 100);
	return damage * (100 - def) / 100 - vit_def;
}

std::int32_t capped_ignore_def_pct(std::int32_t summed_bonus_pct) { return std::clamp(summed_bonus_pct, 0, 100); }

std::int32_t def_after_ignore(std::int32_t def, std::int32_t summed_bonus_pct) {
	const std::int64_t ignore = capped_ignore_def_pct(summed_bonus_pct);
	return to_i32(static_cast<std::int64_t>(def) * (100 - ignore) / 100);
}

AttrFixTable::AttrFixTable() {
	for (auto& lv : pct_) {
		for (auto& row : lv) {
			row.fill(100);
		}
	}
}

void AttrFixTable::set(Element atk, Element def, std::int32_t def_level, std::int32_t pct) {
	const auto l = static_cast<std::size_t>(std::clamp(def_level, 1, 4) - 1);
	pct_[l][idx(atk)][idx(def)] = static_cast<std::int16_t>(std::clamp(pct, -1000, 1000));
}

std::int32_t AttrFixTable::pct(Element atk, Element def, std::int32_t def_level) const {
	const auto l = static_cast<std::size_t>(std::clamp(def_level, 1, 4) - 1);
	return pct_[l][idx(atk)][idx(def)];
}

std::int64_t apply_element(std::int64_t damage, const AttrFixTable& table, Element atk, Element def,
                           std::int32_t def_level) {
	return damage * table.pct(atk, def, def_level) / 100;
}

SizeFixTable::SizeFixTable() {
	for (auto& row : pct_) {
		row.fill(100);
	}
}

void SizeFixTable::set(WeaponType w, Size s, std::int32_t pct) {
	pct_[static_cast<std::size_t>(w)][static_cast<std::size_t>(s)] = static_cast<std::int16_t>(std::clamp(pct, 0, 1000));
}

std::int32_t SizeFixTable::pct(WeaponType w, Size s) const {
	return pct_[static_cast<std::size_t>(w)][static_cast<std::size_t>(s)];
}

std::int64_t apply_size(std::int64_t damage, const SizeFixTable& table, WeaponType w, Size s) {
	return damage * table.pct(w, s) / 100;
}

std::int32_t aspd_re_uncapped(const AspdReInput& in) {
	const double dex = in.dex;
	const double agi = in.agi;
	double temp = in.ranged_weapon ? dex * dex / 7.0 + agi * agi * 0.5 : dex * dex / 5.0 + agi * agi * 0.5;
	temp = std::sqrt(temp) * 0.25 + 196.0;
	const double total = temp + static_cast<double>(in.status_aspd_bonus + in.skill_bonus) * agi / 200.0;
	return static_cast<std::int32_t>(total) - std::min(in.job_aspd_base, 200);
}

std::int32_t cap_aspd(std::int32_t aspd, ClassTier tier, const BattleConfig& cfg) {
	const bool third = tier == ClassTier::Third || tier == ClassTier::Fourth;
	return std::min(aspd, third ? cfg.max_third_aspd : cfg.max_aspd);
}

std::int32_t amotion_pre_re(std::int32_t base_amotion, std::int32_t agi, std::int32_t dex, std::int32_t aspd_add) {
	const std::int64_t a = base_amotion;
	return to_i32(a - a * (4 * static_cast<std::int64_t>(agi) + dex) / 1000 + aspd_add);
}

} // namespace rathena::map_rules::combat
