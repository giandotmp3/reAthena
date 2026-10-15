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
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <regex>
#include <string>
#include <vector>

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

namespace {

/// One statement legacy char_delete() ran, from tools/golden/capture.sh chardel.
std::vector<std::string> legacy_delete_statements() {
	std::vector<std::string> out;
	std::ifstream in(std::filesystem::path(PERSISTENCE_GOLDEN_DIR) / "char_delete_sql.jsonl");
	std::string line;
	const std::string key = "\"sql\": \"";
	while (std::getline(in, line)) {
		const auto b = line.find(key);
		const auto e = line.rfind('"');
		if (b != std::string::npos && e > b + key.size()) out.push_back(line.substr(b + key.size(), e - b - key.size()));
	}
	return out;
}

/// The cleanup step a legacy statement belongs to; nullopt for statements that clean nothing up
/// (the initial SELECT, the guild lookup, the charlog/interlog audit inserts).
std::optional<CleanupStep> step_of(const std::string& sql) {
	struct Rule {
		const char* pattern;
		std::optional<CleanupStep> step;
	};
	static const Rule rules[] = {
		{R"(^SELECT )", std::nullopt},
		{R"(^INSERT INTO `(charlog|interlog)`)", std::nullopt},
		{R"(^UPDATE `char` SET `partner_id`)", CleanupStep::Divorce},
		{R"(^DELETE FROM `inventory` WHERE .*`nameid`)", CleanupStep::Divorce}, // wedding rings
		{R"(^UPDATE `char` SET `child`)", CleanupStep::DeAdopt},
		{R"(^DELETE FROM `skill` WHERE `id` = '410')", CleanupStep::DeAdopt},
		{R"(^UPDATE `char` SET `party_id`)", CleanupStep::LeaveParty},
		{R"(^(DELETE FROM|UPDATE) `party`)", CleanupStep::LeaveParty},
		{R"(^DELETE FROM `pet` WHERE `char_id`)", CleanupStep::DeleteHatchedPet},
		{R"(^DELETE FROM `pet` WHERE `pet_id` IN)", CleanupStep::DeleteEggPets},
		{R"(^DELETE FROM `(homunculus|skill_homunculus|skillcooldown_homunculus)`)", CleanupStep::DeleteHomunculus},
		{R"(^DELETE FROM `elemental`)", CleanupStep::DeleteElemental},
		{R"(^DELETE FROM `(mercenary|mercenary_owner|skillcooldown_mercenary)`)", CleanupStep::DeleteMercenaryOwner},
		{R"(^DELETE FROM `friends` WHERE `char_id`)", CleanupStep::DeleteOwnFriends},
		{R"(^DELETE FROM `friends` WHERE `friend_id`)", CleanupStep::DeleteFromOthersFriends},
		{R"(^DELETE FROM `hotkey`)", CleanupStep::DeleteHotkeys},
		{R"(^DELETE FROM `inventory`)", CleanupStep::DeleteInventory},
		{R"(^DELETE FROM `cart_inventory`)", CleanupStep::DeleteCart},
		{R"(^DELETE FROM `memo`)", CleanupStep::DeleteMemo},
		{R"(^DELETE FROM `char_reg_str`)", CleanupStep::DeleteCharRegStr},
		{R"(^DELETE FROM `char_reg_num`)", CleanupStep::DeleteCharRegNum},
		{R"(^DELETE FROM `skill`)", CleanupStep::DeleteSkills},
		{R"(^DELETE FROM `mail_attachments`)", CleanupStep::DeleteReceivedMailAttachments},
		{R"(^DELETE FROM `mail` WHERE `dest_id`)", CleanupStep::DeleteReceivedMail},
		{R"(^UPDATE `mail` SET `send_id`)", CleanupStep::AnonymizeSentMail},
		{R"(^DELETE FROM `sc_data`)", CleanupStep::DeleteStatusChanges},
		{R"(^DELETE FROM `bonus_script`)", CleanupStep::DeleteBonusScripts},
		{R"(^DELETE FROM `quest`)", CleanupStep::DeleteQuests},
		{R"(^DELETE FROM `achievement`)", CleanupStep::DeleteAchievements},
		{R"(^DELETE FROM `char` WHERE `char_id`)", CleanupStep::DeleteCharacterRow},
		{R"(^(DELETE FROM `guild[a-z_]*`|UPDATE `char` SET `guild_id`|UPDATE `guild[a-z_]*`))", CleanupStep::GuildBreakOrLeave},
	};
	for (const auto& r : rules)
		if (std::regex_search(sql, std::regex(r.pattern))) return r.step;
	ADD_FAILURE() << "legacy statement not mapped to a cleanup step: " << sql;
	return std::nullopt;
}

} // namespace

TEST(BR_CHAR_013_CharacterDelete, BR_CHAR_013_PlanFollowsLegacyGoldenOrder) {
	// tools/golden/capture.sh chardel: a character leading a party and a guild, with a pet egg,
	// items, a skill, variables, a received mail, a status change and a bonus script, deleted
	// through the client (CH_DELETE_CHAR3); every statement char_delete() ran, in order.
	const auto statements = legacy_delete_statements();
	ASSERT_GE(statements.size(), 20u) << "golden vectors missing; run tools/golden/capture.sh chardel";

	std::vector<CleanupStep> legacy; // distinct steps in first-run order
	for (const auto& sql : statements) {
		const auto s = step_of(sql);
		if (s && std::find(legacy.begin(), legacy.end(), *s) == legacy.end()) legacy.push_back(*s);
	}
	for (const auto s : legacy) EXPECT_GE(position(s), 0) << to_string(s) << " is missing from the plan";

	// Legacy breaks or leaves the guild AFTER deleting the char row; the target plan does it before,
	// so the guild_member/guild FKs never see a missing character (the one deliberate reordering).
	const auto guild = std::find(legacy.begin(), legacy.end(), CleanupStep::GuildBreakOrLeave);
	const auto row = std::find(legacy.begin(), legacy.end(), CleanupStep::DeleteCharacterRow);
	ASSERT_NE(guild, legacy.end());
	ASSERT_NE(row, legacy.end());
	EXPECT_GT(guild, row) << "legacy order changed: guild handling no longer follows the row delete";
	legacy.erase(guild);
	EXPECT_LT(position(CleanupStep::GuildBreakOrLeave), position(CleanupStep::DeleteCharacterRow));

	// Every other step keeps the legacy relative order.
	for (std::size_t i = 1; i < legacy.size(); ++i)
		EXPECT_LT(position(legacy[i - 1]), position(legacy[i]))
			<< to_string(legacy[i - 1]) << " must run before " << to_string(legacy[i]) << " (legacy order)";
}

TEST(BR_CHAR_013_CharacterDelete, BR_CHAR_013_LegacyLeavesMailAttachmentsBehind) {
	// Legacy deletes received mail rows but never their attachments (orphans in mail_attachments);
	// the target plan deletes them first and the target FK cascades.
	const auto statements = legacy_delete_statements();
	ASSERT_FALSE(statements.empty());
	EXPECT_TRUE(std::none_of(statements.begin(), statements.end(),
	                         [](const std::string& s) { return s.find("`mail_attachments`") != std::string::npos; }));
	EXPECT_LT(position(CleanupStep::DeleteReceivedMailAttachments), position(CleanupStep::DeleteReceivedMail));
}
