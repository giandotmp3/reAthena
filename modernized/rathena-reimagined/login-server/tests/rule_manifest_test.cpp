// Traceability guard (architecture §8): every behavior-contract rule assigned
// to login-server has at least one acceptance test, passing or pending.
// Test names carry the BR-ID with '-' replaced by '_'.
#include <gtest/gtest.h>

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::vector<std::string> assigned_rules() {
  std::vector<std::string> r;
  for (int i = 1; i <= 39; ++i) {
    // BR-LOGIN-004 was removed at HITL #1; its test asserts the removal.
    char buf[16];
    std::snprintf(buf, sizeof buf, "BR-LOGIN-%03d", i);
    r.emplace_back(buf);
  }
  for (const char* x : {"BR-INTER-004", "BR-INTER-005", "BR-INTER-006", "BR-INTER-014", "BR-INTER-017"})
    r.emplace_back(x);
  return r;
}

std::string to_token(std::string s) {
  for (auto& c : s)
    if (c == '-') c = '_';
  return s;
}

TEST(RuleManifest, EveryAssignedRuleHasAnAcceptanceTest) {
  const auto* unit = ::testing::UnitTest::GetInstance();
  for (const auto& rule : assigned_rules()) {
    const std::string token = to_token(rule) + "_";
    bool found = false;
    for (int i = 0; i < unit->total_test_suite_count() && !found; ++i) {
      const auto* suite = unit->GetTestSuite(i);
      for (int j = 0; j < suite->total_test_count() && !found; ++j)
        found = std::string_view(suite->GetTestInfo(j)->name()).find(token) != std::string_view::npos;
    }
    EXPECT_TRUE(found) << rule << " has no acceptance test";
  }
}

}  // namespace
