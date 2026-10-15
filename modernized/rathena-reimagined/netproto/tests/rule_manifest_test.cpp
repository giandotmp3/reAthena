// Traceability guard (architecture section 8): every behavior-contract rule
// assigned to netproto must have at least one acceptance test, passing or
// pending. Test names carry the BR-ID with '-' replaced by '_'.
#include <gtest/gtest.h>

#include <array>
#include <string>
#include <string_view>

namespace {

// netproto owns BR-INTER-001, 002, 007, 010-013 and the handshake half of
// BR-LOGIN-025 / BR-CHAR-030; it also owns the 0x2717 wire codec of BR-INTER-003.
constexpr std::array<std::string_view, 10> kAssignedRules = {
    "BR-INTER-001", "BR-INTER-002", "BR-INTER-003", "BR-INTER-007", "BR-INTER-010",
    "BR-INTER-011", "BR-INTER-012", "BR-INTER-013", "BR-LOGIN-025", "BR-CHAR-030",
};

std::string to_test_token(std::string_view rule) {
  std::string s(rule);
  for (auto& c : s) {
    if (c == '-') c = '_';
  }
  return s;
}

TEST(RuleManifest, EveryAssignedRuleHasAnAcceptanceTest) {
  const auto* unit = ::testing::UnitTest::GetInstance();
  for (const auto rule : kAssignedRules) {
    const std::string token = to_test_token(rule);
    bool found = false;
    for (int i = 0; i < unit->total_test_suite_count() && !found; ++i) {
      const auto* suite = unit->GetTestSuite(i);
      for (int j = 0; j < suite->total_test_count() && !found; ++j) {
        found = std::string_view(suite->GetTestInfo(j)->name()).find(token) != std::string_view::npos;
      }
    }
    EXPECT_TRUE(found) << rule << " has no acceptance test";
  }
}

}  // namespace
