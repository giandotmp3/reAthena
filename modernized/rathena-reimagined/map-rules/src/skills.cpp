#include "rathena/map_rules/skills.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

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

std::int32_t after_cast_delay(const AfterCastDelayInput& in, const BattleConfig& cfg) {
	if (in.kind == DelaySkillKind::Abracadabra) {
		return 0;
	}
	if ((in.unit_type & cfg.no_skill_delay) != 0) {
		return cfg.min_skill_delay_limit;
	}
	double time = in.base_delay_ms;
	if (time < 0) {
		time = -time + in.amotion;
	}
	const auto scale_by = [&](std::int32_t stat) {
		const std::int32_t scale = cfg.castrate_dex_scale - stat;
		time = scale > 0 ? time * scale / cfg.castrate_dex_scale : 0; // "capped later to minimum"
	};
	switch (in.kind) {
	case DelaySkillKind::MonkCombo:
		if (time == 0) {
			time = 1000;
		}
		time -= 4 * in.agi + 2 * in.dex;
		break;
	case DelaySkillKind::Basilica:
		if (cfg.mode == Mode::PreRenewal) {
			if (!in.basilica_active) {
				time = 0;
			}
			break;
		}
		[[fallthrough]]; // renewal: an ordinary skill
	default:
		if (cfg.delay_dependon_dex && (in.delaynodex & kDelayNoDex) == 0) {
			scale_by(in.dex);
		}
		if (cfg.delay_dependon_agi && (in.delaynodex & kDelayNoDex) == 0) {
			scale_by(in.agi);
		}
		break;
	}
	if (in.soul_link_halves) {
		time /= 2;
	}
	std::int32_t delay = 0;
	if ((in.delaynodex & kDelayNoStatus) == 0) {
		delay += in.status_reduction_pct;
	}
	if ((in.delaynodex & kDelayNoItem) == 0 && in.unit_type == kBlPc) {
		delay += in.bonus_delay_rate;
		time += in.bonus_skill_delay_ms;
	}
	if (delay != 0) {
		// Legacy: time * (1 - (float)min(delay, 100) / 100), the factor computed in float.
		const float factor = 1 - static_cast<float>(std::min(delay, 100)) / 100;
		time = time * factor;
	}
	if (cfg.delay_rate != 100) {
		time = time * cfg.delay_rate / 100;
	}
	return std::max(static_cast<std::int32_t>(time), 0);
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

namespace {

// (int32)(interval / (rate/100. * multi)). A zero rate divides by zero in legacy (undefined
// conversion of infinity); here it means "never".
std::int32_t base_interval(std::int32_t interval, std::int32_t rate_pct, std::int32_t multi) {
	if (rate_pct <= 0) {
		return std::numeric_limits<std::int32_t>::max() / 4;
	}
	return static_cast<std::int32_t>(interval / (rate_pct / 100. * multi));
}

std::int32_t regen_multi(const RegenCadenceInput& in) {
	return 1 + (in.sitting ? 1 : 0) + (in.castle_bonus ? 1 : 0);
}

constexpr std::int32_t kShrtMax = 32767; // SHRT_MAX

std::int32_t cap(std::int64_t v, std::int32_t lo) {
	return static_cast<std::int32_t>(v >= kShrtMax ? kShrtMax : (v <= lo ? lo : v));
}

} // namespace

std::int32_t natural_hp_regen_rate(const RegenCadenceInput& in, const BattleConfig& cfg) {
	std::int32_t rate = base_interval(cfg.natural_healhp_interval, in.hp_rate_pct, regen_multi(in));
	if (in.unit == RegenUnit::Player && in.walking) {
		rate *= 2; // only reached with regen_while_walking: plain walking players do not regen HP
	}
	if (in.unit == RegenUnit::Homunculus) {
		rate /= 3;
	}
	if (in.unit == RegenUnit::Mercenary) {
		rate = (rate * 4) / 3;
	}
	return rate - kNaturalHealInterval / 2;
}

std::int32_t natural_sp_regen_rate(const RegenCadenceInput& in, const BattleConfig& cfg) {
	std::int32_t rate = base_interval(cfg.natural_healsp_interval, in.sp_rate_pct, regen_multi(in));
	if (in.unit == RegenUnit::Homunculus) {
		rate /= 2;
	}
	if (in.unit == RegenUnit::Mercenary) {
		rate = (rate * 3) / 4;
	}
	if (cfg.mode == Mode::Renewal && in.unit == RegenUnit::Player && in.monk_fury) {
		rate *= 2; // "Tick is doubled in Fury state"
	}
	return rate - kNaturalHealInterval / 2;
}

RegenGate natural_regen_gate(const RegenGateInput& in, const BattleConfig& cfg) {
	// status_calc_regen_rate
	RegenGate g{true, true};
	if (in.no_regen_sc) {
		g = {false, false};
	}
	const bool monk_no_sp = cfg.mode == Mode::PreRenewal ? (in.monk_extremity_fist || in.monk_fury)
	                                                     : in.monk_extremity_fist;
	if (in.no_sp_regen_sc || (in.unit == RegenUnit::Player && monk_no_sp)) {
		g.sp = false;
	}
	// status_natural_heal
	if (in.hp_blocked) {
		g.hp = false;
	}
	if (in.sp_blocked) {
		g.sp = false;
	}
	if (in.dead || in.hidden || in.overweight) {
		g = {false, false};
	}
	if (in.walking) {
		if (in.unit != RegenUnit::Mercenary && !in.regen_while_walking) {
			g.hp = false;
		}
		// Only a homunculus stops SP regen while walking; players keep it.
		if (in.unit == RegenUnit::Homunculus && !in.regen_while_walking) {
			g.sp = false;
		}
	}
	return g;
}

RegenPass natural_regen_pass(Tick last, Tick now, std::int32_t rate, bool enabled) {
	if (!enabled) {
		return {false, now};
	}
	if (last + rate <= now) {
		return {true, now};
	}
	return {false, last};
}

RegenTicks natural_regen(Tick elapsed_ms, const RegenCadenceInput& in, const BattleConfig& cfg) {
	RegenTicks t;
	const std::int32_t hp_rate = natural_hp_regen_rate(in, cfg);
	const std::int32_t sp_rate = natural_sp_regen_rate(in, cfg);
	Tick hp_last = 0;
	Tick sp_last = 0;
	for (Tick now = kNaturalHealInterval; now <= elapsed_ms; now += kNaturalHealInterval) {
		const auto hp = natural_regen_pass(hp_last, now, hp_rate, true);
		const auto sp = natural_regen_pass(sp_last, now, sp_rate, true);
		hp_last = hp.last;
		sp_last = sp.last;
		t.hp_events += hp.heals ? 1 : 0;
		t.sp_events += sp.heals ? 1 : 0;
	}
	return t;
}

RegenAmounts natural_regen_amounts(const RegenAmountInput& in) {
	RegenAmounts a;
	const bool player = in.unit == RegenUnit::Player;
	const std::int32_t lo = player ? 0 : 1; // reg_flag
	std::int64_t val = in.vit / 5 + std::max<std::int64_t>(1, in.max_hp / 200);
	if (player && in.hprecov_rate != 100) {
		val = val * in.hprecov_rate / 100;
	}
	a.hp = cap(val, lo);
	val = 1 + in.int_ / 6 + in.max_sp / 100;
	if (in.int_ >= 120) {
		val += (in.int_ - 120) / 2 + 4;
	}
	if (player && in.sprecov_rate != 100) {
		val = val * in.sprecov_rate / 100;
	}
	a.sp = cap(val, lo);
	if (player && in.meditatio_lv > 0) {
		a.sp = cap(static_cast<std::int64_t>(a.sp) * (100 + 3 * in.meditatio_lv) / 100, 1);
	}
	switch (in.unit) {
	case RegenUnit::Homunculus:
		if (in.hami_skin_lv > 0) {
			a.hp = cap(static_cast<std::int64_t>(a.hp) * (100 + 5 * in.hami_skin_lv) / 100, 1);
		}
		if (in.hlif_brain_lv > 0) {
			a.sp = cap(static_cast<std::int64_t>(a.sp) * (100 + 3 * in.hlif_brain_lv) / 100, 1);
		}
		break;
	case RegenUnit::Mercenary:
	case RegenUnit::Elemental:
		a.hp = cap(static_cast<std::int64_t>((static_cast<double>(in.max_hp * in.vit) / 10000.0 + 1.0) * 6.0), 1);
		a.sp = cap(static_cast<std::int64_t>((static_cast<double>(in.max_sp) * (in.int_ + 10.0) / 750.0) + 1.0), 1);
		break;
	case RegenUnit::Player:
		break;
	}
	return a;
}

SkillRegenStep skill_regen_step(std::int64_t accumulator, Tick diff, std::int32_t rate_pct, bool halve,
                                const BattleConfig& cfg) {
	SkillRegenStep s;
	auto add = static_cast<std::int32_t>(static_cast<double>(diff) * (rate_pct / 100.));
	if (halve) {
		add /= 2;
	}
	s.accumulator = accumulator + add;
	if (cfg.natural_heal_skill_interval <= 0) {
		return s;
	}
	while (s.accumulator >= cfg.natural_heal_skill_interval) {
		s.accumulator -= cfg.natural_heal_skill_interval;
		++s.events;
	}
	return s;
}

} // namespace rathena::map_rules::skills
