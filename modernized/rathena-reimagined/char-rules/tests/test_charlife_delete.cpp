// Acceptance tests: timed deletion, selection, character ban (CAP-05). BR-CHAR-009..016.
#include <gtest/gtest.h>

#include <algorithm>

#include "rathena/char_rules/charlife.hpp"

using namespace rathena::char_rules;

namespace {
constexpr UnixTime kNow = 1'760'000'000;

CharacterSummary character() {
	CharacterSummary c;
	c.char_id = 150'001;
	c.account_id = 2'000'001;
	c.name = "Adventurer";
	c.base_level = 50;
	return c;
}
} // namespace

// ---- BR-CHAR-009 -----------------------------------------------------------------------------------
TEST(CharDelete, BR_CHAR_009_RequestQueuesDeleteDateNowPlusDelay) {
	DeletionConfig cfg;
	const auto out = request_deletion(character(), true, cfg, kNow);
	EXPECT_EQ(out.result, DeleteRequestResult::Queued);
	EXPECT_EQ(out.delete_date, kNow + 86'400);
}

TEST(CharDelete, BR_CHAR_009_BlockedByGuild4OrParty5PerRestriction) {
	DeletionConfig cfg; // restriction 3 = party | guild
	auto c = character();
	c.guild_id = 10;
	EXPECT_EQ(request_deletion(c, true, cfg, kNow).result, DeleteRequestResult::InGuild);
	EXPECT_EQ(static_cast<int>(DeleteRequestResult::InGuild), 4);
	c.guild_id = 0;
	c.party_id = 7;
	EXPECT_EQ(request_deletion(c, true, cfg, kNow).result, DeleteRequestResult::InParty);
	EXPECT_EQ(static_cast<int>(DeleteRequestResult::InParty), 5);
	cfg.restriction = kDelRestrictNone;
	EXPECT_EQ(request_deletion(c, true, cfg, kNow).result, DeleteRequestResult::Queued);
}

TEST(CharDelete, BR_CHAR_009_AlreadyQueuedAndForeignCharacter) {
	DeletionConfig cfg;
	auto c = character();
	c.delete_date = kNow + 5;
	EXPECT_EQ(request_deletion(c, true, cfg, kNow).result, DeleteRequestResult::AlreadyQueued);
	EXPECT_EQ(request_deletion(character(), false, cfg, kNow).result, DeleteRequestResult::DatabaseOrNotFound);
}

// ---- BR-CHAR-010 / 011 -----------------------------------------------------------------------------
TEST(CharDelete, BR_CHAR_010_BirthdateCodeMustMatchYYMMDD) {
	EXPECT_TRUE(birthdate_code_matches("1990-04-21", "900421"));
	EXPECT_FALSE(birthdate_code_matches("1990-04-21", "900422"));
	EXPECT_FALSE(birthdate_code_matches("1990-04-21", ""));
	EXPECT_TRUE(birthdate_code_matches("", ""));  // legacy: unset birthdate + empty code
	EXPECT_FALSE(birthdate_code_matches("", "900421"));
}

TEST(CharDelete, BR_CHAR_011_LegacyDeleteByEmailOrBirthdatePerOption) {
	// Fake addresses only; no legacy account data.
	EXPECT_TRUE(delete_code_matches("Player@Example.test", "player@example.test", "1990-04-21", kDelOptionEmail, false));
	EXPECT_FALSE(delete_code_matches("other@example.test", "player@example.test", "1990-04-21", kDelOptionEmail, false));
	EXPECT_TRUE(delete_code_matches("", "a@a.com", "", kDelOptionEmail, false)); // default e-mail
	EXPECT_FALSE(delete_code_matches("player@example.test", "player@example.test", "1990-04-21", kDelOptionBirthdate, false));
	EXPECT_TRUE(delete_code_matches("900421", "player@example.test", "1990-04-21", kDelOptionBirthdate, true));
	EXPECT_TRUE(delete_code_matches("900421", "player@example.test", "1990-04-21", kDelOptionEmail | kDelOptionBirthdate, true));
}

// ---- BR-CHAR-012 -----------------------------------------------------------------------------------
TEST(CharDelete, BR_CHAR_012_GateOwnershipLevelGuildPartyAndDelay) {
	DeletionConfig cfg;
	auto c = character();
	c.delete_date = kNow - 1;
	EXPECT_EQ(check_delete_gate(c, false, cfg, kNow), DeleteGate::NotFound);
	EXPECT_EQ(check_delete_gate(c, true, cfg, kNow), DeleteGate::Ok);

	cfg.level_limit = 50; // >= 50 blocked
	EXPECT_EQ(check_delete_gate(c, true, cfg, kNow), DeleteGate::BaseLevel);
	cfg.level_limit = -50; // <= 50 blocked
	EXPECT_EQ(check_delete_gate(c, true, cfg, kNow), DeleteGate::BaseLevel);
	cfg.level_limit = 0;

	c.guild_id = 3;
	EXPECT_EQ(check_delete_gate(c, true, cfg, kNow), DeleteGate::Guild);
	c.guild_id = 0;
	c.party_id = 3;
	EXPECT_EQ(check_delete_gate(c, true, cfg, kNow), DeleteGate::Party);
	c.party_id = 0;

	c.delete_date = 0;
	EXPECT_EQ(check_delete_gate(c, true, cfg, kNow), DeleteGate::Time);
	c.delete_date = kNow + 1;
	EXPECT_EQ(check_delete_gate(c, true, cfg, kNow), DeleteGate::Time);
	cfg.delay_seconds = 0;
	EXPECT_EQ(check_delete_gate(c, true, cfg, kNow), DeleteGate::Ok);
}

// ---- BR-CHAR-013 -----------------------------------------------------------------------------------
TEST(CharDelete, BR_CHAR_013_CleanupCoversAllRelatedData) {
	auto c = character();
	c.partner_id = 150'002;
	c.father_id = 150'003;
	c.party_id = 5;
	c.homun_id = 9;
	c.elemental_id = 4;
	c.guild_id = 8;
	const auto plan = deletion_cleanup_plan(c, /*is_guild_master=*/false);
	const auto has = [&](CleanupStep s) { return std::find(plan.begin(), plan.end(), s) != plan.end(); };
	for (const auto s : {CleanupStep::Divorce, CleanupStep::DeAdoptFromParents, CleanupStep::LeaveParty,
	                     CleanupStep::DeletePets, CleanupStep::DeleteHomunculus, CleanupStep::DeleteElemental,
	                     CleanupStep::DeleteMercenaryOwner, CleanupStep::DeleteFriendsBothWays,
	                     CleanupStep::DeleteInventory, CleanupStep::DeleteCart, CleanupStep::DeleteRegistries,
	                     CleanupStep::DeleteSkills, CleanupStep::DeleteReceivedMail, CleanupStep::AnonymizeSentMail,
	                     CleanupStep::DeleteStatusChanges, CleanupStep::DeleteQuests,
	                     CleanupStep::DeleteAchievements, CleanupStep::DeleteCharacterRow, CleanupStep::LeaveGuild}) {
		EXPECT_TRUE(has(s)) << static_cast<int>(s);
	}
	EXPECT_FALSE(has(CleanupStep::BreakGuild));
	const auto master_plan = deletion_cleanup_plan(c, true);
	EXPECT_EQ(master_plan.back(), CleanupStep::BreakGuild);
}

TEST(CharDelete, BR_CHAR_013_UnlinkedCharacterSkipsConditionalSteps) {
	const auto plan = deletion_cleanup_plan(character(), false);
	const auto has = [&](CleanupStep s) { return std::find(plan.begin(), plan.end(), s) != plan.end(); };
	EXPECT_FALSE(has(CleanupStep::Divorce));
	EXPECT_FALSE(has(CleanupStep::LeaveParty));
	EXPECT_FALSE(has(CleanupStep::LeaveGuild));
	EXPECT_EQ(plan.back(), CleanupStep::DeleteCharacterRow);
}

TEST(CharDelete, BR_CHAR_013_CleanupOrderMatchesLegacyGoldenVectorAndM3bCascade) {
	GTEST_SKIP() << "PENDING BR-CHAR-013: golden vector of the executed SQL sequence from an instrumented legacy "
	                "build (architecture §3.2 step 1) and the M3b FK cascade-order check (§5)";
}

// ---- BR-CHAR-014 / 015 / 016 -----------------------------------------------------------------------
TEST(CharDelete, BR_CHAR_014_OwnerCancelsDeletionByZeroingDate) {
	auto c = character();
	c.delete_date = kNow + 100;
	EXPECT_EQ(cancel_deletion(c, true), UnixTime{0});
	EXPECT_FALSE(cancel_deletion(c, false).has_value());
}

TEST(CharDelete, BR_CHAR_015_QueuedCharacterCannotBeSelected) {
	auto c = character();
	EXPECT_EQ(check_selectable(c, kNow), SelectResult::Ok);
	c.delete_date = kNow + 100;
	EXPECT_EQ(check_selectable(c, kNow), SelectResult::QueuedForDeletion);
}

TEST(CharDelete, BR_CHAR_016_ActiveBanBlocksSelection) {
	auto c = character();
	c.unban_time = kNow + 60;
	EXPECT_EQ(check_selectable(c, kNow), SelectResult::Banned);
	c.unban_time = kNow - 1;
	EXPECT_EQ(check_selectable(c, kNow), SelectResult::Ok);
}

TEST(CharDelete, BR_CHAR_016_BanAdjustsFromMaxNowCurrentClampsAndKicks) {
	auto b = adjust_char_ban(0, 3600, kNow);
	EXPECT_TRUE(b.applied);
	EXPECT_EQ(b.unban_time, kNow + 3600);
	EXPECT_TRUE(b.kick);

	b = adjust_char_ban(kNow + 100, 50, kNow); // extends from current
	EXPECT_EQ(b.unban_time, kNow + 150);

	b = adjust_char_ban(kNow - 500, 100, kNow); // expired ban: from now
	EXPECT_EQ(b.unban_time, kNow + 100);

	b = adjust_char_ban(kNow + 100, -1000, kNow); // below now -> 0, no kick
	EXPECT_TRUE(b.applied);
	EXPECT_EQ(b.unban_time, 0);
	EXPECT_FALSE(b.kick);

	b = adjust_char_ban(0, -10, kNow); // reducing a non-banned char is ignored
	EXPECT_FALSE(b.applied);
}
