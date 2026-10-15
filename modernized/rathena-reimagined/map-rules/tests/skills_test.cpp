// Acceptance tests: rules/skills, BR-MAP-051..055 (spec §5.4, CAP-16).
#include <gtest/gtest.h>

#include "rathena/map_rules/skills.hpp"

namespace mr = rathena::map_rules;
namespace sk = rathena::map_rules::skills;

TEST(Skills, BR_MAP_051_PreRenewalCastTimeScalesWithDex) {
	const mr::BattleConfig cfg;
	EXPECT_EQ(sk::cast_time_pre_re(2000, 75, cfg), 1000);
	EXPECT_EQ(sk::cast_time_pre_re(2000, 0, cfg), 2000);
	EXPECT_EQ(sk::cast_time_pre_re(2000, 150, cfg), 0); // instant cast at castrate_dex_scale
}

TEST(Skills, BR_MAP_051_DexStepKeepsTheFractionForLaterBonuses) {
	// skill_castfix keeps the DEX-scaled time in double: 1000 x 149 / 150 = 993.33..., and only the
	// final result is truncated, after the item and status bonuses.
	const mr::BattleConfig cfg;
	const auto r = sk::cast_time_pre_re_dex(1000, 1, cfg);
	EXPECT_FALSE(r.instant);
	EXPECT_DOUBLE_EQ(r.time, 1000.0 * 149 / 150);
	// Instant cast skips every later bonus (legacy returns 0 at once), even a bVariableCast.
	EXPECT_TRUE(sk::cast_time_pre_re_dex(1000, 150, cfg).instant);
	EXPECT_TRUE(sk::cast_time_pre_re_dex(1000, 200, cfg).instant);
	// A zero cast time still goes through the bonuses.
	const auto zero = sk::cast_time_pre_re_dex(0, 50, cfg);
	EXPECT_FALSE(zero.instant);
	EXPECT_EQ(zero.time, 0.0);
	EXPECT_EQ(sk::cast_time_pre_re(-100, 50, cfg), 0);
}

TEST(Skills, BR_MAP_052_RenewalCastTwentyPercentFixedPlusReducedVariable) {
	const mr::BattleConfig cfg;
	EXPECT_EQ(sk::cast_time_re({.base_ms = 5000}, cfg), 5000);
	// (2 x 265 + 0) / 530 = 1 -> variable part gone, 20% fixed remains.
	EXPECT_EQ(sk::cast_time_re({.base_ms = 5000, .dex = 265}, cfg), 1000);
	// DEX 100, INT 130: 4000 x (1 - sqrt(330/530)) = 843 (+ 1000 fixed).
	EXPECT_EQ(sk::cast_time_re({.base_ms = 5000, .dex = 100, .int_ = 130}, cfg), 1843);
	// Explicit fixed cast is kept separate from the variable part.
	EXPECT_EQ(sk::cast_time_re({.base_ms = 3000, .fixed_ms = 500}, cfg), 3500);
}

TEST(Skills, BR_MAP_052_LegacyStepsAndRounding) {
	const mr::BattleConfig cfg;
	// Step 1: the default fixed part is taken from the truncated time in integers.
	auto split = sk::cast_split_re(1234.9, -1, cfg);
	EXPECT_EQ(split.fixed, 246);                      // (int)1234.9 x 20 / 100
	EXPECT_DOUBLE_EQ(split.variable, 1234.9 * 80 / 100); // the variable part stays in double
	split = sk::cast_split_re(1000, 300, cfg);
	EXPECT_EQ(split.fixed, 300);
	EXPECT_EQ(split.variable, 1000.0);
	auto nofix = cfg;
	nofix.default_fixed_castrate = 0;
	split = sk::cast_split_re(1000, -1, nofix);
	EXPECT_EQ(split.fixed, 0);
	EXPECT_EQ(split.variable, 1000.0);
	// Step 2 is not clamped: stats above the scale make the variable part negative...
	EXPECT_LT(sk::cast_variable_by_stats_re(1000, 300, 0, cfg), 0.0);
	// ...and step 3 caps it at 0, keeping the fixed part.
	EXPECT_EQ(sk::cast_combine_re(-130.5, 0, 800, 0), 800);
	// Negative reductions lengthen the cast; reductions above 100 count as 100.
	EXPECT_EQ(sk::cast_combine_re(1000, -20, 1000, 0), 2200);
	EXPECT_EQ(sk::cast_combine_re(1000, 0, 1000, -50), 2500);
	EXPECT_EQ(sk::cast_combine_re(1000, 150, 1000, 150), 0);
	// The float factor: 1000 x (1 - 0.3f) = 699.99998 -> 699.
	EXPECT_EQ(sk::cast_combine_re(1000, 30, 0, 0), 699);
	EXPECT_EQ(sk::cast_combine_re(0, 0, 1000, 30), 700); // the sum is a float: 0.7f x 1000 = 700
	// A negative fixed time counts as 0.
	EXPECT_EQ(sk::cast_combine_re(500, 0, -1000, 0), 500);
	EXPECT_EQ(sk::cast_time_re({.base_ms = -1}, cfg), 0);
}

// --- BR-MAP-053: skill.cpp skill_delayfix ---------------------------------------------------------

TEST(Skills, BR_MAP_053_ShippedDelayIsTheSkillDbDelay) {
	const mr::BattleConfig cfg; // delay_dependon_dex/agi off, delay_rate 100
	EXPECT_EQ(sk::after_cast_delay({.base_delay_ms = 1000, .agi = 99, .dex = 99}, cfg), 1000);
	// A negative db delay means "add the attack motion": -500 -> 500 + amotion 400.
	EXPECT_EQ(sk::after_cast_delay({.base_delay_ms = -500, .amotion = 400}, cfg), 900);
	EXPECT_EQ(sk::after_cast_delay({.kind = sk::DelaySkillKind::Abracadabra, .base_delay_ms = 1000}, cfg), 0);
	// no_skill_delay (shipped BL_MOB): monsters always get min_skill_delay_limit (100).
	EXPECT_EQ(sk::after_cast_delay({.unit_type = sk::kBlMob, .base_delay_ms = 5000}, cfg), 100);
	EXPECT_EQ(sk::after_cast_delay({.unit_type = sk::kBlMob, .base_delay_ms = 0}, cfg), 100);
}

TEST(Skills, BR_MAP_053_DexAndAgiReductionWhenEnabled) {
	mr::BattleConfig cfg;
	cfg.delay_dependon_dex = true;
	// 1000 x (150 - 75) / 150 = 500.
	EXPECT_EQ(sk::after_cast_delay({.base_delay_ms = 1000, .dex = 75}, cfg), 500);
	EXPECT_EQ(sk::after_cast_delay({.base_delay_ms = 1000, .dex = 150}, cfg), 0);
	cfg.delay_dependon_agi = true;
	// DEX first, then AGI: 1000 x 75/150 = 500, x 120/150 = 400.
	EXPECT_EQ(sk::after_cast_delay({.base_delay_ms = 1000, .agi = 30, .dex = 75}, cfg), 400);
	// DelayNoDex skills ignore both.
	EXPECT_EQ(sk::after_cast_delay({.base_delay_ms = 1000, .delaynodex = sk::kDelayNoDex, .agi = 30, .dex = 75}, cfg),
	          1000);
}

TEST(Skills, BR_MAP_053_MonkCombosAndPreRenewalBasilica) {
	const mr::BattleConfig cfg;
	// Unset delay -> 1000, minus 4 AGI + 2 DEX: 1000 - 400 - 100 = 500.
	EXPECT_EQ(sk::after_cast_delay({.kind = sk::DelaySkillKind::MonkCombo, .agi = 100, .dex = 50}, cfg), 500);
	EXPECT_EQ(sk::after_cast_delay({.kind = sk::DelaySkillKind::MonkCombo, .base_delay_ms = 2000, .agi = 100}, cfg),
	          1600);
	EXPECT_EQ(sk::after_cast_delay({.kind = sk::DelaySkillKind::MonkCombo, .agi = 200, .dex = 200}, cfg), 0);
	// Pre-renewal Basilica: no delay when creating it, the db delay when cancelling it.
	const auto pre = mr::BattleConfig::shipped(mr::Mode::PreRenewal);
	EXPECT_EQ(sk::after_cast_delay({.kind = sk::DelaySkillKind::Basilica, .base_delay_ms = 2000}, pre), 0);
	EXPECT_EQ(
	    sk::after_cast_delay({.kind = sk::DelaySkillKind::Basilica, .base_delay_ms = 2000, .basilica_active = true}, pre),
	    2000);
	EXPECT_EQ(sk::after_cast_delay({.kind = sk::DelaySkillKind::Basilica, .base_delay_ms = 2000}, cfg), 2000);
}

TEST(Skills, BR_MAP_053_StatusAndItemReductionsUseALegacyFloatFactor) {
	const mr::BattleConfig cfg;
	// time * (1 - (float)30 / 100): 1 - 0.3f = 0.699999988f, so 1000 ms -> 699.99998 -> 699.
	EXPECT_EQ(sk::after_cast_delay({.base_delay_ms = 1000, .status_reduction_pct = 30}, cfg), 699);
	EXPECT_EQ(sk::after_cast_delay({.base_delay_ms = 1000, .status_reduction_pct = 10}, cfg), 899);
	EXPECT_EQ(sk::after_cast_delay({.base_delay_ms = 1000, .status_reduction_pct = 50}, cfg), 500);
	// Status + bDelayRate add up (30 + 10 = 40% -> 600) and cap at 100%.
	EXPECT_EQ(sk::after_cast_delay({.base_delay_ms = 1000, .status_reduction_pct = 30, .bonus_delay_rate = 10}, cfg),
	          600);
	EXPECT_EQ(sk::after_cast_delay({.base_delay_ms = 1000, .status_reduction_pct = 80, .bonus_delay_rate = 40}, cfg),
	          0);
	// A negative total lengthens the delay (bDelayRate,20 stores -20): 1000 x 1.2 = 1200.
	EXPECT_EQ(sk::after_cast_delay({.base_delay_ms = 1000, .bonus_delay_rate = -20}, cfg), 1200);
	// bSkillDelay is added before the percentage: (1000 + 200) x 50% = 600.
	EXPECT_EQ(sk::after_cast_delay({.base_delay_ms = 1000, .status_reduction_pct = 50, .bonus_skill_delay_ms = 200},
	                               cfg),
	          600);
	// Flags: NoStatus ignores SC reductions, NoItem ignores item bonuses; item bonuses are players only.
	EXPECT_EQ(sk::after_cast_delay(
	              {.base_delay_ms = 1000, .delaynodex = sk::kDelayNoStatus, .status_reduction_pct = 50}, cfg),
	          1000);
	EXPECT_EQ(sk::after_cast_delay({.base_delay_ms = 1000, .delaynodex = sk::kDelayNoItem, .bonus_delay_rate = 50,
	                                .bonus_skill_delay_ms = 200},
	                               cfg),
	          1000);
	EXPECT_EQ(sk::after_cast_delay({.unit_type = 0x8 /* BL_HOM */, .base_delay_ms = 1000, .bonus_delay_rate = 50}, cfg),
	          1000);
	// Soul-link halving happens before the percentage reductions.
	EXPECT_EQ(sk::after_cast_delay({.base_delay_ms = 1000, .soul_link_halves = true, .status_reduction_pct = 50}, cfg),
	          250);
}

TEST(Skills, BR_MAP_053_DelayRateScalesLast) {
	mr::BattleConfig cfg;
	cfg.delay_rate = 50;
	EXPECT_EQ(sk::after_cast_delay({.base_delay_ms = 1000}, cfg), 500);
	EXPECT_EQ(sk::after_cast_delay({.base_delay_ms = 1000, .status_reduction_pct = 50}, cfg), 250);
	cfg.delay_rate = 0;
	EXPECT_EQ(sk::after_cast_delay({.base_delay_ms = 1000}, cfg), 0);
}

TEST(Skills, BR_MAP_054_CooldownBlocksRecast) {
	sk::CooldownList cd;
	EXPECT_TRUE(cd.may_cast(5, 2000, 0));
	EXPECT_EQ(cd.start(5, 2000, 0), sk::CooldownList::StartResult::Started);
	EXPECT_FALSE(cd.may_cast(5, 2000, 1999));
	EXPECT_TRUE(cd.may_cast(5, 2000, 2000));
}

TEST(Skills, BR_MAP_054_FullListNeverLetsASkillBypassItsCooldown) {
	// [DEFECT fixed] legacy skill_blockpc_start silently skipped the 21st cooldown.
	sk::CooldownList cd;
	for (std::uint16_t id = 1; id <= sk::kMaxSkillCooldowns; ++id) {
		ASSERT_EQ(cd.start(id, 10000, 0), sk::CooldownList::StartResult::Started);
	}
	EXPECT_EQ(cd.active(0), sk::kMaxSkillCooldowns);
	// The 21st skill with a cooldown is refused at the cast gate rather than cast cooldown-free.
	EXPECT_FALSE(cd.may_cast(100, 5000, 10));
	EXPECT_EQ(cd.start(100, 5000, 10), sk::CooldownList::StartResult::RejectedFull);
	EXPECT_FALSE(cd.on_cooldown(100, 10));
	EXPECT_FALSE(cd.may_cast(100, 5000, 20)); // still refused: it can never be cast without a cooldown
	// Skills without a cooldown are unaffected; already-tracked skills refresh in place.
	EXPECT_TRUE(cd.may_cast(101, 0, 10));
	EXPECT_EQ(cd.start(1, 20000, 10000), sk::CooldownList::StartResult::Started); // id 1 had expired
}

TEST(Skills, BR_MAP_054_ExpiredEntriesFreeCapacity) {
	sk::CooldownList cd;
	for (std::uint16_t id = 1; id <= sk::kMaxSkillCooldowns; ++id) {
		ASSERT_EQ(cd.start(id, 1000, 0), sk::CooldownList::StartResult::Started);
	}
	EXPECT_TRUE(cd.may_cast(100, 5000, 1000));
	EXPECT_EQ(cd.start(100, 5000, 1000), sk::CooldownList::StartResult::Started);
	EXPECT_TRUE(cd.on_cooldown(100, 1001));
}

// --- BR-MAP-055: status.cpp status_natural_heal / status_calc_regen / status_calc_regen_rate ----

TEST(Skills, BR_MAP_055_RegenIntervalsFromTheConfiguredCadence) {
	const mr::BattleConfig cfg; // natural_healhp_interval 6000, natural_healsp_interval 8000
	using U = sk::RegenUnit;
	// rate = interval / (rate/100 x multi) - NATURAL_HEAL_INTERVAL/2.
	EXPECT_EQ(sk::natural_hp_regen_rate({}, cfg), 5750);
	EXPECT_EQ(sk::natural_sp_regen_rate({}, cfg), 7750);
	EXPECT_EQ(sk::natural_hp_regen_rate({.sitting = true}, cfg), 2750);                      // multi 2
	EXPECT_EQ(sk::natural_hp_regen_rate({.sitting = true, .castle_bonus = true}, cfg), 1750); // multi 3
	EXPECT_EQ(sk::natural_hp_regen_rate({.hp_rate_pct = 300}, cfg), 1750);                    // Tension Relax
	EXPECT_EQ(sk::natural_sp_regen_rate({.sp_rate_pct = 200}, cfg), 3750);                    // Magnificat
	// A walking player (only with regen_while_walking) regenerates half as often.
	EXPECT_EQ(sk::natural_hp_regen_rate({.walking = true, .regen_while_walking = true}, cfg), 11750);
	// Homunculus: HP /3, SP /2. Mercenary: HP x4/3, SP x3/4.
	EXPECT_EQ(sk::natural_hp_regen_rate({.unit = U::Homunculus}, cfg), 1750);
	EXPECT_EQ(sk::natural_sp_regen_rate({.unit = U::Homunculus}, cfg), 3750);
	EXPECT_EQ(sk::natural_hp_regen_rate({.unit = U::Mercenary}, cfg), 7750);
	EXPECT_EQ(sk::natural_sp_regen_rate({.unit = U::Mercenary}, cfg), 5750);
	EXPECT_EQ(sk::natural_hp_regen_rate({.unit = U::Elemental}, cfg), 5750);
	// Renewal Fury doubles the SP interval; pre-renewal leaves it (and blocks SP regen, below).
	EXPECT_EQ(sk::natural_sp_regen_rate({.monk_fury = true}, cfg), 15750);
	EXPECT_EQ(sk::natural_sp_regen_rate({.monk_fury = true}, mr::BattleConfig::shipped(mr::Mode::PreRenewal)), 7750);
}

TEST(Skills, BR_MAP_055_HealsOnTheFiveHundredMsPassThatReachesTheInterval) {
	// last + rate <= now, with rate 5750: the pass at 5500 does not heal, the one at 6000 does.
	EXPECT_FALSE(sk::natural_regen_pass(0, 5500, 5750, true).heals);
	EXPECT_EQ(sk::natural_regen_pass(0, 5500, 5750, true).last, 0);
	const auto p = sk::natural_regen_pass(0, 6000, 5750, true);
	EXPECT_TRUE(p.heals);
	EXPECT_EQ(p.last, 6000);
	// A pass with regen off resets the last tick, so the interval restarts when it comes back.
	const auto off = sk::natural_regen_pass(0, 6000, 5750, false);
	EXPECT_FALSE(off.heals);
	EXPECT_EQ(off.last, 6000);
}

TEST(Skills, BR_MAP_055_NaturalRegenerationCadence) {
	const mr::BattleConfig cfg;
	// One minute standing: HP every 6 s (10), SP every 8 s (7: 8, 16, ..., 56).
	auto t = sk::natural_regen(60000, {}, cfg);
	EXPECT_EQ(t.hp_events, 10);
	EXPECT_EQ(t.sp_events, 7);
	// Sitting halves both: HP every 3 s (20), SP every 4 s (15).
	t = sk::natural_regen(60000, {.sitting = true}, cfg);
	EXPECT_EQ(t.hp_events, 20);
	EXPECT_EQ(t.sp_events, 15);
	// The 500 ms timer quantises: rate 700% -> 6000/7 = 857 - 250 = 607 -> every 2nd pass (1 s).
	EXPECT_EQ(sk::natural_regen(10000, {.hp_rate_pct = 700}, cfg).hp_events, 10);
	EXPECT_EQ(sk::natural_regen(5999, {}, cfg).hp_events, 0);
	// natural_healhp_interval is the knob.
	mr::BattleConfig fast;
	fast.natural_healhp_interval = 3000;
	EXPECT_EQ(sk::natural_regen(60000, {}, fast).hp_events, 20);
}

TEST(Skills, BR_MAP_055_RegenGate) {
	const mr::BattleConfig re;
	const auto pre = mr::BattleConfig::shipped(mr::Mode::PreRenewal);
	using U = sk::RegenUnit;
	const auto both = [](sk::RegenGate g) { return g.hp && g.sp; };
	const auto none = [](sk::RegenGate g) { return !g.hp && !g.sp; };
	EXPECT_TRUE(both(sk::natural_regen_gate({}, re)));
	EXPECT_TRUE(none(sk::natural_regen_gate({.dead = true}, re)));
	EXPECT_TRUE(none(sk::natural_regen_gate({.hidden = true}, re)));
	EXPECT_TRUE(none(sk::natural_regen_gate({.overweight = true}, re)));
	EXPECT_TRUE(none(sk::natural_regen_gate({.no_regen_sc = true}, re)));
	const auto no_sp = sk::natural_regen_gate({.no_sp_regen_sc = true}, re);
	EXPECT_TRUE(no_sp.hp && !no_sp.sp);
	EXPECT_FALSE(sk::natural_regen_gate({.hp_blocked = true}, re).hp);
	EXPECT_FALSE(sk::natural_regen_gate({.sp_blocked = true}, re).sp);
	// Walking: players lose HP regen but keep SP; mercenaries keep HP; homunculi lose both.
	const auto walk = sk::natural_regen_gate({.walking = true}, re);
	EXPECT_TRUE(!walk.hp && walk.sp);
	EXPECT_TRUE(both(sk::natural_regen_gate({.walking = true, .regen_while_walking = true}, re)));
	EXPECT_TRUE(both(sk::natural_regen_gate({.unit = U::Mercenary, .walking = true}, re)));
	EXPECT_TRUE(none(sk::natural_regen_gate({.unit = U::Homunculus, .walking = true}, re)));
	// Monk Fury: pre-renewal stops SP regen; renewal keeps it (at half speed). Asura stops it in both.
	EXPECT_TRUE(sk::natural_regen_gate({.monk_fury = true}, re).sp);
	EXPECT_FALSE(sk::natural_regen_gate({.monk_fury = true}, pre).sp);
	EXPECT_FALSE(sk::natural_regen_gate({.monk_extremity_fist = true}, re).sp);
	EXPECT_FALSE(sk::natural_regen_gate({.monk_extremity_fist = true}, pre).sp);
}

TEST(Skills, BR_MAP_055_RegenAmounts) {
	using U = sk::RegenUnit;
	// Player: HP 50/5 + max(1, 4000/200) = 30; SP 1 + 60/6 + 500/100 = 16.
	auto a = sk::natural_regen_amounts({.vit = 50, .int_ = 60, .max_hp = 4000, .max_sp = 500});
	EXPECT_EQ(a.hp, 30);
	EXPECT_EQ(a.sp, 16);
	// INT >= 120 bonus: 1 + 21 + 10 + (130 - 120)/2 + 4 = 41.
	EXPECT_EQ(sk::natural_regen_amounts({.int_ = 130, .max_sp = 1000}).sp, 41);
	// Recovery rates and Meditatio (+3%/level): 30 x 150% = 45; 16 x 130% = 20.
	EXPECT_EQ(sk::natural_regen_amounts({.vit = 50, .max_hp = 4000, .hprecov_rate = 150}).hp, 45);
	EXPECT_EQ(sk::natural_regen_amounts({.int_ = 60, .max_sp = 500, .meditatio_lv = 10}).sp, 20);
	// Players may regenerate 0 (reg_flag 0); other units at least 1.
	EXPECT_EQ(sk::natural_regen_amounts({.vit = 1, .max_hp = 100, .hprecov_rate = 0}).hp, 0);
	EXPECT_EQ(sk::natural_regen_amounts({.vit = 1, .max_hp = 100}).hp, 1);
	// Homunculus HAMI_SKIN (+5%/level): 30 x 125% = 37.
	EXPECT_EQ(sk::natural_regen_amounts({.unit = U::Homunculus, .vit = 50, .max_hp = 4000, .hami_skin_lv = 5}).hp, 37);
	// Mercenary / elemental: HP (5000 x 50 / 10000 + 1) x 6 = 156; SP 300 x 30 / 750 + 1 = 13.
	a = sk::natural_regen_amounts({.unit = U::Mercenary, .vit = 50, .int_ = 20, .max_hp = 5000, .max_sp = 300});
	EXPECT_EQ(a.hp, 156);
	EXPECT_EQ(a.sp, 13);
	EXPECT_EQ(sk::natural_regen_amounts({.unit = U::Elemental, .vit = 50, .int_ = 20, .max_hp = 5000, .max_sp = 300}).hp,
	          156);
	// Capped at SHRT_MAX.
	EXPECT_EQ(sk::natural_regen_amounts({.vit = 1, .max_hp = 100000000}).hp, 32767);
}

TEST(Skills, BR_MAP_055_SkillRegenAccumulatesTowardsTheSkillInterval) {
	const mr::BattleConfig cfg; // natural_heal_skill_interval 10000
	auto s = sk::skill_regen_step(0, 500, 100, false, cfg);
	EXPECT_EQ(s.events, 0);
	EXPECT_EQ(s.accumulator, 500);
	s = sk::skill_regen_step(9800, 500, 100, false, cfg);
	EXPECT_EQ(s.events, 1);
	EXPECT_EQ(s.accumulator, 300);
	// Tension Relax (+200%) on skill regen: 1500 per pass. Overweight sitting regen: halved.
	EXPECT_EQ(sk::skill_regen_step(0, 500, 300, false, cfg).accumulator, 1500);
	EXPECT_EQ(sk::skill_regen_step(0, 500, 100, true, cfg).accumulator, 250);
	// Twenty 500 ms passes = one heal every 10 s.
	std::int64_t acc = 0;
	int events = 0;
	for (int i = 0; i < 40; ++i) {
		const auto step = sk::skill_regen_step(acc, 500, 100, false, cfg);
		acc = step.accumulator;
		events += step.events;
	}
	EXPECT_EQ(events, 2);
}
