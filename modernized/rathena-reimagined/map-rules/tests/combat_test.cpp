// Acceptance tests: rules/combat, BR-MAP-004..020 (spec §5.4, CAP-14).
// Worked examples are taken verbatim from the behavior contract.
#include <gtest/gtest.h>

#include "rathena/map_rules/combat.hpp"

namespace mr = rathena::map_rules;
namespace cb = rathena::map_rules::combat;

namespace {

mr::BattleConfig re() { return {}; }
mr::BattleConfig pre_re() {
	mr::BattleConfig c;
	c.mode = mr::Mode::PreRenewal;
	return c;
}

} // namespace

TEST(Combat, BR_MAP_004_MaxHpTranscendentWorkedExample) {
	// Base 5000, VIT 80, transcendent -> 11250.
	EXPECT_EQ(cb::max_hp({.base_hp = 5000, .vit = 80, .tier = mr::ClassTier::Transcendent}), 11250);
}

TEST(Combat, BR_MAP_004_MaxHpIsAtLeastOne) {
	EXPECT_EQ(cb::max_hp({.base_hp = 10, .vit = 0, .flat_bonus = -1000}), 1);
}

TEST(Combat, BR_MAP_005_RenewalBaseAtkWorkedExample) {
	// STR 100, DEX 50, LUK 30, Lv 150 -> 157.
	mr::Stats s{.str = 100, .dex = 50, .luk = 30};
	EXPECT_EQ(cb::base_atk_re(s, 150), 157);
	s.pow = 2;
	EXPECT_EQ(cb::base_atk_re(s, 150), 167);
}

TEST(Combat, BR_MAP_006_RenewalMatk) {
	// INT 100, DEX 50, LUK 30, Lv 150: 100 + 50 + 10 + 10 + 37 = 207.
	mr::Stats s{.int_ = 100, .dex = 50, .luk = 30};
	EXPECT_EQ(cb::matk_re(s, 150), 207);
}

TEST(Combat, BR_MAP_006_PreRenewalMatkMinMax) {
	// INT 99: min 99 + 14^2 = 295, max 99 + 19^2 = 460.
	mr::Stats s{.int_ = 99};
	const auto m = cb::matk_pre_re(s);
	EXPECT_EQ(m.min, 295);
	EXPECT_EQ(m.max, 460);
}

TEST(Combat, BR_MAP_007_RenewalHitWorkedExample) {
	// Lv 99, DEX 90, LUK 30 -> 374.
	EXPECT_EQ(cb::hit_re({.dex = 90, .luk = 30}, 99), 374);
}

TEST(Combat, BR_MAP_007_RenewalFlee) {
	// 100 + 99 + 90 + 30/5 = 295.
	EXPECT_EQ(cb::flee_re({.agi = 90, .luk = 30}, 99), 295);
}

TEST(Combat, BR_MAP_008_RenewalSoftDefAndMdef) {
	// soft DEF (99+80)/2 + 50/5 = 89 + 10 = 99; soft MDEF 60 + 24 + (40+80)/5 = 108.
	mr::Stats s{.agi = 50, .vit = 80, .int_ = 60, .dex = 40};
	EXPECT_EQ(cb::soft_def_re(s, 99), 99);
	EXPECT_EQ(cb::soft_mdef_re(s, 99), 108);
}

TEST(Combat, BR_MAP_009_RenewalCriWorkedExample) {
	// LUK 30, Lv 99 -> 109.
	EXPECT_EQ(cb::cri_re({.luk = 30}, 99), 109);
}

TEST(Combat, BR_MAP_010_CritChanceWorkedExample) {
	// CRI 109 vs LUK 10 -> 89 per mille = 8.9%.
	EXPECT_EQ(cb::crit_chance_permille({.cri = 109, .target_luk = 10}), 89);
}

TEST(Combat, BR_MAP_010_MobVsPlayerUsesTripleLukAndSleepDoubles) {
	EXPECT_EQ(cb::crit_chance_permille({.cri = 109, .target_luk = 10, .attacker_is_player = false,
	                                    .target_is_player = true}),
	          79);
	EXPECT_EQ(cb::crit_chance_permille({.cri = 109, .target_luk = 10, .target_sleeping = true}), 178);
	EXPECT_EQ(cb::crit_chance_permille({.cri = 10, .target_luk = 50}), 0);
}

TEST(Combat, BR_MAP_010_CritRollIsRndMod1000) {
	mr::SequenceRng below{{88}};
	mr::SequenceRng at{{89}};
	EXPECT_TRUE(cb::roll_critical(89, below));
	EXPECT_FALSE(cb::roll_critical(89, at));
}

TEST(Combat, BR_MAP_011_RenewalHitChanceWorkedExample) {
	// RE 374 vs 300 -> 74%.
	EXPECT_EQ(cb::hit_chance_pct(374, 300, re()), 74);
}

TEST(Combat, BR_MAP_011_PreRenewalBaseAndClamp) {
	EXPECT_EQ(cb::hit_chance_pct(100, 150, pre_re()), 30);
	EXPECT_EQ(cb::hit_chance_pct(10, 500, re()), 5);
	EXPECT_EQ(cb::hit_chance_pct(500, 10, re()), 100);
}

TEST(Combat, BR_MAP_012_CrowdFleeWorkedExample) {
	// 5 attackers, FLEE 300 -> 210.
	EXPECT_EQ(cb::crowd_flee(300, 5, re()), 210);
	EXPECT_EQ(cb::crowd_flee(300, 2, re()), 300);
}

TEST(Combat, BR_MAP_013_CrowdDefMinusFivePercentPerExtraAttacker) {
	// 4 attackers: (4 - 2) x 5% = 10% off.
	EXPECT_EQ(cb::crowd_def(100, 4, re()), 90);
	EXPECT_EQ(cb::crowd_def(100, 2, re()), 100);
}

TEST(Combat, BR_MAP_014_RenewalDefenseWorkedExample) {
	// 1000 dmg, DEF 100, softDEF 50 -> 770.
	EXPECT_EQ(cb::apply_defense_re(1000, 100, 50), 770);
}

TEST(Combat, BR_MAP_015_PreRenewalDefenseWithSeededVitSpread) {
	// DEF 20, VIT 30: 1000 x 80% = 800; VIT def = 15 + rnd()%(30*15/150 = 3).
	mr::SequenceRng rng{{2}};
	EXPECT_EQ(cb::apply_defense_pre_re(1000, 20, 30, rng), 800 - 15 - 2);
	mr::SequenceRng rng0{{0}};
	EXPECT_EQ(cb::apply_defense_pre_re(1000, 20, 10, rng0), 800 - 5); // VIT < 15: no spread
}

TEST(Combat, BR_MAP_015_PreRenewalGoldenVectors) {
	GTEST_SKIP() << "PENDING BR-MAP-015: golden vectors from instrumented legacy battle_calc_defense "
	                "(architecture §3.2 Pin step) not captured yet";
}

TEST(Combat, BR_MAP_016_IgnoreDefCappedAtHundredPercent) {
	EXPECT_EQ(cb::capped_ignore_def_pct(150), 100);
	EXPECT_EQ(cb::def_after_ignore(100, 150), 0);
	EXPECT_EQ(cb::def_after_ignore(100, 30), 70);
}

TEST(Combat, BR_MAP_017_ElementTableWorkedExamples) {
	cb::AttrFixTable t; // entries as in db/re/attr_fix.yml
	t.set(mr::Element::Water, mr::Element::Fire, 1, 150);
	t.set(mr::Element::Neutral, mr::Element::Ghost, 1, 90);
	EXPECT_EQ(cb::apply_element(1000, t, mr::Element::Water, mr::Element::Fire, 1), 1500);
	EXPECT_EQ(cb::apply_element(1000, t, mr::Element::Neutral, mr::Element::Ghost, 1), 900);
}

TEST(Combat, BR_MAP_017_ShippedAttrFixTableLoaded) {
	GTEST_SKIP() << "PENDING BR-MAP-017: attr_fix.yml loader (CAP-23 static data) not in this unit yet";
}

TEST(Combat, BR_MAP_018_SizeModifierWorkedExample) {
	cb::SizeFixTable t; // entries as in db/re/size_fix.yml
	t.set(mr::WeaponType::Knuckle, mr::Size::Large, 75);
	EXPECT_EQ(cb::apply_size(1000, t, mr::WeaponType::Knuckle, mr::Size::Large), 750);
}

TEST(Combat, BR_MAP_019_AspdCappedAt190Or193ForThirdJobs) {
	EXPECT_EQ(cb::cap_aspd(195, mr::ClassTier::Transcendent, re()), 190);
	EXPECT_EQ(cb::cap_aspd(195, mr::ClassTier::Third, re()), 193);
	EXPECT_EQ(cb::cap_aspd(150, mr::ClassTier::Normal, re()), 150);
}

TEST(Combat, BR_MAP_019_RenewalAspdFormulaGoldenVectors) {
	GTEST_SKIP() << "PENDING BR-MAP-019: RE ASPD golden vectors (job_db aspd_base x weapon) not captured yet";
}

TEST(Combat, BR_MAP_020_PreRenewalAttackDelayWorkedExample) {
	// 600 base, AGI 90, DEX 50 -> 354.
	EXPECT_EQ(cb::amotion_pre_re(600, 90, 50), 354);
}
