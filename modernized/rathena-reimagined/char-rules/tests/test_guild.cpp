// Acceptance tests: guilds (CAP-09, char side). BR-CHAR-037..045.
#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

#include "rathena/char_rules/guild.hpp"

using namespace rathena::char_rules;

namespace {
GuildMember member(CharId cid) { return {cid + 1'850'000u, cid, "m" + std::to_string(cid), 0}; }

Guild guild_with(int n_members, GuildId id = 1) {
	auto g = new_guild(id, "Guild" + std::to_string(id), member(150'001));
	for (int i = 1; i < n_members; ++i) {
		auto m = member(150'001 + static_cast<CharId>(i));
		m.position = 19;
		g.members.push_back(m);
	}
	return g;
}

// Synthetic exp table (db/guild_exp.yml values are injected; these are test-only).
std::vector<std::uint64_t> exp_table() {
	std::vector<std::uint64_t> t;
	for (int lv = 1; lv < kMaxGuildLevel; ++lv) t.push_back(static_cast<std::uint64_t>(lv) * 1000);
	return t;
}
} // namespace

// ---- BR-CHAR-037 -----------------------------------------------------------------------------------
TEST(Guild, BR_CHAR_037_CreationUniqueNameDefaultPositionsMax16) {
	NameConfig cfg;
	EXPECT_EQ(check_guild_create("Valkyries", cfg, nullptr), GuildCreateResult::Ok);
	EXPECT_EQ(check_guild_create("Valkyries", cfg, [](std::string_view n) { return n == "Valkyries"; }),
	          GuildCreateResult::NameTaken);
	EXPECT_EQ(check_guild_create("Valk!", cfg, nullptr), GuildCreateResult::InvalidName);

	const auto g = new_guild(7, "Valkyries", member(150'001));
	EXPECT_EQ(g.positions.size(), kMaxGuildPosition);
	EXPECT_EQ(g.positions.front().name, "Position 1");
	EXPECT_NE(g.positions.front().mode, 0u);
	EXPECT_EQ(g.members.front().position, 0);
	EXPECT_EQ(g.max_member, 16);
	EXPECT_EQ(g.level, 1);
}

// ---- BR-CHAR-038 -----------------------------------------------------------------------------------
TEST(Guild, BR_CHAR_038_WorkedExampleExtension10Gives76) {
	EXPECT_EQ(guild_capacity(0), 16);
	EXPECT_EQ(guild_capacity(1), 22);
	EXPECT_EQ(guild_capacity(10), 76);
	EXPECT_EQ(guild_capacity(11), kMaxGuild); // capped at MAX_GUILD
}

TEST(Guild, BR_CHAR_038_AddMemberRespectsCapacity) {
	auto g = guild_with(16);
	EXPECT_FALSE(guild_can_add_member(g));
	g.skills.push_back({kGuildSkillExtension, 1, 10});
	guild_recalculate(g, exp_table());
	EXPECT_EQ(g.max_member, 22);
	EXPECT_TRUE(guild_can_add_member(g));
}

// ---- BR-CHAR-039 -----------------------------------------------------------------------------------
TEST(Guild, BR_CHAR_039_LevelUpWhileExpAtLeastNextAddsSkillPoint) {
	auto g = guild_with(1);
	g.exp = 1000 + 2000 + 500; // enough for 1->2 and 2->3
	guild_recalculate(g, exp_table());
	EXPECT_EQ(g.level, 3);
	EXPECT_EQ(g.skill_points, 2);
	EXPECT_EQ(g.exp, 500u);
	EXPECT_EQ(g.next_exp, 3000u);
}

TEST(Guild, BR_CHAR_039_StopsAtLevel50) {
	auto g = guild_with(1);
	g.exp = UINT64_C(1) << 40;
	guild_recalculate(g, exp_table());
	EXPECT_EQ(g.level, kMaxGuildLevel);
	EXPECT_EQ(g.skill_points, kMaxGuildLevel - 1);
}

// ---- BR-CHAR-040 -----------------------------------------------------------------------------------
TEST(Guild, BR_CHAR_040_SkillUpNeedsPointsAndBelowMax) {
	auto g = guild_with(1);
	g.skills.push_back({kGuildSkillExtension, 9, 10});
	EXPECT_FALSE(guild_can_skill_up(g, kGuildSkillExtension)); // no points
	g.skill_points = 2;
	EXPECT_TRUE(guild_skill_up(g, kGuildSkillExtension));
	EXPECT_EQ(guild_skill_level(g, kGuildSkillExtension), 10);
	EXPECT_EQ(g.skill_points, 1);
	EXPECT_EQ(g.max_member, 76);
	EXPECT_FALSE(guild_skill_up(g, kGuildSkillExtension)); // at max
	EXPECT_FALSE(guild_skill_up(g, 12345));                 // unknown skill
}

// ---- BR-CHAR-041 -----------------------------------------------------------------------------------
TEST(Guild, BR_CHAR_041_ExpulsionListIsFifoMax32) {
	auto g = guild_with(40);
	for (int i = 1; i <= 34; ++i) {
		EXPECT_FALSE(guild_expel(g, 150'001 + static_cast<CharId>(i), "bye"));
	}
	EXPECT_EQ(g.expulsions.size(), kMaxGuildExpulsion);
	EXPECT_EQ(g.expulsions.front().char_id, 150'004u); // first two dropped (FIFO)
	EXPECT_EQ(g.expulsions.back().char_id, 150'035u);
}

TEST(Guild, BR_CHAR_041_EmptyGuildIsBroken) {
	auto g = guild_with(1);
	EXPECT_TRUE(guild_expel(g, 150'001, "last"));
}

// ---- BR-CHAR-042 -----------------------------------------------------------------------------------
TEST(Guild, BR_CHAR_042_BreakDeletesAllGuildDataAndClearsMembers) {
	auto g = guild_with(3, 1);
	auto other = guild_with(1, 2);
	ASSERT_EQ(guild_add_alliance(g, other, AllianceKind::Ally), AllianceResult::Ok);
	const auto plan = guild_break_plan(g);
	EXPECT_EQ(plan.clear_guild_id_for, (std::vector<CharId>{150'001, 150'002, 150'003}));
	EXPECT_EQ(plan.remove_alliance_from, (std::vector<GuildId>{2}));
	EXPECT_TRUE(plan.delete_guild_row && plan.delete_members && plan.delete_positions && plan.delete_skills &&
	            plan.delete_expulsions && plan.delete_storage && plan.delete_storage_log && plan.delete_emblem &&
	            plan.release_castles);
}

TEST(Guild, BR_CHAR_042_BreakIsOneTransactionInPersistence) {
	GTEST_SKIP() << "PENDING BR-CHAR-042: GuildRepository executes GuildBreakPlan atomically (InnoDB after M2); "
	                "integration test against a ${CHAR_DB_URL} test schema";
}

// ---- BR-CHAR-043 -----------------------------------------------------------------------------------
TEST(Guild, BR_CHAR_043_AllianceBidirectionalOppositionUnidirectional) {
	auto a = guild_with(1, 1);
	auto b = guild_with(1, 2);
	auto c = guild_with(1, 3);
	EXPECT_EQ(guild_add_alliance(a, b, AllianceKind::Ally), AllianceResult::Ok);
	EXPECT_EQ(a.alliances.size(), 1u);
	EXPECT_EQ(b.alliances.size(), 1u);
	EXPECT_EQ(guild_add_alliance(a, c, AllianceKind::Opposition), AllianceResult::Ok);
	EXPECT_EQ(a.alliances.size(), 2u);
	EXPECT_TRUE(c.alliances.empty());
	EXPECT_EQ(guild_add_alliance(a, b, AllianceKind::Ally), AllianceResult::AlreadyLinked);
	EXPECT_EQ(guild_add_alliance(a, a, AllianceKind::Ally), AllianceResult::SameGuild);
}

TEST(Guild, BR_CHAR_043_MaxSixteen) {
	auto a = guild_with(1, 1);
	std::vector<Guild> others;
	for (GuildId id = 2; id < 2 + kMaxGuildAlliance + 1; ++id) others.push_back(guild_with(1, id));
	for (std::size_t i = 0; i < kMaxGuildAlliance; ++i) {
		ASSERT_EQ(guild_add_alliance(a, others[i], AllianceKind::Ally), AllianceResult::Ok);
	}
	EXPECT_EQ(guild_add_alliance(a, others.back(), AllianceKind::Ally), AllianceResult::LimitReached);
}

// ---- BR-CHAR-044 -----------------------------------------------------------------------------------
TEST(Guild, BR_CHAR_044_MasterTransferSwapsSlotZeroAndRecordsTime) {
	auto g = guild_with(3);
	EXPECT_TRUE(guild_change_master(g, 150'003, 1'760'000'000));
	EXPECT_EQ(g.members.front().char_id, 150'003u);
	EXPECT_EQ(g.members.front().position, 0);
	EXPECT_EQ(g.members.back().char_id, 150'001u);
	EXPECT_EQ(g.members.back().position, 19);
	EXPECT_EQ(g.last_master_change, 1'760'000'000);
	EXPECT_FALSE(guild_change_master(g, 150'003, 1)); // already master
	EXPECT_FALSE(guild_change_master(g, 999, 1));
}

// ---- BR-CHAR-045 -----------------------------------------------------------------------------------
TEST(Guild, BR_CHAR_045_StorageOfficial100Plus100PerLevelElse600) {
	EXPECT_EQ(guild_storage_capacity(1, true), 200);
	EXPECT_EQ(guild_storage_capacity(5, true), 600);
	EXPECT_EQ(guild_storage_capacity(0, true), 0);
	EXPECT_EQ(guild_storage_capacity(0, false), 600);
	EXPECT_EQ(guild_storage_capacity(3, false), 600);
}
