// BR-LOGIN-034: web auth token.
#include <gtest/gtest.h>

#include <cctype>
#include <set>

#include "rathena/login/web_token.hpp"
#include "test_support.hpp"

using namespace rathena::login;
using namespace rathena::login::test;

TEST(LoginWebToken, BR_LOGIN_034_SixteenAlphanumericUniqueChars) {
  SeededRng rng(7);
  std::set<std::string> taken;
  for (int i = 0; i < 50; ++i) {
    auto t = generate_web_token(rng, [&](const std::string& s) { return taken.count(s) > 0; });
    ASSERT_TRUE(t);
    EXPECT_EQ(t->size(), 16u);
    for (char c : *t) EXPECT_TRUE(std::isalnum(static_cast<unsigned char>(c)));
    EXPECT_TRUE(taken.insert(*t).second);
  }
}

TEST(LoginWebToken, BR_LOGIN_034_DefectFix_RetryCappedAt20) {
  SeededRng rng(1);
  int calls = 0;
  const auto t = generate_web_token(rng, [&](const std::string&) {
    ++calls;
    return true;  // every candidate collides
  });
  EXPECT_FALSE(t);
  EXPECT_EQ(calls, 20);
}

TEST(LoginWebToken, BR_LOGIN_034_LifecycleEnabledOnlineDisabled10sAfterLogout) {
  Account a = player();
  enable_web_token(a, "AAAABBBBCCCCDDDD");
  EXPECT_TRUE(a.web_auth_token_enabled);
  EXPECT_EQ(schedule_web_token_disable(kNow).disable_at, kNow + 10);
  disable_web_token(a);
  EXPECT_FALSE(a.web_auth_token_enabled);
  enable_web_token(a, "AAAABBBBCCCCDDDD");
  wipe_web_token_on_startup(a);
  EXPECT_TRUE(a.web_auth_token.empty());
  EXPECT_FALSE(a.web_auth_token_enabled);
}
