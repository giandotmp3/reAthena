// Acceptance tests: BR-CHAR-013 -- character deletion cleans up all related data (divorce,
// de-adopt, party, companions, friends both directions, items, regs, skills, mail, SC, quests,
// achievements, guild break or leave), and the target schema's foreign-key actions are verified
// against it.
//
// PENDING: the deletion is executed by the char-server refactor (phase 3) and the order must first
// be pinned with golden vectors from an instrumented legacy build (architecture §3.2). The test
// bodies below are the target assertions; they compile today and run once GTEST_SKIP is removed.
#include <gtest/gtest.h>

#include <algorithm>
#include <string>

#include "rathena/persistence/character_delete.hpp"
#include "rathena/persistence/errors.hpp"
#include "rathena/persistence/in_memory.hpp"
#include "rathena/persistence/foreign_keys.hpp"

using namespace rathena::persistence;

namespace {

std::ptrdiff_t position(CleanupStep s) {
	const auto plan = character_delete_plan();
	const auto it = std::find_if(plan.begin(), plan.end(), [&](const CleanupStepInfo& i) { return i.step == s; });
	return it == plan.end() ? -1 : it - plan.begin();
}

} // namespace

TEST(BR_CHAR_013_CharacterDelete, BR_CHAR_013_PlanCoversEveryCleanupCategory) {
	GTEST_SKIP() << "PENDING BR-CHAR-013: cleanup plan not yet pinned against legacy golden vectors";
	for (const auto s : {CleanupStep::Divorce, CleanupStep::DeAdopt, CleanupStep::LeaveParty,
	                     CleanupStep::DeleteHatchedPet, CleanupStep::DeleteEggPets, CleanupStep::DeleteHomunculus,
	                     CleanupStep::DeleteElemental, CleanupStep::DeleteMercenaryOwner,
	                     CleanupStep::DeleteOwnFriends, CleanupStep::DeleteFromOthersFriends,
	                     CleanupStep::DeleteInventory, CleanupStep::DeleteCart, CleanupStep::DeleteCharRegStr,
	                     CleanupStep::DeleteCharRegNum, CleanupStep::DeleteSkills, CleanupStep::DeleteReceivedMail,
	                     CleanupStep::AnonymizeSentMail, CleanupStep::DeleteStatusChanges, CleanupStep::DeleteQuests,
	                     CleanupStep::DeleteAchievements, CleanupStep::GuildBreakOrLeave,
	                     CleanupStep::DeleteCharacterRow})
		EXPECT_GE(position(s), 0) << to_string(s);
}

TEST(BR_CHAR_013_CharacterDelete, BR_CHAR_013_CascadeOrderDependentsBeforeParent) {
	GTEST_SKIP() << "PENDING BR-CHAR-013: cascade order not yet pinned against legacy golden vectors";
	const auto plan = character_delete_plan();
	ASSERT_FALSE(plan.empty());
	EXPECT_EQ(plan.back().step, CleanupStep::DeleteCharacterRow);
	// Egg pets are found through inventory/cart rows, so they go first.
	EXPECT_LT(position(CleanupStep::DeleteEggPets), position(CleanupStep::DeleteInventory));
	EXPECT_LT(position(CleanupStep::DeleteEggPets), position(CleanupStep::DeleteCart));
	// Attachments before their mail rows.
	EXPECT_LT(position(CleanupStep::DeleteReceivedMailAttachments), position(CleanupStep::DeleteReceivedMail));
	// Target fix vs legacy: guild break/leave runs BEFORE the char row is deleted (FK-safe).
	EXPECT_LT(position(CleanupStep::GuildBreakOrLeave), position(CleanupStep::DeleteCharacterRow));
}

TEST(BR_CHAR_013_CharacterDelete, BR_CHAR_013_TargetForeignKeysAgreeWithCleanupPlan) {
	GTEST_SKIP() << "PENDING BR-CHAR-013: target FK actions verified only after the cleanup order is pinned";
	// Every table the plan deletes from that references `char` must CASCADE or be handled before
	// DeleteCharacterRow; a RESTRICT FK on `char` must be cleared by an earlier plan step.
	for (const auto& fk : target_foreign_keys()) {
		if (fk.parent_table != "character" || fk.child_table == "character") continue;
		if (fk.on_delete != OnDelete::Restrict) continue;
		const auto plan = character_delete_plan();
		const bool cleared = std::any_of(plan.begin(), plan.end() - 1, [&](const CleanupStepInfo& s) {
			return s.logical_table == fk.child_table;
		});
		EXPECT_TRUE(cleared) << fk.child_table << "." << fk.column << " RESTRICTs char deletion";
	}
}

TEST(BR_CHAR_013_CharacterDelete, BR_CHAR_013_RepositoryRemoveLeavesNoOrphans) {
	// The full cleanup on every backend is in repositories_test.cpp (RepositoryContract).
	InMemoryDatabase db;
	InMemoryCharacterRepository chars(db);
	InMemoryPartyRepository parties(db);
	InMemoryMailRepository mail(db);
	Character c;
	c.account = AccountId{2'000'000};
	c.name = "DeleteMe";
	const auto cid = chars.create(c);
	Party p;
	p.name = "PartyOfOne";
	p.leader_char = cid;
	const auto pid = parties.create(p);
	Mail m;
	m.dest = cid;
	m.title = "hi";
	const auto mid = mail.create(m);

	ASSERT_TRUE(chars.remove(cid));
	EXPECT_FALSE(chars.find(cid).has_value());
	EXPECT_FALSE(mail.find(mid).has_value()); // received mail deleted
	(void)pid;
}

TEST(BR_CHAR_013_CharacterDelete, BR_CHAR_013_RemoveOfAnUnknownCharacterChangesNothing) {
	InMemoryDatabase db;
	InMemoryCharacterRepository chars(db);
	EXPECT_FALSE(chars.remove(CharId{150'000}));
}
