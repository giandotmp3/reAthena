// Acceptance tests: parties (CAP-08, char side). BR-CHAR-033..036.
#include <gtest/gtest.h>

#include "rathena/char_rules/party.hpp"

using namespace rathena::char_rules;

namespace {
Party party_with_levels(std::initializer_list<std::pair<int, bool>> lv_online) {
	Party p;
	p.party_id = 1;
	CharId cid = 150'000;
	for (const auto& [lv, online] : lv_online) {
		++cid;
		p.members.push_back({cid + 1'850'000u, cid, "m" + std::to_string(cid), lv, online, p.members.empty()});
	}
	return p;
}
} // namespace

// ---- BR-CHAR-033 -----------------------------------------------------------------------------------
TEST(Party, BR_CHAR_033_UniqueNameAndLetterPolicy) {
	NameConfig cfg;
	EXPECT_EQ(check_party_create("Hunters", cfg, nullptr).result, PartyCreateResult::Ok);
	EXPECT_EQ(check_party_create("Hunters", cfg, [](std::string_view n) { return n == "Hunters"; }).result,
	          PartyCreateResult::NameTaken);
	EXPECT_EQ(check_party_create("Hunt!ers", cfg, nullptr).result, PartyCreateResult::InvalidName);
	EXPECT_EQ(check_party_create("", cfg, nullptr).result, PartyCreateResult::InvalidName);
	cfg.policy = NameLetterPolicy::Forbid;
	cfg.letters = "!";
	EXPECT_EQ(check_party_create("Hunt!ers", cfg, nullptr).result, PartyCreateResult::InvalidName);
}

TEST(Party, BR_CHAR_033_ClientQuoteIsNormalizedUnderAllowList) {
	const auto out = check_party_create("\"Hunters\"\"Guild\"", NameConfig{}, nullptr);
	EXPECT_EQ(out.result, PartyCreateResult::Ok);
	EXPECT_EQ(out.name, "Hunters Guild");
}

TEST(Party, BR_CHAR_033_MaxTwelveMembers) {
	Party p;
	for (int i = 0; i < 11; ++i) p.members.push_back({1, static_cast<CharId>(i + 1), "x", 10, true, i == 0});
	EXPECT_TRUE(party_can_add_member(p));
	p.members.push_back({1, 99, "x", 10, true, false});
	EXPECT_FALSE(party_can_add_member(p));
	EXPECT_EQ(kMaxParty, 12u);
}

// ---- BR-CHAR-034 -----------------------------------------------------------------------------------
TEST(Party, BR_CHAR_034_WorkedExampleLevels90And106OffAnd90And105On) {
	const int share = PartyGuildConfig{}.party_share_level; // shipped 15
	EXPECT_FALSE(party_even_share_allowed(party_with_levels({{90, true}, {106, true}}), share, {}));
	EXPECT_TRUE(party_even_share_allowed(party_with_levels({{90, true}, {105, true}}), share, {}));
}

TEST(Party, BR_CHAR_034_FewerThanTwoOnlineOrFamilyException) {
	EXPECT_TRUE(party_even_share_allowed(party_with_levels({{10, true}, {99, false}}), 15, {}));
	EXPECT_TRUE(party_even_share_allowed(party_with_levels({{10, true}, {99, true}}), 15, FamilyInfo{true}));
	EXPECT_FALSE(party_even_share_allowed(party_with_levels({{10, true}, {99, true}}), 15, FamilyInfo{false}));
}

TEST(Party, BR_CHAR_034_FamilyExceptionDerivedFromCharRelations) {
	GTEST_SKIP() << "PENDING BR-CHAR-034: char_family()/char_child() relation lookup (parent+child pair or "
	                "3-member family) supplied by CharacterRepository; golden vectors from int_party.cpp:84-97";
}

// ---- BR-CHAR-035 / 036 -----------------------------------------------------------------------------
TEST(Party, BR_CHAR_035_LeaderLeavingDissolvesParty) {
	auto p = party_with_levels({{50, true}, {51, true}, {52, true}});
	const CharId leader = p.members.front().char_id;
	EXPECT_EQ(party_leave(p, leader), LeaveOutcome::PartyDissolved);
	EXPECT_TRUE(p.members.empty());
}

TEST(Party, BR_CHAR_035_MemberLeavingKeepsParty) {
	auto p = party_with_levels({{50, true}, {51, true}});
	EXPECT_EQ(party_leave(p, p.members.back().char_id), LeaveOutcome::MemberRemoved);
	EXPECT_EQ(p.members.size(), 1u);
	EXPECT_EQ(party_leave(p, 1), LeaveOutcome::NotMember);
}

TEST(Party, BR_CHAR_036_PartyWithoutMembersIsOrphaned) {
	Party p;
	p.party_id = 9;
	EXPECT_TRUE(party_is_orphaned(p));
	p.members.push_back({2'000'001, 150'001, "x", 10, false, true});
	EXPECT_FALSE(party_is_orphaned(p));
}
