// CAP-02: account administration.
#include <gtest/gtest.h>

#include "rathena/login/account_admin.hpp"
#include "test_support.hpp"

using namespace rathena::login;
using namespace rathena::login::test;

TEST(LoginAdmin, BR_LOGIN_026_BanExtendsFromMaxOfNowAndCurrent) {
  Account a = player();
  EXPECT_TRUE(apply_ban(a, kNow, 100).changed);
  EXPECT_EQ(a.unban_time, kNow + 100);
  apply_ban(a, kNow, 50);  // extends from the current unban
  EXPECT_EQ(a.unban_time, kNow + 150);
  a.unban_time = kNow - 1000;  // stale ban: extends from now
  apply_ban(a, kNow, 10);
  EXPECT_EQ(a.unban_time, kNow + 10);
  const auto ignored = apply_ban(a, kNow, -kNow);  // result <= now
  EXPECT_FALSE(ignored.changed);
  EXPECT_FALSE(ignored.broadcast);
  EXPECT_EQ(a.unban_time, kNow + 10);
}

TEST(LoginAdmin, BR_LOGIN_027_UnbanSetsZero) {
  Account a = player();
  a.unban_time = kNow + 999;
  EXPECT_TRUE(apply_unban(a).changed);
  EXPECT_EQ(a.unban_time, 0);
}

TEST(LoginAdmin, BR_LOGIN_028_StateAppliedIfDifferentBroadcastIfNonZero) {
  Account a = player();
  EXPECT_FALSE(apply_state(a, 0).changed);
  auto r = apply_state(a, 5);
  EXPECT_TRUE(r.changed);
  EXPECT_EQ(r.broadcast, InterBroadcast::AccountStatus);
  r = apply_state(a, 0);
  EXPECT_TRUE(r.changed);
  EXPECT_FALSE(r.broadcast);
}

TEST(LoginAdmin, BR_LOGIN_029_SexTogglesExceptServer) {
  Account a = player();
  EXPECT_EQ(toggle_sex(a).broadcast, InterBroadcast::SexChanged);
  EXPECT_EQ(a.sex, Sex::Female);
  toggle_sex(a);
  EXPECT_EQ(a.sex, Sex::Male);
  a.sex = Sex::Server;
  EXPECT_FALSE(toggle_sex(a).changed);
  EXPECT_EQ(a.sex, Sex::Server);
}

TEST(LoginAdmin, BR_LOGIN_030_EmailChangeRules) {
  Account a = player();
  a.email = "Old@Example.org";
  EXPECT_FALSE(change_email(a, "old@example.org", "a@a.com").changed);    // default address
  EXPECT_FALSE(change_email(a, "wrong@example.org", "new@example.org").changed);
  EXPECT_FALSE(change_email(a, "old@example.org", "not-an-email").changed);
  EXPECT_TRUE(change_email(a, "old@example.org", "new@example.org").changed);  // case-insensitive
  EXPECT_EQ(a.email, "new@example.org");
  EXPECT_FALSE(is_valid_email("@x.org"));
  EXPECT_FALSE(is_valid_email("a@b"));
  EXPECT_FALSE(is_valid_email("a..b@c.org"));
}

TEST(LoginAdmin, BR_LOGIN_031_CharSlots) {
  LoginConfig cfg;  // MAX_CHARS 15, vip_char_increase 6
  Account a = player();
  EXPECT_EQ(effective_char_slots(cfg, a, false), 9);
  EXPECT_EQ(effective_char_slots(cfg, a, true), 15);
  a.character_slots = 3;
  EXPECT_EQ(effective_char_slots(cfg, a, false), 3);
  a.character_slots = 0;
  cfg.chars_per_account = 7;
  EXPECT_EQ(effective_char_slots(cfg, a, false), 7);
}

TEST(LoginAdmin, BR_LOGIN_032_VipGroupLifecycle) {
  LoginConfig cfg;
  Account a = player();
  a.group_id = 1;
  auto v = apply_vip(cfg, a, kNow, 3600);
  EXPECT_TRUE(v.active);
  EXPECT_EQ(a.group_id, 5);
  EXPECT_EQ(a.old_group, 1);
  EXPECT_EQ(v.char_slots, 15);
  v = apply_vip(cfg, a, kNow + 3601);  // expired
  EXPECT_FALSE(v.active);
  EXPECT_EQ(a.group_id, 1);
  EXPECT_EQ(v.char_slots, 9);

  Account gm = player();
  gm.group_id = 99;
  apply_vip(cfg, gm, kNow, 3600);
  EXPECT_EQ(gm.group_id, 99);  // group > 5 untouched
}

TEST(LoginInter, BR_INTER_017_VipInfoComputedForCharServer) {
  LoginConfig cfg;
  Account a = player();
  const auto v = apply_vip(cfg, a, kNow, 60);
  EXPECT_TRUE(v.active);
  EXPECT_EQ(v.vip_time, kNow + 60);
  EXPECT_EQ(v.group_id, cfg.vip_group);
  EXPECT_TRUE(v.group_changed);
}

TEST(LoginInter, BR_INTER_017_VipRelayedLoginToCharToMap) {
  GTEST_SKIP() << "PENDING BR-INTER-017: 0x2742/0x2743 wire relay (netproto codec + char/map "
                  "propagation) not wired in login-server yet";
}

TEST(LoginAdmin, BR_LOGIN_035_PinUpdateStoresPinAndTime) {
  Account a = player();
  EXPECT_TRUE(apply_pin_update(a, "1357", kNow).stored);
  EXPECT_EQ(a.pincode, "1357");
  EXPECT_EQ(a.pincode_change, kNow);
  EXPECT_TRUE(apply_pin_failure_limit_reached().force_offline);
}

TEST(LoginAdmin, BR_LOGIN_036_DoubleHashRegistryIsLoginOwned) {
  EXPECT_TRUE(is_login_registry_key("##CASHPOINTS"));
  EXPECT_FALSE(is_login_registry_key("#KAFRAPOINTS"));
  EXPECT_FALSE(is_login_registry_key("plain"));
  EXPECT_FALSE(is_login_registry_key("##"));
}

TEST(LoginAdmin, BR_LOGIN_036_RegistryRelayedToAllCharServers) {
  GTEST_SKIP() << "PENDING BR-LOGIN-036: global_acc_reg persistence and 0x2726 relay to all "
                  "char servers not implemented yet";
}

TEST(LoginAdmin, BR_LOGIN_037_AccountDeletionIsAtomic) {
  GTEST_SKIP() << "PENDING BR-LOGIN-037: AccountRepository::remove_atomically (row + ## regs in "
                  "one transaction) needs the persistence MySQL adapter";
}
