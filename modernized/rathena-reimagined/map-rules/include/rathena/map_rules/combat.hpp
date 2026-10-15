// rules/combat: CAP-14 combat and stat formulas, BR-MAP-004..020.
// Legacy homes: src/map/status.cpp (derived stats), src/map/battle.cpp (hit/crit/defense).
// Both the renewal and pre-renewal pipelines are in scope (spec §1.1).
#pragma once

#include <array>
#include <cstdint>

#include "rathena/map_rules/battle_config.hpp"
#include "rathena/map_rules/domain.hpp"
#include "rathena/map_rules/rng.hpp"

namespace rathena::map_rules::combat {

// --- Derived stats (status.cpp) ---------------------------------------------------------------

struct MaxHpInput {
	std::uint32_t base_hp = 0;       // job_db HP table at the current base level
	std::uint32_t vit = 0;
	/// Transcendent: any JOBL_UPPER class, transcendent 3rd jobs included; Fourth: a primary 4th
	/// job. Both get x1.25. Third (non-transcendent) and Normal get no class mod.
	ClassTier tier = ClassTier::Normal;
	bool taekwon_ranker = false;     // x3 instead, when no x1.25 applies
	std::int32_t equip_vit = 0;      // VIT from equipment: +1 HP each
	std::int32_t flat_bonus = 0;     // bMaxHP and status bonuses (STATUS_BONUS_FIX)
	std::int32_t equip_rate_pct = 0; // bMaxHPrate from equipment (maxhprate - 100)
	std::int32_t item_rate_pct = 0;  // usable-item rate bonus
	std::int32_t rate_bonus_pct = 0; // STATUS_BONUS_RATE; its increment is truncated ("Aegis accuracy")
};
/// BR-MAP-004: base x (1 + VIT/100) x class mod (+25% transcendent / 4th, x3 Taekwon ranker)
/// + equip VIT + flat bonus, then the rate bonuses; at least 1. Computed in double and truncated
/// once, as legacy status_calc_maxhp_pc.
std::uint32_t max_hp(const MaxHpInput& in);

/// BR-MAP-005: RE base ATK = STR + DEX/5 + LUK/3 + Lv/4 + 5*POW, the first four terms summed in
/// tenths before truncating (legacy status_base_atk).
std::int32_t base_atk_re(const Stats& s, std::int32_t base_level);

struct MatkRange {
	std::int32_t min = 0;
	std::int32_t max = 0;
};
/// BR-MAP-006: RE MATK = INT + INT/2 + DEX/5 + LUK/3 + Lv/4 + 5*SPL.
std::int32_t matk_re(const Stats& s, std::int32_t base_level);
/// BR-MAP-006: pre-RE MATK min = INT + (INT/7)^2, max = INT + (INT/5)^2.
MatkRange matk_pre_re(const Stats& s);

/// BR-MAP-007: RE HIT = 175 + Lv + DEX + LUK/3 + 2*CON.
std::int32_t hit_re(const Stats& s, std::int32_t base_level);
/// BR-MAP-007: RE FLEE = 100 + Lv + AGI + LUK/5 + 2*CON.
std::int32_t flee_re(const Stats& s, std::int32_t base_level);

/// BR-MAP-008: RE soft DEF = (Lv + VIT)/2 + AGI/5, truncated once (not per term).
std::int32_t soft_def_re(const Stats& s, std::int32_t base_level);
/// BR-MAP-008: RE soft MDEF = INT + Lv/4 + (DEX + VIT)/5, truncated once (not per term).
std::int32_t soft_mdef_re(const Stats& s, std::int32_t base_level);

/// BR-MAP-009: RE CRI = 10 + 3*LUK + Lv/10 (units of 0.1%).
std::int32_t cri_re(const Stats& s, std::int32_t base_level);

// --- Hit / crit resolution (battle.cpp) -------------------------------------------------------

struct CritInput {
	std::int32_t cri = 0;          // attacker CRI, 0.1% units
	std::int32_t target_luk = 0;
	bool attacker_is_player = true;
	bool target_is_player = false;
	bool target_sleeping = false;
};
/// BR-MAP-010: CRI - target LUK*2 (*3 when a non-player hits a player), then x2 vs sleeping.
/// Result is in 0.1% units (89 -> 8.9%); never negative.
std::int32_t crit_chance_permille(const CritInput& in);
/// BR-MAP-010: the same before the floor at 0. Legacy is_attack_critical applies skill modifiers
/// (Sharpshooting +, Breaker /2, ...) to this possibly negative value.
std::int32_t crit_chance_raw(const CritInput& in);
/// BR-MAP-010: legacy `rnd()%1000 < cri`. Always draws once, even when the chance is <= 0.
bool roll_critical(std::int32_t permille, Rng& rng);

/// BR-MAP-011: (80 pre-RE / 0 RE) + HIT - FLEE, clamped to [min_hitrate, max_hitrate] (5..100).
std::int32_t hit_chance_pct(std::int32_t hit, std::int32_t flee, const BattleConfig& cfg);
/// BR-MAP-011: legacy `rnd()%100 < hitrate`. Always draws once, even when the chance is <= 0.
bool roll_hit(std::int32_t hit_pct, Rng& rng);

/// BR-MAP-012: with >= agi_penalty_count attackers, FLEE x (100 - (n - (count-1)) x num)%
/// (type 1), or FLEE - (n - (count-1)) x num (type 2). Not floored: legacy stores the value in an
/// int16 and then floors it at 1, which the caller does.
std::int32_t crowd_flee(std::int32_t flee, std::int32_t attackers, const BattleConfig& cfg);
/// BR-MAP-013: with >= vit_penalty_count attackers, DEF x (100 - (n - (count-1)) x num)% (type 1)
/// or DEF - (n - (count-1)) x num (type 2). Not floored: hard DEF may go negative; legacy floors
/// soft DEF at 1 afterwards.
std::int32_t crowd_def(std::int32_t def, std::int32_t attackers, const BattleConfig& cfg);

// --- Defense (battle.cpp) ---------------------------------------------------------------------

/// BR-MAP-014: RE: dmg x (4000 + DEF)/(4000 + 10 x DEF) - softDEF. Negative DEF raises the damage
/// and the result may be negative (later steps floor it), as legacy. DEF -400 is read as -399.
std::int64_t apply_defense_re(std::int64_t damage, std::int32_t hard_def, std::int32_t soft_def);

/// Inclusive range [min, max] of a randomised value.
struct Range {
	std::int32_t min = 0;
	std::int32_t max = 0;
};

/// BR-MAP-015: pre-RE VIT defense of a player target with soft DEF `def2` (battle.cpp,
/// battle_calc_defense_reduction): [3 x def2/10] + rnd(0, max(0, [def2^2/150] - [3 x def2/10] - 1))
/// + [def2/2]. Pinned by golden vectors.
Range vit_def_pre_re_player_range(std::int32_t def2);
/// BR-MAP-015: pre-RE VIT defense of a monster (or pet) target: def2 + rnd() % ([def2/20]^2), the
/// random part being 0 when [def2/20]^2 is 0.
Range vit_def_pre_re_monster_range(std::int32_t def2);
/// Draws a VIT defense uniformly from `r`.
std::int32_t roll_vit_def(Range r, Rng& rng);

/// BR-MAP-015: pre-RE: dmg x (100 - DEF)% - VIT def, with DEF capped at 100. The result may be
/// negative; the post-defense step (refine bonus, then minimum damage 1) handles that, as legacy.
std::int64_t apply_defense_pre_re(std::int64_t damage, std::int32_t hard_def, std::int32_t vit_def);

/// BR-MAP-016: total ignore-DEF percent is capped at 100 (a negative total raises DEF).
std::int32_t capped_ignore_def_pct(std::int32_t summed_bonus_pct);
/// BR-MAP-016: DEF remaining after ignore-DEF bonuses: DEF - DEF x pct/100 (legacy rounding).
std::int32_t def_after_ignore(std::int32_t def, std::int32_t summed_bonus_pct);

// --- Element / size tables (db/{re,pre-re}/attr_fix.yml, size_fix.yml) ------------------------

/// Static data loaded from attr_fix.yml; percentages. Default-constructed entries are 100%.
class AttrFixTable {
public:
	AttrFixTable();
	void set(Element atk, Element def, std::int32_t def_level, std::int32_t pct);
	std::int32_t pct(Element atk, Element def, std::int32_t def_level) const;

private:
	std::array<std::array<std::array<std::int16_t, kElementCount>, kElementCount>, kElementLevels> pct_{};
};
/// BR-MAP-017: damage x attr_fix(atk, def, level)%. Negative percentages heal (returned as-is).
std::int64_t apply_element(std::int64_t damage, const AttrFixTable& table, Element atk, Element def,
                           std::int32_t def_level);

class SizeFixTable {
public:
	SizeFixTable();
	void set(WeaponType w, Size s, std::int32_t pct);
	std::int32_t pct(WeaponType w, Size s) const;

private:
	std::array<std::array<std::int16_t, kSizeCount>, kWeaponTypeCount> pct_{};
};
/// BR-MAP-018: damage x size_fix(weapon, size)%.
std::int64_t apply_size(std::int64_t damage, const SizeFixTable& table, WeaponType w, Size s);

// --- Attack speed (status.cpp) ----------------------------------------------------------------

struct AspdReInput {
	std::int32_t job_aspd_base = 0; // job_db aspd_base[weapon] (+ shield penalty) as legacy
	std::int32_t agi = 0;
	std::int32_t dex = 0;
	bool ranged_weapon = false;      // bow/musical/whip/guns use DEX^2/7 instead of DEX^2/5
	std::int32_t skill_bonus = 0;    // `val` from passive skills / riding
	std::int32_t status_aspd_bonus = 0;
};
/// BR-MAP-019: RE ASPD (display value, before the cap). Single precision, as legacy.
std::int32_t aspd_re_uncapped(const AspdReInput& in);
/// BR-MAP-019: cap to max_aspd (190), or max_third_aspd (193) for 3rd/4th jobs.
std::int32_t cap_aspd(std::int32_t aspd, ClassTier tier, const BattleConfig& cfg);

/// BR-MAP-020: pre-RE attack delay: base - base x (4 x AGI + DEX)/1000 + aspd_add.
std::int32_t amotion_pre_re(std::int32_t base_amotion, std::int32_t agi, std::int32_t dex, std::int32_t aspd_add = 0);

} // namespace rathena::map_rules::combat
