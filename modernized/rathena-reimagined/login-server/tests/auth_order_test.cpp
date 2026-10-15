// CAP-01 / CAP-03: authentication order and refusal codes.
#include <gtest/gtest.h>

#include "rathena/login/auth_rules.hpp"
#include "rathena/login/config.hpp"
#include "rathena/login/md5.hpp"
#include "test_support.hpp"

using namespace rathena::login;
using namespace rathena::login::test;

namespace {
AuthFacts ok_facts() {
  AuthFacts f;
  f.account = player();
  f.password_ok = true;
  f.now = kNow;
  return f;
}
}  // namespace

TEST(LoginAuth, BR_LOGIN_001_CorrectPasswordAndFutureUnbanGivesCode6) {
  auto f = ok_facts();
  f.account->unban_time = kNow + 3600;
  const auto d = decide_login(f);
  EXPECT_FALSE(d.accepted);
  EXPECT_EQ(d.code, 6u);
  EXPECT_EQ(d.unblock_time, kNow + 3600);
}

TEST(LoginAuth, BR_LOGIN_001_GateOrderIsFixed) {
  const auto order = auth_gate_order();
  const AuthGate expected[] = {AuthGate::Dnsbl,   AuthGate::AutoCreate, AuthGate::Unknown,
                               AuthGate::ServerAccount, AuthGate::Password, AuthGate::Expired,
                               AuthGate::Banned,  AuthGate::State,      AuthGate::ClientHash};
  ASSERT_EQ(order.size(), std::size(expected));
  for (std::size_t i = 0; i < order.size(); ++i) EXPECT_EQ(order[i], expected[i]) << i;
}

TEST(LoginAuth, BR_LOGIN_001_FirstFailureWins) {
  // Everything is wrong at once: DNSBL must win.
  AuthFacts f = ok_facts();
  f.dnsbl_listed = true;
  f.password_ok = false;
  f.account->expiration_time = kNow - 1;
  f.account->unban_time = kNow + 10;
  f.account->state = 5;
  f.client_hash_ok = false;
  EXPECT_EQ(decide_login(f).code, 3u);
  f.dnsbl_listed = false;
  EXPECT_EQ(decide_login(f).code, 1u);  // bad password before expired
  f.password_ok = true;
  EXPECT_EQ(decide_login(f).code, 2u);  // expired before banned
  f.account->expiration_time = 0;
  EXPECT_EQ(decide_login(f).code, 6u);  // banned before state
  f.account->unban_time = 0;
  EXPECT_EQ(decide_login(f).code, 4u);  // state before client hash
  f.account->state = 0;
  EXPECT_EQ(decide_login(f).code, 5u);
  f.client_hash_ok = true;
  EXPECT_TRUE(decide_login(f).accepted);
}

TEST(LoginAuth, BR_LOGIN_001_UnverifiedPasswordFailsClosed) {
  auto f = ok_facts();
  f.password_ok.reset();
  EXPECT_EQ(decide_login(f).code, 1u);
}

TEST(LoginAuth, BR_LOGIN_001_HandlerOrchestrationEndToEnd) {
  GTEST_SKIP() << "PENDING BR-LOGIN-001: LoginClientHandler orchestration (ipban -> dnsbl -> "
                  "repository lookup -> hash pool -> decide_login -> AC_ACCEPT_LOGIN) not wired yet";
}

TEST(LoginAuth, BR_LOGIN_002_PlaintextCompareIsCaseSensitive) {
  EXPECT_TRUE(plaintext_password_matches("secret1", "secret1"));
  EXPECT_FALSE(plaintext_password_matches("secret1", "Secret1"));
  EXPECT_FALSE(plaintext_password_matches("secret1", "secret"));
}

TEST(LoginAuth, BR_LOGIN_003_ChallengeShimIsOffByDefault) {
  LoginConfig cfg;  // shipped defaults
  EXPECT_FALSE(cfg.md5_challenge_shim);
  const auto r = verify_md5_challenge(cfg, "salt", std::string("chalsecret"), md5("saltchalsecret"));
  EXPECT_FALSE(r.accepted);
  EXPECT_EQ(r.refuse_code, 3u);
}

TEST(LoginAuth, BR_LOGIN_003_ChallengeAcceptsSaltPassOrPassSaltWhenEnabled) {
  LoginConfig cfg;
  cfg.md5_challenge_shim = true;
  const std::string salt = "\x05\x10xyz";
  const std::optional<std::string> secret = "fakechal1";
  EXPECT_TRUE(verify_md5_challenge(cfg, salt, secret, md5(salt + *secret)).accepted);
  EXPECT_TRUE(verify_md5_challenge(cfg, salt, secret, md5(*secret + salt)).accepted);
  const auto bad = verify_md5_challenge(cfg, salt, secret, md5("nope"));
  EXPECT_FALSE(bad.accepted);
  EXPECT_EQ(bad.refuse_code, 1u);
  EXPECT_FALSE(verify_md5_challenge(cfg, salt, std::nullopt, md5(salt)).accepted);
}

TEST(LoginAuth, BR_LOGIN_003_Md5MatchesRfc1321Vectors) {
  EXPECT_EQ(to_hex(md5("")), "d41d8cd98f00b204e9800998ecf8427e");
  EXPECT_EQ(to_hex(md5("abc")), "900150983cd24fb0d6963f7d28e17f72");
  EXPECT_EQ(to_hex(md5("The quick brown fox jumps over the lazy dog")),
            "9e107d9d372bb6826bd81d3542a419d6");
}

TEST(LoginAuth, BR_LOGIN_004_Removed_UseMd5PasswordsKeyIsRejected) {
  // HITL #1: use_MD5_passwords is removed. The key must not be silently accepted.
  LoginConfig cfg;
  EXPECT_EQ(apply_config_key(cfg, "use_MD5_passwords", "yes"), ConfigKeyStatus::Removed);
  EXPECT_EQ(apply_config_key(cfg, "use_md5_passwords", "no"), ConfigKeyStatus::Removed);
}

TEST(LoginAuth, BR_LOGIN_005_ChallengeSaltNeverContainsNul) {
  ScriptedRng edge{0u, 254u, 255u, 509u, 510u, 0xFFFFFFFFu};
  const auto s = generate_challenge_salt(edge, 6);
  ASSERT_EQ(s.size(), 6u);
  for (char c : s) {
    const auto b = static_cast<unsigned char>(c);
    EXPECT_GE(b, 1);
    EXPECT_LE(b, 255);
  }
  SeededRng rng(42);
  for (int i = 0; i < 200; ++i)
    for (char c : generate_challenge_salt(rng)) EXPECT_NE(c, '\0');
}

TEST(LoginAuth, BR_LOGIN_009_ServerAccountFromGameClientGivesCode0) {
  auto f = ok_facts();
  f.account->sex = Sex::Server;
  EXPECT_EQ(decide_login(f).code, 0u);
}

TEST(LoginAuth, BR_LOGIN_010_ExpiredAccountGivesCode2AndZeroNeverExpires) {
  auto f = ok_facts();
  f.account->expiration_time = kNow - 1;
  EXPECT_EQ(decide_login(f).code, 2u);
  f.account->expiration_time = 0;
  EXPECT_TRUE(decide_login(f).accepted);
  f.account->expiration_time = kNow + 100;
  EXPECT_TRUE(decide_login(f).accepted);
}

TEST(LoginAuth, BR_LOGIN_011_FutureUnbanGivesCode6WithUnblockDate) {
  auto f = ok_facts();
  f.account->unban_time = kNow + 1;
  const auto d = decide_login(f);
  EXPECT_EQ(d.code, 6u);
  ASSERT_TRUE(d.unblock_time.has_value());
  f.account->unban_time = kNow - 1;  // stale ban no longer blocks
  EXPECT_TRUE(decide_login(f).accepted);
}

TEST(LoginAuth, BR_LOGIN_012_NonZeroStateGivesStateMinusOne) {
  auto f = ok_facts();
  f.account->state = 5;
  EXPECT_EQ(decide_login(f).code, 4u);
  f.account->state = 1;
  EXPECT_EQ(decide_login(f).code, 0u);
  EXPECT_FALSE(decide_login(f).accepted);
}

TEST(LoginAuth, BR_LOGIN_013_ClientHashCheck) {
  LoginConfig cfg;
  cfg.client_hash_check = true;
  const auto good = md5("fake-client-exe");
  cfg.client_hash_nodes = {{10, std::nullopt}, {0, good}};
  EXPECT_EQ(client_hash_ok(cfg, 0, good), true);
  EXPECT_EQ(client_hash_ok(cfg, 0, md5("other")), false);
  EXPECT_EQ(client_hash_ok(cfg, 0, std::nullopt), false);
  EXPECT_EQ(client_hash_ok(cfg, 10, md5("other")), true);  // "disabled" node for group <= 10
  cfg.client_hash_nodes.clear();
  EXPECT_EQ(client_hash_ok(cfg, 99, good), false);
  cfg.client_hash_check = false;
  EXPECT_EQ(client_hash_ok(cfg, 0, std::nullopt), std::nullopt);

  auto f = ok_facts();
  f.client_hash_ok = false;
  EXPECT_EQ(decide_login(f).code, 5u);
}

namespace {
class FakeResolver final : public DnsResolver {
 public:
  explicit FakeResolver(std::string listed) : listed_(std::move(listed)) {}
  bool resolves(const std::string& h) override {
    queries.push_back(h);
    return h == listed_;
  }
  std::vector<std::string> queries;

 private:
  std::string listed_;
};
}  // namespace

TEST(LoginAuth, BR_LOGIN_014_AnyDnsblResolutionGivesCode3) {
  LoginConfig cfg;
  cfg.use_dnsbl = true;
  cfg.dnsbl_servers = {"bl1.example.invalid", "bl2.example.invalid"};
  FakeResolver r("4.3.2.1.bl2.example.invalid");
  EXPECT_TRUE(dnsbl_listed(cfg, make_ipv4(1, 2, 3, 4), r));
  EXPECT_EQ(r.queries.front(), "4.3.2.1.bl1.example.invalid");
  FakeResolver none("nothing");
  EXPECT_FALSE(dnsbl_listed(cfg, make_ipv4(1, 2, 3, 4), none));
  cfg.use_dnsbl = false;
  EXPECT_FALSE(dnsbl_listed(cfg, make_ipv4(1, 2, 3, 4), r));

  auto f = ok_facts();
  f.dnsbl_listed = true;
  EXPECT_EQ(decide_login(f).code, 3u);
}

TEST(LoginAuth, BR_LOGIN_015_SuccessUpdatesAccount) {
  Account a = player();
  a.unban_time = kNow - 50;  // stale
  a.logincount = 7;
  ScriptedRng rng{0x11111111u, 0x22222222u};
  const auto u = apply_login_success(a, kNow, make_ipv4(10, 0, 0, 9), "2025-10-09 10:00:00",
                                     "AbCdEfGhIjKlMnOp", rng);
  EXPECT_EQ(u.login_id1, 0x11111111u);
  EXPECT_EQ(u.login_id2, 0x22222222u);
  EXPECT_EQ(a.unban_time, 0);
  EXPECT_EQ(a.logincount, 8u);
  EXPECT_EQ(a.last_ip, "10.0.0.9");
  EXPECT_EQ(a.lastlogin, "2025-10-09 10:00:00");
  EXPECT_EQ(a.web_auth_token, "AbCdEfGhIjKlMnOp");
  EXPECT_TRUE(a.web_auth_token_enabled);
}

TEST(LoginAuth, BR_LOGIN_016_GroupGatingGivesServerClosed) {
  LoginConfig cfg;
  EXPECT_EQ(group_gate(cfg, 0), std::nullopt);
  cfg.group_id_to_connect = 99;
  EXPECT_EQ(group_gate(cfg, 0), NotifyBan::ServerClosed);
  EXPECT_EQ(group_gate(cfg, 99), std::nullopt);
  cfg.group_id_to_connect = -1;
  cfg.min_group_id_to_connect = 10;
  EXPECT_EQ(group_gate(cfg, 9), NotifyBan::ServerClosed);
  EXPECT_EQ(group_gate(cfg, 10), std::nullopt);
  EXPECT_EQ(static_cast<int>(NotifyBan::ServerClosed), 1);
}

TEST(LoginAuth, BR_LOGIN_017_NoCharServerGivesCode1) {
  EXPECT_EQ(char_server_gate(0), NotifyBan::ServerClosed);
  EXPECT_EQ(char_server_gate(1), std::nullopt);
}
