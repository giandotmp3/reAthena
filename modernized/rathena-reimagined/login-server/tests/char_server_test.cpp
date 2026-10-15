// Char-server registration, routing and broadcast targets.
#include <gtest/gtest.h>

#include "rathena/login/account_admin.hpp"
#include "rathena/login/char_server_registry.hpp"
#include "test_support.hpp"

using namespace rathena::login;
using namespace rathena::login::test;

namespace {
Account server_account(AccountId id) {
  Account a;
  a.account_id = id;
  a.userid = kFakeInterUser;
  a.sex = Sex::Server;
  return a;
}
CharServerInfo info(const char* name) {
  CharServerInfo i;
  i.name = name;
  i.ip = make_ipv4(192, 0, 2, 10);
  i.port = 6121;
  return i;
}
}  // namespace

TEST(LoginCharServer, BR_LOGIN_025_RegistrationRequiresEnvCredentialsServerSexAndFreeSlot) {
  CharServerRegistry reg;
  const auto creds = fake_inter_credentials();
  EXPECT_EQ(reg.try_register(creds, kFakeInterUser, "wrong-pass-00000000", server_account(1), info("a")),
            RegistrationResult::BadCredentials);
  Account notS = server_account(1);
  notS.sex = Sex::Male;
  EXPECT_EQ(reg.try_register(creds, kFakeInterUser, kFakeInterPass, notS, info("a")),
            RegistrationResult::NotServerAccount);
  EXPECT_EQ(reg.try_register(creds, kFakeInterUser, kFakeInterPass, server_account(5), info("a")),
            RegistrationResult::IdOutOfRange);
  EXPECT_EQ(reg.try_register(creds, kFakeInterUser, kFakeInterPass, server_account(1), info("a")),
            RegistrationResult::Accepted);
  EXPECT_EQ(reg.try_register(creds, kFakeInterUser, kFakeInterPass, server_account(1), info("b")),
            RegistrationResult::SlotTaken);
}

TEST(LoginCharServer, BR_LOGIN_025_EmptyExpectedCredentialsNeverMatch) {
  CharServerRegistry reg;
  EXPECT_EQ(reg.try_register({"", ""}, "", "", server_account(0), info("a")),
            RegistrationResult::BadCredentials);
}

TEST(LoginCharServer, BR_LOGIN_025_CredentialsLoadedFromEnvironmentOnly) {
  const auto none = load_inter_server_credentials([](const char*) -> const char* { return nullptr; });
  EXPECT_FALSE(none);
  const auto empty = load_inter_server_credentials([](const char*) -> const char* { return ""; });
  EXPECT_FALSE(empty);
  const auto ok = load_inter_server_credentials([](const char* k) -> const char* {
    return std::string_view(k) == "INTER_SERVER_USER" ? kFakeInterUser : kFakeInterPass;
  });
  ASSERT_TRUE(ok);
  EXPECT_EQ(ok->user, kFakeInterUser);
}

TEST(LoginCharServer, BR_LOGIN_020_LanClientGetsLanCharIp) {
  LoginConfig cfg;
  const Ipv4 lan_char = make_ipv4(10, 0, 0, 5);
  const Ipv4 pub = make_ipv4(198, 51, 100, 5);
  cfg.lan_subnets = {{make_ipv4(255, 255, 255, 0), lan_char, lan_char}};
  EXPECT_EQ(select_char_ip(cfg, make_ipv4(10, 0, 0, 77), pub), lan_char);
  EXPECT_EQ(select_char_ip(cfg, make_ipv4(10, 0, 1, 77), pub), pub);
}

TEST(LoginCharServer, BR_LOGIN_021_PopulationColour) {
  LoginConfig cfg;
  EXPECT_EQ(population_display(cfg, 200), 0);
  EXPECT_EQ(population_display(cfg, 201), 1);
  EXPECT_EQ(population_display(cfg, 500), 1);
  EXPECT_EQ(population_display(cfg, 1000), 2);
  EXPECT_EQ(population_display(cfg, 1001), 3);
  cfg.usercount_disable = true;
  EXPECT_EQ(population_display(cfg, 5), 4);
}

TEST(LoginInter, BR_INTER_005_BanAndStateChangesBroadcastToAllCharServers) {
  CharServerRegistry reg;
  const auto creds = fake_inter_credentials();
  for (AccountId id : {0u, 2u, 4u})
    ASSERT_EQ(reg.try_register(creds, kFakeInterUser, kFakeInterPass, server_account(id), info("s")),
              RegistrationResult::Accepted);
  Account a = player();
  const auto ban = apply_ban(a, kNow, 3600);
  ASSERT_EQ(ban.broadcast, InterBroadcast::AccountStatus);
  EXPECT_EQ(static_cast<int>(*ban.broadcast), 0x2731);
  EXPECT_EQ(reg.connected_slots(), (std::vector<int>{0, 2, 4}));
  EXPECT_EQ(apply_state(a, 5).broadcast, InterBroadcast::AccountStatus);
}

TEST(LoginInter, BR_INTER_006_IpResyncUpdatesRegisteredServer) {
  CharServerRegistry reg;
  ASSERT_EQ(reg.try_register(fake_inter_credentials(), kFakeInterUser, kFakeInterPass, server_account(3),
                             info("s")),
            RegistrationResult::Accepted);
  EXPECT_TRUE(reg.update_ip(3, make_ipv4(192, 0, 2, 99)));
  EXPECT_EQ(reg.slot(3)->ip, make_ipv4(192, 0, 2, 99));
  EXPECT_FALSE(reg.update_ip(1, make_ipv4(192, 0, 2, 99)));
}

TEST(LoginInter, BR_INTER_006_PeriodicResyncTimer) {
  GTEST_SKIP() << "PENDING BR-INTER-006: periodic 0x2735 request on the Asio timer "
                  "(ip_sync_interval) not implemented yet";
}

TEST(LoginInter, BR_INTER_014_AtMostFiveCharServers) {
  CharServerRegistry reg;
  const auto creds = fake_inter_credentials();
  for (AccountId id = 0; id < 5; ++id)
    EXPECT_EQ(reg.try_register(creds, kFakeInterUser, kFakeInterPass, server_account(id), info("s")),
              RegistrationResult::Accepted);
  EXPECT_EQ(reg.connected_count(), 5);
  EXPECT_EQ(reg.try_register(creds, kFakeInterUser, kFakeInterPass, server_account(5), info("s")),
            RegistrationResult::IdOutOfRange);
}
