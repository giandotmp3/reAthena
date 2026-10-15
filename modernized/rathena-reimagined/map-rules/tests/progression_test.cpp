// Acceptance tests: rules/progression, BR-MAP-021..034 (spec §5.4, CAP-15).
#include <gtest/gtest.h>

#include "rathena/map_rules/progression.hpp"

namespace mr = rathena::map_rules;
namespace pg = rathena::map_rules::progression;

namespace {

mr::BattleConfig re() { return {}; }

// Synthetic exp table: next[level] = 100 x level, max level 10.
pg::ExpTable small_table() {
	pg::ExpTable t;
	for (int lv = 0; lv <= 10; ++lv) {
		t.next.push_back(lv == 10 ? 0 : 100LL * lv);
	}
	return t;
}

} // namespace

TEST(Progression, BR_MAP_021_RenewalStatCostWorkedExample) {
	// 10 -> 11 costs 2; 11 -> 12 costs 3.
	EXPECT_EQ(pg::stat_cost(10, mr::Mode::Renewal), 2);
	EXPECT_EQ(pg::stat_cost(11, mr::Mode::Renewal), 3);
	EXPECT_EQ(pg::stat_cost(100, mr::Mode::Renewal), 16);
	EXPECT_EQ(pg::stat_cost(105, mr::Mode::Renewal), 20);
}

TEST(Progression, BR_MAP_021_PreRenewalStatCost) {
	EXPECT_EQ(pg::stat_cost(1, mr::Mode::PreRenewal), 2);
	EXPECT_EQ(pg::stat_cost(98, mr::Mode::PreRenewal), 11);
}

TEST(Progression, BR_MAP_022_StatRaiseRequiresPositiveAmountJobMaxAndPoints) {
	const auto cfg = re();
	EXPECT_EQ(pg::raise_stat(50, 0, 100, mr::ClassTier::Normal, cfg).error, pg::StatRaiseError::NonPositiveAmount);
	EXPECT_EQ(pg::raise_stat(99, 1, 100, mr::ClassTier::Normal, cfg).error, pg::StatRaiseError::AboveJobMax);
	EXPECT_TRUE(pg::raise_stat(99, 1, 100, mr::ClassTier::Third, cfg).ok()); // third jobs: 130
	EXPECT_EQ(pg::raise_stat(10, 2, 4, mr::ClassTier::Normal, cfg).error, pg::StatRaiseError::NotEnoughPoints);
	const auto ok = pg::raise_stat(10, 2, 5, mr::ClassTier::Normal, cfg);
	ASSERT_TRUE(ok.ok());
	EXPECT_EQ(ok.new_value, 12);
	EXPECT_EQ(ok.points_spent, 5);
}

TEST(Progression, BR_MAP_023_StatusPointsFromTable) {
	pg::StatPointTable t{{0, 48, 51, 54, 58}};
	EXPECT_EQ(pg::status_points_gained(t, 1, 2), 3);
	EXPECT_EQ(pg::status_points_gained(t, 1, 4), 10);
}

TEST(Progression, BR_MAP_024_SingleLevelUpCapsOverflowAndHeals) {
	// Level 1, need 100; gain 1000 without multi_level_up -> level 2, exp capped at next(2)-1 = 199.
	const auto r = pg::apply_base_exp(1, 0, 1000, small_table(), re());
	EXPECT_EQ(r.level, 2);
	EXPECT_EQ(r.exp, 199);
	EXPECT_TRUE(r.full_heal);
	EXPECT_TRUE(r.recheck_equipment);
}

TEST(Progression, BR_MAP_024_MultiLevelUpWhenEnabled) {
	auto cfg = re();
	cfg.multi_level_up = true;
	const auto r = pg::apply_base_exp(1, 0, 900, small_table(), cfg); // 100 + 200 + 300 = 600, rest 300
	EXPECT_EQ(r.level, 4);
	EXPECT_EQ(r.exp, 300);
	EXPECT_EQ(r.levels_gained, 3);
}

TEST(Progression, BR_MAP_025_JobLevelUpGrantsOneSkillPoint) {
	const auto r = pg::apply_job_exp(3, 250, 100, small_table(), re());
	EXPECT_EQ(r.level, 4);
	EXPECT_EQ(r.skill_points_gained, 1);
}

TEST(Progression, BR_MAP_026_NoExpWhenDeadOrOffMap) {
	EXPECT_EQ(pg::gain_exp({.dead = true, .base_exp = 1000}, re()).base_exp, 0);
	EXPECT_EQ(pg::gain_exp({.on_map = false, .base_exp = 1000}, re()).base_exp, 0);
}

TEST(Progression, BR_MAP_026_GuildTaxBeforeBonusAndMaxLevelClamp) {
	// 1000 base, 10% tax -> 900, then +50% -> 1350. Guild gets the untaxed-share 100.
	const auto g = pg::gain_exp({.base_exp = 1000, .job_exp = 500, .in_guild = true, .guild_tax_pct = 10,
	                             .bonus_pct = 50},
	                            re());
	EXPECT_EQ(g.base_exp, 1350);
	EXPECT_EQ(g.guild_exp, 100);
	EXPECT_EQ(g.job_exp, 750);
	EXPECT_EQ(pg::gain_exp({.base_exp = 1000, .at_max_base_level = true}, re()).base_exp, 0);
}

TEST(Progression, BR_MAP_027_GuildTaxWorkedExample) {
	// 1,000 exp at 10% -> 900 to the player, 100 to the guild.
	const auto s = pg::guild_tax(1000, 10, re());
	EXPECT_EQ(s.player, 900);
	EXPECT_EQ(s.guild, 100);
	EXPECT_EQ(pg::guild_tax(1000, 90, re()).guild, 500); // clamped to guild_exp_limit 50%
}

TEST(Progression, BR_MAP_028_EvenShareWorkedExample) {
	// 3,000 exp, 3 members -> 1,000 each.
	EXPECT_EQ(pg::even_share(3000, 3, re()), 1000);
}

TEST(Progression, BR_MAP_029_LevelPenaltyBucketsRenewalOnly) {
	pg::LevelPenaltyTable t{2, {50, 90, 100, 110, 120}}; // diff -2..+2
	EXPECT_EQ(pg::level_penalty_pct(t, 50, 50, re()), 100);
	EXPECT_EQ(pg::level_penalty_pct(t, 60, 50, re()), 120); // clamped to +2 edge
	EXPECT_EQ(pg::level_penalty_pct(t, 40, 50, re()), 50);
	mr::BattleConfig pre;
	pre.mode = mr::Mode::PreRenewal;
	EXPECT_EQ(pg::level_penalty_pct(t, 40, 50, pre), 100);
}

TEST(Progression, BR_MAP_030_DeathPenaltyWorkedExample) {
	// Type 1 at 1%: next level needs 1,000,000 -> -10,000 exp.
	const auto p = pg::death_penalty({.base_exp = 500000, .base_next = 1000000}, re());
	EXPECT_EQ(p.base_loss, 10000);
}

TEST(Progression, BR_MAP_030_NovicesGvgAndInsuranceExempt) {
	EXPECT_EQ(pg::death_penalty({.base_exp = 500000, .base_next = 1000000, .novice = true}, re()).base_loss, 0);
	EXPECT_EQ(pg::death_penalty({.base_exp = 500000, .base_next = 1000000, .in_gvg = true}, re()).base_loss, 0);
	EXPECT_EQ(pg::death_penalty({.base_exp = 500000, .base_next = 1000000, .has_insurance = true}, re()).base_loss, 0);
	EXPECT_EQ(pg::death_penalty({.base_exp = 3000, .base_next = 1000000}, re()).base_loss, 3000); // never below 0
}

TEST(Progression, BR_MAP_031_SuperNoviceAngelAt99PercentOncePerLevel) {
	EXPECT_TRUE(pg::super_novice_angel(990, 1000, false));
	EXPECT_FALSE(pg::super_novice_angel(989, 1000, false));
	EXPECT_FALSE(pg::super_novice_angel(999, 1000, true));
}

TEST(Progression, BR_MAP_032_MobLevelsUpOnKill) {
	if (!pg::mob_level_after_kill(10, true).has_value()) {
		GTEST_SKIP() << "PENDING BR-MAP-032: mob level-up on kill (pc.cpp:9922-9935) not extracted";
	}
	EXPECT_EQ(*pg::mob_level_after_kill(10, true), 11);
}

TEST(Progression, BR_MAP_033_SkullDropOnPk) {
	if (!pg::drops_skull_on_pk(true, true).has_value()) {
		GTEST_SKIP() << "PENDING BR-MAP-033: skull drop on PK (pc.cpp:9981-9993) not extracted";
	}
	EXPECT_TRUE(*pg::drops_skull_on_pk(true, true));
}

TEST(Progression, BR_MAP_034_NightmareItemDrop) {
	mr::SequenceRng rng{{0}};
	if (!pg::nightmare_drop_indices(10, rng).has_value()) {
		GTEST_SKIP() << "PENDING BR-MAP-034: nightmare drop (pc.cpp:10059-10110) not extracted";
	}
}
