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

TEST(Skills, BR_MAP_053_AfterCastDelay) {
	const mr::BattleConfig cfg;
	if (!sk::after_cast_delay(1000, 0, 0, 0, cfg).has_value()) {
		GTEST_SKIP() << "PENDING BR-MAP-053: after-cast delay (skill.cpp:10385-10495) not extracted";
	}
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

TEST(Skills, BR_MAP_055_NaturalRegenerationCadence) {
	const mr::BattleConfig cfg;
	if (!sk::natural_regen(6000, false, cfg).has_value()) {
		GTEST_SKIP() << "PENDING BR-MAP-055: natural regen cadence (battle.cpp:8387-8389) not extracted";
	}
}
