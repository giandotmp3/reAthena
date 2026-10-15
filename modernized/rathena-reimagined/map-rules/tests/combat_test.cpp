// Acceptance tests: rules/combat, BR-MAP-004..020 (spec §5.4, CAP-14).
// Worked examples are taken verbatim from the behavior contract.
#include <gtest/gtest.h>

#include "rathena/map_rules/attr_fix_loader.hpp"
#include "rathena/map_rules/combat.hpp"
#include "golden_vectors.hpp"

#include <filesystem>
#include <fstream>
#include <optional>
#include <set>
#include <sstream>

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
	EXPECT_EQ(cb::max_hp({.base_hp = 5000, .vit = 80, .tier = mr::ClassTier::Transcendent}), 11250u);
}

TEST(Combat, BR_MAP_004_MaxHpIsAtLeastOne) {
	EXPECT_EQ(cb::max_hp({.base_hp = 10, .vit = 0, .flat_bonus = -1000}), 1u);
}

TEST(Combat, BR_MAP_004_MaxHpRunsInDoubleLikeLegacy) {
	// Legacy status_calc_maxhp_pc: 100 x (1.0 + 13 x 0.01) is 112.999... in double -> 112, not 113.
	EXPECT_EQ(cb::max_hp({.base_hp = 100, .vit = 13}), 112u);
	// Class mods: 4th jobs get x1.25 like transcendent; Taekwon rankers x3; 3rd jobs nothing.
	EXPECT_EQ(cb::max_hp({.base_hp = 1000, .vit = 50, .tier = mr::ClassTier::Fourth}), 1875u);
	EXPECT_EQ(cb::max_hp({.base_hp = 1000, .vit = 50, .taekwon_ranker = true}), 4500u);
	EXPECT_EQ(cb::max_hp({.base_hp = 1000, .vit = 50, .tier = mr::ClassTier::Third}), 1500u);
	EXPECT_EQ(cb::max_hp({.base_hp = 1000, .vit = 50, .tier = mr::ClassTier::Transcendent, .taekwon_ranker = true}),
	          1875u)
		<< "x1.25 wins over the ranker bonus";
	// 5000 x 1.8 x 1.25 + 5 equip VIT + 100 = 11355; +10% equip = 12490.5; +7% rate: the increment
	// truncates (874), the total once at the end -> 13364.
	EXPECT_EQ(cb::max_hp({.base_hp = 5000,
	                      .vit = 80,
	                      .tier = mr::ClassTier::Transcendent,
	                      .equip_vit = 5,
	                      .flat_bonus = 100,
	                      .equip_rate_pct = 10,
	                      .rate_bonus_pct = 7}),
	          13364u);
}

TEST(Combat, BR_MAP_005_RenewalBaseAtkWorkedExample) {
	// STR 100, DEX 50, LUK 30, Lv 150 -> 157.
	mr::Stats s{.str = 100, .dex = 50, .luk = 30};
	EXPECT_EQ(cb::base_atk_re(s, 150), 157);
	s.pow = 2;
	EXPECT_EQ(cb::base_atk_re(s, 150), 167);
}

TEST(Combat, BR_MAP_005_RenewalBaseAtkSumsFractionsLikeLegacy) {
	// Legacy sums in tenths: (10 + 4*10/5 + 2*10/3 + 3*10/4) / 10 = (10 + 8 + 6 + 7) / 10 = 3.
	EXPECT_EQ(cb::base_atk_re({.str = 1, .dex = 4, .luk = 2}, 3), 3);
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

TEST(Combat, BR_MAP_008_SoftDefTruncatesOnceLikeLegacy) {
	// Legacy (int)((1 + 0)/2.f + 3/5.f) = (int)1.1 = 1, where per-term truncation would give 0.
	EXPECT_EQ(cb::soft_def_re({.agi = 3, .vit = 0}, 1), 1);
	// (int)(0 + 3/4.f + 4/5.f) = (int)1.55 = 1.
	EXPECT_EQ(cb::soft_mdef_re({.vit = 0, .int_ = 0, .dex = 4}, 3), 1);
	// Negative stats truncate toward zero, as the float cast does: (int)(-3/2.f + 0) = -1.
	EXPECT_EQ(cb::soft_def_re({.agi = 0, .vit = -3}, 0), -1);
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

TEST(Combat, BR_MAP_010_CritRollDrawsEvenWithoutAChance) {
	// Legacy `rnd()%1000 < cri` draws whatever cri is; the random stream must not depend on it.
	mr::SequenceRng rng{{0}};
	EXPECT_FALSE(cb::roll_critical(0, rng));
	EXPECT_FALSE(cb::roll_critical(-50, rng));
	EXPECT_EQ(rng.calls(), 2u);
}

TEST(Combat, BR_MAP_010_RawCritChanceIsNotFloored) {
	// Skill modifiers apply to the raw value: CRI 10 vs LUK 50 is -90 (Sharpshooting's +200 -> 110).
	EXPECT_EQ(cb::crit_chance_raw({.cri = 10, .target_luk = 50}), -90);
	EXPECT_EQ(cb::crit_chance_raw({.cri = 10, .target_luk = 50, .target_sleeping = true}), -180);
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

TEST(Combat, BR_MAP_011_HitRollDrawsEvenWithoutAChance) {
	mr::SequenceRng rng{{0}};
	EXPECT_FALSE(cb::roll_hit(0, rng));
	EXPECT_TRUE(cb::roll_hit(1, rng));
	EXPECT_EQ(rng.calls(), 2u);
}

TEST(Combat, BR_MAP_012_CrowdFleeWorkedExample) {
	// 5 attackers, FLEE 300 -> 210.
	EXPECT_EQ(cb::crowd_flee(300, 5, re()), 210);
	EXPECT_EQ(cb::crowd_flee(300, 2, re()), 300);
}

TEST(Combat, BR_MAP_012_CrowdFleeIsNotFlooredByTheRule) {
	// Legacy stores the value (int16) and then floors it at 1; the rule returns the raw value.
	// 13 attackers: 300 x (100 - 11 x 10) / 100 = -30.
	EXPECT_EQ(cb::crowd_flee(300, 13, re()), -30);
	mr::BattleConfig absolute = re();
	absolute.agi_penalty_type = 2;
	EXPECT_EQ(cb::crowd_flee(30, 5, absolute), 0); // 30 - 3 x 10
	EXPECT_EQ(cb::crowd_flee(20, 5, absolute), -10);
}

TEST(Combat, BR_MAP_013_CrowdDefMinusFivePercentPerExtraAttacker) {
	// 4 attackers: (4 - 2) x 5% = 10% off.
	EXPECT_EQ(cb::crowd_def(100, 4, re()), 90);
	EXPECT_EQ(cb::crowd_def(100, 2, re()), 100);
}

TEST(Combat, BR_MAP_013_CrowdDefMayGoNegativeLikeLegacy) {
	// Type 2 (absolute): hard DEF 10, 6 attackers -> 10 - 4 x 5 = -10 (legacy keeps it).
	mr::BattleConfig absolute = re();
	absolute.vit_penalty_type = 2;
	EXPECT_EQ(cb::crowd_def(10, 6, absolute), -10);
	EXPECT_EQ(cb::crowd_def(-7, 4, re()), -6) << "integer division truncates toward zero";
}

TEST(Combat, BR_MAP_014_RenewalDefenseWorkedExample) {
	// 1000 dmg, DEF 100, softDEF 50 -> 770.
	EXPECT_EQ(cb::apply_defense_re(1000, 100, 50), 770);
}

TEST(Combat, BR_MAP_014_RenewalDefenseKeepsLegacyEdges) {
	// Negative DEF raises the damage: 1000 x 3900 / 3000 = 1300.
	EXPECT_EQ(cb::apply_defense_re(1000, -100, 0), 1300);
	// The result is not floored here (later steps apply the minimum damage).
	EXPECT_EQ(cb::apply_defense_re(10, 0, 30), -20);
	// DEF -400 would divide by zero; legacy reads it as -399: 1000 x 3601 / 10 = 360100.
	EXPECT_EQ(cb::apply_defense_re(1000, -400, 0), 360100);
}

TEST(Combat, BR_MAP_015_PreRenewalPlayerVitDefRange) {
	// Soft DEF 30: [9] + rnd(0, max(0, 6 - 9 - 1)) + [15] = 24 exactly.
	EXPECT_EQ(cb::vit_def_pre_re_player_range(30).min, 24);
	EXPECT_EQ(cb::vit_def_pre_re_player_range(30).max, 24);
	// Soft DEF 99: [29] + rnd(0, 65 - 29 - 1 = 35) + [49] -> 78..113.
	EXPECT_EQ(cb::vit_def_pre_re_player_range(99).min, 78);
	EXPECT_EQ(cb::vit_def_pre_re_player_range(99).max, 113);
}

TEST(Combat, BR_MAP_015_PreRenewalMonsterVitDefRange) {
	// Soft DEF 19: no random part ([19/20]^2 = 0).
	EXPECT_EQ(cb::vit_def_pre_re_monster_range(19).min, 19);
	EXPECT_EQ(cb::vit_def_pre_re_monster_range(19).max, 19);
	// Soft DEF 60: 60 + rnd() % 9 -> 60..68.
	EXPECT_EQ(cb::vit_def_pre_re_monster_range(60).min, 60);
	EXPECT_EQ(cb::vit_def_pre_re_monster_range(60).max, 68);
}

TEST(Combat, BR_MAP_015_PreRenewalDefenseWorkedExample) {
	// DEF 20 -> 80%: 1000 -> 800, minus VIT def 24.
	EXPECT_EQ(cb::apply_defense_pre_re(1000, 20, 24), 776);
	EXPECT_EQ(cb::apply_defense_pre_re(1000, 150, 0), 0) << "DEF is capped at 100";
	EXPECT_EQ(cb::apply_defense_pre_re(10, 0, 30), -20) << "may go negative; min damage is applied later";
	mr::SeededRng rng{7};
	for (int i = 0; i < 100; ++i) {
		const auto r = cb::vit_def_pre_re_player_range(99);
		const auto v = cb::roll_vit_def(r, rng);
		EXPECT_GE(v, r.min);
		EXPECT_LE(v, r.max);
	}
}

namespace {
// One captured call of legacy battle_calc_defense_reduction (pre-renewal build).
void check_pre_re_defense_vector(const golden::Vector& v, bool player_target) {
	const auto def1 = static_cast<std::int32_t>(golden::at(v, "def1"));
	const auto def2 = static_cast<std::int32_t>(golden::at(v, "def2"));
	const auto vit_def = golden::at(v, "vit_def");
	const auto r = player_target ? cb::vit_def_pre_re_player_range(def2) : cb::vit_def_pre_re_monster_range(def2);
	// Player targets can add flat skill bonuses (AL_DP, RA_RANGERMAIN, NC_RESEARCHFE) on top; the
	// capture character has none, so vit_def is exactly the rolled value.
	EXPECT_GE(vit_def, r.min) << golden::str(v);
	EXPECT_LE(vit_def, r.max) << golden::str(v);
	EXPECT_EQ(vit_def - r.min, golden::at(v, "rnd")) << "random part " << golden::str(v);
	if (golden::at(v, "weapon_defense_type") == 0 && golden::at(v, "ignores_def") == 0 && golden::at(v, "piercing") == 0) {
		EXPECT_EQ(cb::apply_defense_pre_re(golden::at(v, "damage_in"), def1, static_cast<std::int32_t>(vit_def)),
		          golden::at(v, "damage_out"))
			<< golden::str(v);
	}
}
} // namespace

TEST(Combat, BR_MAP_015_PreRenewalGoldenVectors) {
	// Captured from legacy battle_calc_defense_reduction in a pre-renewal build: monsters hitting the
	// capture character over a range of VIT, and the character hitting monsters of varied VIT/DEF
	// (tools/golden/capture.sh prere).
	const auto pc = golden::load("defense_pc_pre_re.jsonl");
	const auto mob = golden::load("defense_mob_pre_re.jsonl");
	ASSERT_GE(pc.size(), 20u) << "golden vectors missing; run tools/golden/capture.sh prere";
	ASSERT_GE(mob.size(), 20u) << "golden vectors missing; run tools/golden/capture.sh prere";
	for (const auto& v : pc) check_pre_re_defense_vector(v, true);
	for (const auto& v : mob) check_pre_re_defense_vector(v, false);
}

TEST(Combat, BR_MAP_016_IgnoreDefCappedAtHundredPercent) {
	EXPECT_EQ(cb::capped_ignore_def_pct(150), 100);
	EXPECT_EQ(cb::def_after_ignore(100, 150), 0);
	EXPECT_EQ(cb::def_after_ignore(100, 30), 70);
}

TEST(Combat, BR_MAP_016_IgnoreDefRoundsLikeLegacy) {
	// Legacy: def -= def x i / 100, so DEF 7 with 50% keeps 4 (7 - 3), not 3.
	EXPECT_EQ(cb::def_after_ignore(7, 50), 4);
	// Only the top is capped: a negative total raises DEF.
	EXPECT_EQ(cb::capped_ignore_def_pct(-30), -30);
	EXPECT_EQ(cb::def_after_ignore(100, -50), 150);
}

TEST(Combat, BR_MAP_017_ElementTableWorkedExamples) {
	cb::AttrFixTable t; // entries as in db/re/attr_fix.yml
	t.set(mr::Element::Water, mr::Element::Fire, 1, 150);
	t.set(mr::Element::Neutral, mr::Element::Ghost, 1, 90);
	EXPECT_EQ(cb::apply_element(1000, t, mr::Element::Water, mr::Element::Fire, 1), 1500);
	EXPECT_EQ(cb::apply_element(1000, t, mr::Element::Neutral, mr::Element::Ghost, 1), 900);
}

namespace {
std::optional<cb::AttrFixTable> load_shipped_attr_fix(const char* mode) {
	const std::filesystem::path file = std::filesystem::path(RATHENA_LEGACY_DB_DIR) / mode / "attr_fix.yml";
	std::ifstream in(file);
	if (!in) return std::nullopt;
	auto r = cb::load_attr_fix_yaml(in);
	EXPECT_TRUE(r.table.has_value()) << file << ": " << r.error;
	return r.table;
}
} // namespace

TEST(Combat, BR_MAP_017_ShippedAttrFixTableLoaded) {
	// Neutral vs Ghost Lv1 is where the modes differ: renewal 90% (spec §5.4), pre-renewal 25%.
	struct Mode { const char* dir; std::int64_t neutral_vs_ghost; };
	for (const Mode mode : {Mode{"re", 900}, Mode{"pre-re", 250}}) {
		SCOPED_TRACE(mode.dir);
		const auto t = load_shipped_attr_fix(mode.dir);
		if (!t) GTEST_SKIP() << "legacy db/ not present next to this tree";
		EXPECT_EQ(cb::apply_element(1000, *t, mr::Element::Water, mr::Element::Fire, 1), 1500);
		EXPECT_EQ(cb::apply_element(1000, *t, mr::Element::Neutral, mr::Element::Ghost, 1), mode.neutral_vs_ghost);
		// Same-element attacks are weak at level 1; Neutral never hurts Neutral less than 100%.
		EXPECT_EQ(t->pct(mr::Element::Fire, mr::Element::Fire, 1), 25);
		EXPECT_EQ(t->pct(mr::Element::Neutral, mr::Element::Neutral, 1), 100);
	}
}

TEST(Combat, BR_MAP_017_LoaderRejectsIncompleteOrUnknownInput) {
	std::istringstream partial("Header:\n  Type: ATTRIBUTE_DB\n  Version: 1\nBody:\n  - Level: 1\n    Water:\n      Fire: 150\n");
	auto r = cb::load_attr_fix_yaml(partial);
	EXPECT_FALSE(r.table.has_value());
	EXPECT_NE(r.error.find("incomplete table"), std::string::npos) << r.error;

	std::istringstream unknown("Body:\n  - Level: 1\n    Lava:\n      Fire: 150\n");
	r = cb::load_attr_fix_yaml(unknown);
	EXPECT_FALSE(r.table.has_value());
	EXPECT_NE(r.error.find("unknown attacker element 'Lava'"), std::string::npos) << r.error;

	std::istringstream bad_level("Body:\n  - Level: 5\n");
	r = cb::load_attr_fix_yaml(bad_level);
	EXPECT_NE(r.error.find("level must be 1..4"), std::string::npos) << r.error;
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
	// Captured from legacy status_base_amotion_pc (renewal) over classes x weapons x AGI x DEX,
	// with and without a shield (tools/golden/capture.sh re).
	const auto vectors = golden::load("aspd_re_re.jsonl");
	ASSERT_GE(vectors.size(), 500u) << "golden vectors missing; run tools/golden/capture.sh re";
	std::set<std::int64_t> weapons;
	for (const auto& v : vectors) {
		const auto weapon = golden::at(v, "weapon");
		weapons.insert(weapon);
		const cb::AspdReInput in{
			.job_aspd_base = static_cast<std::int32_t>(golden::at(v, "job_aspd")),
			.agi = static_cast<std::int32_t>(golden::at(v, "agi")),
			.dex = static_cast<std::int32_t>(golden::at(v, "dex")),
			// W_BOW, W_MUSICAL, W_WHIP and W_REVOLVER..W_GRENADE use DEX^2/7
			.ranged_weapon = weapon == 11 || weapon == 13 || weapon == 14 || (weapon >= 17 && weapon <= 21),
			.skill_bonus = static_cast<std::int32_t>(golden::at(v, "skill_bonus")),
			.status_aspd_bonus = static_cast<std::int32_t>(golden::at(v, "status_bonus")),
		};
		EXPECT_EQ(cb::aspd_re_uncapped(in), golden::at(v, "result")) << golden::str(v);
	}
	// Both formula branches must be covered: melee and ranged weapons.
	EXPECT_TRUE(weapons.count(11) || weapons.count(17) || weapons.count(18)) << "no ranged weapon captured";
	EXPECT_TRUE(weapons.count(1) && weapons.count(0)) << "no melee/bare-hand vectors captured";
}

TEST(Combat, BR_MAP_020_PreRenewalAttackDelayWorkedExample) {
	// 600 base, AGI 90, DEX 50 -> 354.
	EXPECT_EQ(cb::amotion_pre_re(600, 90, 50), 354);
}
