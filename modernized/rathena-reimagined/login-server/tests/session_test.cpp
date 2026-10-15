// CAP-04: session handoff, duplicate login, online state.
#include <gtest/gtest.h>

#include <vector>

#include "rathena/login/online_state.hpp"
#include "rathena/login/ticket_store.hpp"
#include "test_support.hpp"

using namespace rathena::login;
using namespace rathena::login::test;

TEST(LoginSession, BR_LOGIN_018_DuplicateLoginKicksAndNotifiesCode8) {
  OnlineRegistry reg;
  reg.mark_logged_in(2'000'001);
  reg.set_char_server(2'000'001, 0);
  const auto act = reg.check_duplicate(2'000'001, kNow);
  ASSERT_TRUE(act);
  EXPECT_TRUE(act->kick_broadcast);
  EXPECT_EQ(static_cast<int>(act->client_notify), 8);
  EXPECT_EQ(act->force_clear_at, kNow + 30);
  EXPECT_EQ(reg.cleanup(kNow + 29), 0u);
  EXPECT_EQ(reg.cleanup(kNow + 30), 1u);  // force-cleared after 30 s
  EXPECT_FALSE(reg.get(2'000'001));
}

TEST(LoginSession, BR_LOGIN_018_NotOnlineIsNotDuplicate) {
  OnlineRegistry reg;
  EXPECT_FALSE(reg.check_duplicate(2'000'001, kNow));
}

TEST(LoginSession, BR_LOGIN_019_TicketSingleUseWithin30s) {
  LoginTicketStore store;
  store.issue({2'000'001, 111, 222, Sex::Male, 0, kNow});
  EXPECT_EQ(store.verify(2'000'001, 111, 222, Sex::Male, kNow + 30), TicketVerdict::Accepted);
  EXPECT_EQ(store.verify(2'000'001, 111, 222, Sex::Male, kNow + 30), TicketVerdict::Unknown);
}

TEST(LoginSession, BR_LOGIN_019_WrongId2IsRefusedAndConsumed) {
  LoginTicketStore store;
  store.issue({2'000'001, 111, 222, Sex::Male, 0, kNow});
  EXPECT_EQ(store.verify(2'000'001, 111, 223, Sex::Male, kNow), TicketVerdict::Mismatch);
  EXPECT_EQ(store.verify(2'000'001, 111, 222, Sex::Male, kNow), TicketVerdict::Unknown);
}

TEST(LoginSession, BR_LOGIN_019_TicketExpiresAfter30s) {
  LoginTicketStore store;
  store.issue({2'000'001, 1, 2, Sex::Female, 0, kNow});
  EXPECT_EQ(store.verify(2'000'001, 1, 2, Sex::Female, kNow + 31), TicketVerdict::Expired);
  store.issue({2'000'002, 1, 2, Sex::Female, 0, kNow});
  EXPECT_EQ(store.purge_expired(kNow + 31), 1u);
  EXPECT_EQ(store.size(), 0u);
}

TEST(LoginSession, BR_LOGIN_033_OnlineStateTransitions) {
  OnlineRegistry reg;
  reg.mark_logged_in(1'000'000 + 2'000'000);
  EXPECT_EQ(reg.get(3'000'000)->char_server, -1);
  reg.set_char_server(3'000'000, 2);
  EXPECT_EQ(reg.get(3'000'000)->char_server, 2);
  reg.on_char_server_lost(2);
  EXPECT_EQ(reg.get(3'000'000)->char_server, -2);
  EXPECT_EQ(reg.cleanup(kNow), 1u);
  EXPECT_EQ(kOnlineCleanupInterval, 600);
}

TEST(LoginInter, BR_INTER_004_FullSyncMarksServerUsersLostFirst) {
  OnlineRegistry reg;
  reg.set_char_server(2'000'001, 0);
  reg.set_char_server(2'000'002, 0);
  reg.set_char_server(2'000'003, 1);  // other server untouched
  const std::vector<AccountId> online = {2'000'002, 2'000'004};
  reg.apply_full_sync(0, online);
  EXPECT_EQ(reg.get(2'000'001)->char_server, -2);
  EXPECT_EQ(reg.get(2'000'002)->char_server, 0);
  EXPECT_EQ(reg.get(2'000'004)->char_server, 0);
  EXPECT_EQ(reg.get(2'000'003)->char_server, 1);
}
