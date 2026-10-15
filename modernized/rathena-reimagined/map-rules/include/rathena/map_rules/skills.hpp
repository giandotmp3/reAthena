// rules/skills: CAP-16 skills, BR-MAP-051..055.
// Legacy homes: src/map/skill.cpp (cast/delay/cooldown), battle.cpp (regen cadence).
// Upstream's src/map/skills/ SkillFactory is adopted, not replaced (architecture §3.1, K6):
// these are the cross-skill timing rules only.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "rathena/map_rules/battle_config.hpp"
#include "rathena/map_rules/domain.hpp"

namespace rathena::map_rules::skills {

/// BR-MAP-051 (skill_castfix, pre-renewal): the DEX step of the cast time, computed as legacy does in
/// double: time x (float)(castrate_dex_scale - DEX) / castrate_dex_scale. `instant` when DEX is at or
/// above the scale: legacy then returns 0 at once, before any item or status bonus is applied.
struct PreReDexCast {
	bool instant = false;
	double time = 0;
};
PreReDexCast cast_time_pre_re_dex(double time, std::int32_t dex, const BattleConfig& cfg);
/// BR-MAP-051: the DEX step alone, truncated: base x (castrate_dex_scale - DEX) / castrate_dex_scale,
/// 0 at or above the scale, never negative.
std::int32_t cast_time_pre_re(std::int32_t base_ms, std::int32_t dex, const BattleConfig& cfg);

// BR-MAP-052 (skill_vfcastfix, renewal) in legacy's three steps. Item and status modifiers are applied
// by the caller between the steps, exactly where legacy applies them.

/// Step 1: variable/fixed split. A skill without its own fixed time (`fixed` < 0) gets
/// default_fixed_castrate% of the cast time as fixed time, computed on the truncated time in integers
/// ((int32)time x rate / 100), and keeps time x (100 - rate) / 100 in double as variable time;
/// with default_fixed_castrate <= 0 the fixed time is 0. A skill's own fixed time is kept as is.
struct CastSplit {
	double variable = 0;
	std::int32_t fixed = 0;
};
CastSplit cast_split_re(double time, std::int32_t fixed, const BattleConfig& cfg);
/// Step 2: variable x (1 - sqrt((float)(2 x DEX + INT) / vcast_stat_scale)). Not clamped: a stat
/// sum above the scale makes it negative, and step 3 caps it at 0.
double cast_variable_by_stats_re(double variable, std::int32_t dex, std::int32_t int_, const BattleConfig& cfg);
/// Step 3: variable x (1 - min(var%, 100) / 100), truncated and capped at 0, plus
/// (1 - min(fixed%, 100) / 100) x max(fixed, 0); the factors and the sum are in float, as in legacy.
/// Negative percentages lengthen the cast.
std::int32_t cast_combine_re(double variable, std::int32_t var_reduction_pct, std::int32_t fixed,
                             std::int32_t fixed_reduction_pct);

struct CastReInput {
	std::int32_t base_ms = 0;
	std::int32_t fixed_ms = -1;          // skill_db FixedCastTime; < 0 = default_fixed_castrate% of base
	std::int32_t dex = 0;
	std::int32_t int_ = 0;
	std::int32_t var_reduction_pct = 0;  // reduce_cast_rate (at most 100 counts)
	std::int32_t fixed_reduction_pct = 0; // fixcast_r (at most 100 counts)
};
/// BR-MAP-052: RE cast = variable x (1 - sqrt((2 x DEX + INT) / vcast_stat_scale)) + fixed
/// (default fixed = 20% of base): the three steps above with no other modifier. Never negative.
std::int32_t cast_time_re(const CastReInput& in, const BattleConfig& cfg);

/// Legacy bl_type bits (map.hpp) for `no_skill_delay`.
inline constexpr std::int32_t kBlPc = 0x001;  // BL_PC
inline constexpr std::int32_t kBlMob = 0x002; // BL_MOB

/// How skill_delayfix's `switch (skill_id)` treats the skill.
enum class DelaySkillKind : std::uint8_t {
	Normal,
	Abracadabra, // SA_ABRACADABRA: 0, the picked skill's delay applies instead
	MonkCombo,   // MO_TRIPLEATTACK, MO_CHAINCOMBO, MO_COMBOFINISH, CH_TIGERFIST, CH_CHAINCRUSH,
	             // SR_DRAGONCOMBO, SR_FALLENEMPIRE, SJ_PROMINENCEKICK: 1000 if unset, - 4 AGI - 2 DEX
	Basilica,    // HP_BASILICA: pre-renewal only, no delay when creating it (only when cancelling)
};

/// Delay-flag bits of skill_db `DelayNoDex`-style flags (s_skill_db::delaynodex).
inline constexpr std::uint8_t kDelayNoDex = 0x1;   // not reduced by DEX/AGI
inline constexpr std::uint8_t kDelayNoStatus = 0x2; // not reduced by status changes
inline constexpr std::uint8_t kDelayNoItem = 0x4;   // not changed by item bonuses

struct AfterCastDelayInput {
	DelaySkillKind kind = DelaySkillKind::Normal;
	std::int32_t unit_type = kBlPc;   // BL_* of the caster
	std::int32_t base_delay_ms = 0;   // skill_get_delay; negative = -value + attack motion
	std::int32_t amotion = 0;         // status_get_amotion
	std::uint8_t delaynodex = 0;
	std::int32_t agi = 0;
	std::int32_t dex = 0;
	bool basilica_active = false;     // SC_BASILICA already on the caster (Basilica only)
	bool soul_link_halves = false;    // SC_SPIRIT halving: Crusader link + Shield Boomerang, or
	                                  // Assassin link + Sonic Blow outside GvG/battlegrounds
	std::int32_t status_reduction_pct = 0; // Poem of Bragi val3 + Wind Insignia 50 + Magic Mushroom val2
	std::int32_t bonus_delay_rate = 0;     // sd->bonus.delayrate (bDelayRate n subtracts n)
	std::int32_t bonus_skill_delay_ms = 0; // bonus2 bSkillDelay for this skill
};
/// BR-MAP-053: skill_delayfix (identical in renewal and pre-renewal except Basilica).
/// 1. Units in no_skill_delay (shipped: monsters) get min_skill_delay_limit.
/// 2. base < 0 -> -base + amotion.
/// 3. Monk combos: (base or 1000) - 4 AGI - 2 DEX. Others: when delay_dependon_dex/agi is on (both
///    off as shipped) and DEX/AGI may reduce it, x (castrate_dex_scale - stat) / scale, 0 at or above.
/// 4. Soul-link halving; then + bSkillDelay (players, unless kDelayNoItem).
/// 5. x (1 - min(SC% + bDelayRate%, 100)/100) in float, x delay_rate/100, truncated, at least 0.
std::int32_t after_cast_delay(const AfterCastDelayInput& in, const BattleConfig& cfg);

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

/// BR-MAP-054 cast gate for the legacy cooldown map (`sd.scd`, one entry per active cooldown; the
/// 20-entry limit is the char-server save format). The gate runs where a player cast begins and
/// again where it ends, because the entry is only recorded after the skill has run.
/// `active` is the number of entries, `already_present` whether this skill has one (the other
/// cooldown check refuses it), `will_have_cooldown` whether this cast would add one.
/// An autocast never records a cooldown, so it is never refused. A skill that would need a new
/// entry while the list is full is refused, so it can't be cast and re-cast without a cooldown.
bool cooldown_cast_allowed(std::size_t active, bool already_present, bool will_have_cooldown, bool autocast);

// --- Natural regeneration (status.cpp status_natural_heal, status_calc_regen) -----------------

inline constexpr std::int32_t kNaturalHealInterval = 500; // NATURAL_HEAL_INTERVAL: the regen timer period

enum class RegenUnit : std::uint8_t { Player, Homunculus, Mercenary, Elemental }; // BL_REGEN

/// Inputs of the base HP/SP regen interval for one timer pass.
struct RegenCadenceInput {
	RegenUnit unit = RegenUnit::Player;
	std::int32_t hp_rate_pct = 100; // regen->rate.hp (100 + Tension Relax 200, Regeneration, ...)
	std::int32_t sp_rate_pct = 100; // regen->rate.sp (100 + Magnificat 100, ...)
	bool sitting = false;           // vd->dead_sit == 2: halves the interval
	bool castle_bonus = false;      // regen->state.gc (guild castle owner bonus): halves the interval
	bool walking = false;           // a walk timer is running
	bool regen_while_walking = false; // regen->state.walk (e.g. GT_REVITALIZE)
	bool monk_fury = false;         // Monk class in SC_EXPLOSIONSPIRITS without the Monk soul link
};

/// BR-MAP-055: the HP interval `rate` of status_natural_heal: natural_healhp_interval /
/// (rate.hp/100 x multi) with multi = 1 (+1 sitting, +1 castle), x2 for a walking player,
/// /3 for a homunculus, x4/3 for a mercenary, then - NATURAL_HEAL_INTERVAL/2. A heal happens on
/// the timer pass where `last_heal_tick + rate <= now`; the 250 ms slack makes a 6000 ms interval
/// heal on exactly every 12th 500 ms pass.
std::int32_t natural_hp_regen_rate(const RegenCadenceInput& in, const BattleConfig& cfg);
/// BR-MAP-055: the SP interval: natural_healsp_interval / (rate.sp/100 x multi), /2 for a homunculus,
/// x3/4 for a mercenary, x2 in renewal for a Monk in Fury (pre-renewal blocks SP regen instead,
/// see `natural_sp_regen_allowed`), then - NATURAL_HEAL_INTERVAL/2.
std::int32_t natural_sp_regen_rate(const RegenCadenceInput& in, const BattleConfig& cfg);

/// BR-MAP-055: whether base HP / SP regen runs on a pass (the RGN_HP / RGN_SP flags).
struct RegenGateInput {
	RegenUnit unit = RegenUnit::Player;
	bool dead = false;
	bool hidden = false;       // OPTION_HIDE / CLOAK / CHASEWALK or SC__INVISIBILITY
	bool overweight = false;   // regen->state.overweight (weight >= natural_heal_weight_rate)
	bool no_regen_sc = false;  // poison without slow poison, berserk, bleeding, trick dead, ...
	bool no_sp_regen_sc = false; // dancing, Maximize Power, Oblivion Curse, ... (not the Monk cases)
	bool hp_blocked = false;   // regen->state.block & 1
	bool sp_blocked = false;   // regen->state.block & 2
	bool walking = false;
	bool regen_while_walking = false;
	bool monk_fury = false;    // Monk in SC_EXPLOSIONSPIRITS without the Monk soul link
	bool monk_extremity_fist = false; // Monk in SC_EXTREMITYFIST without the Monk soul link
};
struct RegenGate {
	bool hp = false;
	bool sp = false;
};
RegenGate natural_regen_gate(const RegenGateInput& in, const BattleConfig& cfg);

/// BR-MAP-055: one timer pass for one resource: if `last + rate <= now`, the pass heals and the
/// new last tick is `now`; a pass whose regen is off also resets the last tick to `now`.
struct RegenPass {
	bool heals = false;
	Tick last = 0;
};
RegenPass natural_regen_pass(Tick last, Tick now, std::int32_t rate, bool enabled);

/// BR-MAP-055: number of base HP and SP heals over `elapsed_ms` of 500 ms timer passes, starting
/// right after a heal at tick 0, with a constant state (no full-HP stop).
struct RegenTicks {
	std::int32_t hp_events = 0;
	std::int32_t sp_events = 0;
};
RegenTicks natural_regen(Tick elapsed_ms, const RegenCadenceInput& in, const BattleConfig& cfg);

/// BR-MAP-055: amounts healed per base regen event (status_calc_regen).
struct RegenAmountInput {
	RegenUnit unit = RegenUnit::Player;
	std::int32_t vit = 1;
	std::int32_t int_ = 1;
	std::int64_t max_hp = 1;
	std::int64_t max_sp = 1;
	std::int32_t hprecov_rate = 100; // sd->hprecov_rate (players)
	std::int32_t sprecov_rate = 100; // sd->sprecov_rate (players)
	std::int32_t meditatio_lv = 0;   // HP_MEDITATIO (players)
	std::int32_t hami_skin_lv = 0;   // HAMI_SKIN (homunculus)
	std::int32_t hlif_brain_lv = 0;  // HLIF_BRAIN (homunculus)
};
struct RegenAmounts {
	std::int32_t hp = 0;
	std::int32_t sp = 0;
};
/// Players / homunculi: HP = VIT/5 + max(1, MaxHP/200); SP = 1 + INT/6 + MaxSP/100 (+ (INT-120)/2 + 4
/// at INT >= 120); recovery rates; capped to [1 (0 for players), SHRT_MAX]; Meditatio +3%/lv SP.
/// Mercenaries / elementals: HP = (MaxHP x VIT/10000 + 1) x 6, SP = MaxSP x (INT+10)/750 + 1.
RegenAmounts natural_regen_amounts(const RegenAmountInput& in);

/// BR-MAP-055: skill regen (SM_RECOVERY, MG_SRECOVERY, ...): an accumulator advanced each pass by
/// (int)(diff x rate/100) (halved for sitting regen when overweight) heals once per
/// natural_heal_skill_interval.
struct SkillRegenStep {
	std::int32_t events = 0;
	std::int64_t accumulator = 0;
};
SkillRegenStep skill_regen_step(std::int64_t accumulator, Tick diff, std::int32_t rate_pct, bool halve,
                                const BattleConfig& cfg);

} // namespace rathena::map_rules::skills
