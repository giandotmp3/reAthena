// CAP-01: auto-registration and userid resolution.
#include <gtest/gtest.h>

#include "rathena/login/auth_rules.hpp"
#include "test_support.hpp"

using namespace rathena::login;
using namespace rathena::login::test;

TEST(LoginRegistration, BR_LOGIN_006_SuffixCreatesAccountWithSex) {
  LoginConfig cfg;
  EXPECT_FALSE(cfg.new_account);  // shipped default (spec §5.6)
  EXPECT_EQ(parse_autocreate(cfg, "bob123_M"), std::nullopt);
  cfg.new_account = true;
  const auto req = parse_autocreate(cfg, "bob123_M");
  ASSERT_TRUE(req);
  EXPECT_EQ(req->base_userid, "bob123");
  EXPECT_EQ(req->sex, Sex::Male);
  EXPECT_EQ(parse_autocreate(cfg, "amy999_f")->sex, Sex::Female);
  EXPECT_EQ(parse_autocreate(cfg, "bob123"), std::nullopt);
  EXPECT_EQ(parse_autocreate(cfg, "bob123_X"), std::nullopt);
  EXPECT_EQ(autocreate_refusal(cfg, *req, "pass12", false, false, false), std::nullopt);
}

TEST(LoginRegistration, BR_LOGIN_006_RefusalCodes) {
  LoginConfig cfg;
  cfg.new_account = true;
  const AutoCreateRequest req{"bob123", Sex::Male};
  EXPECT_EQ(autocreate_refusal(cfg, req, "pass12", /*encrypted=*/true, false, false), 0u);
  EXPECT_EQ(autocreate_refusal(cfg, req, "abc", false, false, false), 1u);         // too short
  EXPECT_EQ(autocreate_refusal(cfg, {"bob", Sex::Male}, "pass12", false, false, false), 1u);
  EXPECT_EQ(autocreate_refusal(cfg, req, "pass12", false, /*exists=*/true, false), 1u);
  EXPECT_EQ(autocreate_refusal(cfg, req, "pass12", false, false, /*flood=*/true), 3u);

  AuthFacts f;
  f.autocreate_refusal = 3u;
  f.now = kNow;
  EXPECT_EQ(decide_login(f).code, 3u);
}

TEST(LoginRegistration, BR_LOGIN_007_ServerWideFloodLimit) {
  RegistrationFloodLimiter lim(/*allowed_regs=*/2, /*time_allowed=*/10);
  EXPECT_TRUE(lim.allowed(kNow));
  lim.record(kNow);
  EXPECT_TRUE(lim.allowed(kNow + 1));
  lim.record(kNow + 1);
  EXPECT_FALSE(lim.allowed(kNow + 2));  // limit reached inside the window
  EXPECT_FALSE(lim.allowed(kNow + 9));
  EXPECT_TRUE(lim.allowed(kNow + 10));  // window over
  lim.record(kNow + 11);                // new window opens
  EXPECT_TRUE(lim.allowed(kNow + 12));
}

TEST(LoginRegistration, BR_LOGIN_008_NewAccountIdIsMaxPlusOneAtLeastStart) {
  EXPECT_EQ(allocate_account_id(std::nullopt), 2'000'000u);
  EXPECT_EQ(allocate_account_id(4u), 2'000'000u);
  EXPECT_EQ(allocate_account_id(2'000'005u), 2'000'006u);
  EXPECT_EQ(allocate_account_id(100'000'000u), std::nullopt);
  EXPECT_FALSE(is_valid_account_id(0));
  EXPECT_FALSE(is_valid_account_id(100'000'001u));
  EXPECT_TRUE(is_valid_account_id(2'000'000u));
}

TEST(LoginRegistration, BR_LOGIN_038_UseridLookupCaseSensitivityAndAmbiguity) {
  LoginConfig cfg;
  ASSERT_TRUE(cfg.case_sensitive);
  const std::vector<Account> rows = {player(2'000'001, "Alice"), player(2'000'002, "alice")};
  auto hit = resolve_userid(cfg, rows, "alice");
  ASSERT_TRUE(hit);
  EXPECT_EQ(hit->account_id, 2'000'002u);
  EXPECT_EQ(resolve_userid(cfg, rows, "ALICE"), std::nullopt);
  cfg.case_sensitive = false;
  EXPECT_EQ(resolve_userid(cfg, rows, "alice"), std::nullopt);  // 2 matches -> deny
  const std::vector<Account> one = {player(2'000'003, "Bob")};
  EXPECT_TRUE(resolve_userid(cfg, one, "bob"));
}
