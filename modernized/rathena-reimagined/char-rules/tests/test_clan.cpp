// Acceptance tests: clans (CAP-10, P2). BR-CHAR-051.
#include <gtest/gtest.h>

#include "rathena/char_rules/clan.hpp"

using namespace rathena::char_rules;

namespace {
constexpr UnixTime kNow = 1'760'000'000;
constexpr UnixTime kDay = 86'400;

CharacterSummary member(CharId cid, UnixTime last_login, bool online = false) {
	CharacterSummary c;
	c.char_id = cid;
	c.clan_id = 3;
	c.last_login = last_login;
	c.online = online;
	return c;
}
} // namespace

TEST(Clan, BR_CHAR_051_MembershipClearedAfter14DaysOffline) {
	ClanConfig cfg; // shipped 14
	EXPECT_TRUE(clan_membership_expired(member(1, kNow - 14 * kDay), cfg, kNow));
	EXPECT_FALSE(clan_membership_expired(member(1, kNow - 14 * kDay + 1), cfg, kNow));
	EXPECT_FALSE(clan_membership_expired(member(1, kNow - 30 * kDay, /*online=*/true), cfg, kNow));
	EXPECT_FALSE(clan_membership_expired(member(1, 0), cfg, kNow)); // never logged in (NULL)
}

TEST(Clan, BR_CHAR_051_DisabledWhenDaysNotPositive) {
	ClanConfig cfg;
	cfg.remove_inactive_days = 0;
	EXPECT_FALSE(clan_membership_expired(member(1, kNow - 100 * kDay), cfg, kNow));
}

TEST(Clan, BR_CHAR_051_CleanupSelectsOnlyExpiredMembers) {
	const auto ids = clan_cleanup({member(1, kNow - 20 * kDay), member(2, kNow - kDay), member(3, kNow - 15 * kDay)},
	                              ClanConfig{}, kNow);
	EXPECT_EQ(ids, (std::vector<CharId>{1, 3}));
}
