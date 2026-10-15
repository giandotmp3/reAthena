// Acceptance tests: rename, slot move, sex change, fame lists. BR-CHAR-017, 018, 052, 053.
#include <gtest/gtest.h>

#include <map>

#include "rathena/char_rules/charlife.hpp"

using namespace rathena::char_rules;

namespace {
const auto kNoNames = [](std::string_view) { return false; };

CharacterSummary renamable() {
	CharacterSummary c;
	c.char_id = 150'010;
	c.name = "OldName";
	c.rename_count = 1;
	return c;
}
} // namespace

// ---- BR-CHAR-017 -----------------------------------------------------------------------------------
TEST(CharRename, BR_CHAR_017_RenameConsumesCounter) {
	const auto out = rename_char(renamable(), "NewName", {}, {}, kNoNames);
	EXPECT_EQ(out.result, RenameResult::Ok);
	EXPECT_EQ(out.remaining, 0);
	EXPECT_EQ(out.new_name, "NewName");
}

TEST(CharRename, BR_CHAR_017_NeedsCounterAndNoPartyGuildByDefault) {
	auto c = renamable();
	c.rename_count = 0;
	EXPECT_EQ(rename_char(c, "NewName", {}, {}, kNoNames).result, RenameResult::NoRenamesLeft);
	c = renamable();
	c.party_id = 1;
	EXPECT_EQ(rename_char(c, "NewName", {}, {}, kNoNames).result, RenameResult::InParty);
	RenameConfig allow{true, true};
	EXPECT_EQ(rename_char(c, "NewName", allow, {}, kNoNames).result, RenameResult::Ok);
	c = renamable();
	c.guild_id = 1;
	EXPECT_EQ(rename_char(c, "NewName", {}, {}, kNoNames).result, RenameResult::InGuild);
}

TEST(CharRename, BR_CHAR_017_NameRulesApply) {
	EXPECT_EQ(rename_char(renamable(), "Bad!", {}, {}, kNoNames).result, RenameResult::InvalidName);
	EXPECT_EQ(rename_char(renamable(), "Taken", {}, {}, [](std::string_view n) { return n == "Taken"; }).result,
	          RenameResult::NameTaken);
	EXPECT_EQ(rename_char(renamable(), "Server", {}, {}, kNoNames).result, RenameResult::NameTaken);
}

// ---- BR-CHAR-018 -----------------------------------------------------------------------------------
TEST(CharSlotMove, BR_CHAR_018_MoveToEmptySlotConsumesAMove) {
	std::map<int, CharId> slots{{0, 150'001}};
	const auto occ = [&](int s) { return slots.count(s) ? slots[s] : 0u; };
	const auto out = move_char_slot(0, 4, 9, 1, {}, occ);
	EXPECT_TRUE(out.ok);
	EXPECT_FALSE(out.swapped);
	EXPECT_EQ(out.remaining_moves, 0);
}

TEST(CharSlotMove, BR_CHAR_018_RefusalsAndSwap) {
	std::map<int, CharId> slots{{0, 150'001}, {1, 150'002}};
	const auto occ = [&](int s) { return slots.count(s) ? slots[s] : 0u; };
	SlotMoveConfig cfg;
	EXPECT_FALSE(move_char_slot(0, 4, 9, 0, cfg, occ).ok);  // no moves left
	EXPECT_FALSE(move_char_slot(5, 4, 9, 1, cfg, occ).ok);  // empty source
	EXPECT_FALSE(move_char_slot(0, 9, 9, 1, cfg, occ).ok);  // beyond char_slots
	EXPECT_FALSE(move_char_slot(0, 15, 15, 1, cfg, occ).ok); // beyond MAX_CHARS
	const auto swap = move_char_slot(0, 1, 9, 1, cfg, occ);
	EXPECT_TRUE(swap.ok);
	EXPECT_TRUE(swap.swapped);
	cfg.move_to_used = false;
	EXPECT_FALSE(move_char_slot(0, 1, 9, 1, cfg, occ).ok);
	cfg = {};
	cfg.enabled = false;
	EXPECT_FALSE(move_char_slot(0, 4, 9, 1, cfg, occ).ok);
	cfg = {};
	cfg.unlimited = true;
	const auto unl = move_char_slot(0, 4, 9, 0, cfg, occ);
	EXPECT_TRUE(unl.ok);
	EXPECT_EQ(unl.remaining_moves, 0);
}

// ---- BR-CHAR-052 -----------------------------------------------------------------------------------
TEST(CharSexChange, BR_CHAR_052_SwapsGenderedClassesBothWays) {
	const std::pair<Job, Job> pairs[] = {
	    {Job::Bard, Job::Dancer},           {Job::Clown, Job::Gypsy},         {Job::BabyBard, Job::BabyDancer},
	    {Job::Minstrel, Job::Wanderer},     {Job::MinstrelT, Job::WandererT}, {Job::BabyMinstrel, Job::BabyWanderer},
	    {Job::Kagerou, Job::Oboro},         {Job::BabyKagerou, Job::BabyOboro}, {Job::Troubadour, Job::Trouvere},
	    {Job::Shinkiro, Job::Shiranui}};
	for (const auto& [m, f] : pairs) {
		EXPECT_EQ(gendered_counterpart(m), f);
		EXPECT_EQ(gendered_counterpart(f), m);
	}
	EXPECT_EQ(gendered_counterpart(Job::Novice), Job::Novice);
}

TEST(CharSexChange, BR_CHAR_052_UnequipsEverythingAndClearsLook) {
	const auto out = apply_sex_change(Job::Bard, Sex::Female, /*guild*/ 12);
	EXPECT_EQ(out.job, Job::Dancer);
	EXPECT_EQ(out.sex, Sex::Female);
	EXPECT_TRUE(out.unequip_all);
	EXPECT_EQ(out.look.weapon, 0);
	EXPECT_EQ(out.look.head_top, 0);
	EXPECT_EQ(out.look.robe, 0);
	EXPECT_EQ(out.look.body, static_cast<int>(Job::Dancer));
	EXPECT_TRUE(out.update_guild_member);
	EXPECT_FALSE(apply_sex_change(Job::Novice, Sex::Male, 0).update_guild_member);
}

// ---- BR-CHAR-053 -----------------------------------------------------------------------------------
TEST(CharFame, BR_CHAR_053_KeepsTopNByFameAtMost10) {
	std::vector<FameEntry> all;
	for (int i = 1; i <= 14; ++i) all.push_back({static_cast<CharId>(150'000 + i), i * 10, "c" + std::to_string(i)});
	all.push_back({150'100, 0, "zero"});
	const auto top = build_fame_list(all, 25); // clamped to MAX_FAME_LIST
	ASSERT_EQ(top.size(), 10u);
	EXPECT_EQ(top.front().fame, 140);
	EXPECT_EQ(top.back().fame, 50);
	EXPECT_EQ(build_fame_list(all, 3).size(), 3u);
	EXPECT_TRUE(build_fame_list({{1, 0, "z"}}, 10).empty());
}
