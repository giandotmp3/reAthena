// Traceability guard (architecture §8): every behavior-contract rule assigned to persistence has at
// least one acceptance test, passing or pending. Test names carry the BR-ID with '-' -> '_'.
#include <gtest/gtest.h>

#include <array>
#include <string>
#include <string_view>

namespace {

// persistence owns: BR-LOGIN-008 (account id allocation), BR-LOGIN-037 (atomic account delete),
// BR-MAP-045 (Zeny value type invariants) and BR-CHAR-013 (delete cascade order, pending).
constexpr std::array<std::string_view, 4> kAssignedRules = {
    "BR-LOGIN-008", "BR-LOGIN-037", "BR-MAP-045", "BR-CHAR-013",
};

std::string to_test_token(std::string_view rule) {
	std::string s(rule);
	for (auto& c : s)
		if (c == '-') c = '_';
	return s;
}

TEST(RuleManifest, EveryAssignedRuleHasAnAcceptanceTest) {
	const auto* unit = ::testing::UnitTest::GetInstance();
	for (const auto rule : kAssignedRules) {
		const std::string token = to_test_token(rule);
		bool found = false;
		for (int i = 0; i < unit->total_test_suite_count() && !found; ++i) {
			const auto* suite = unit->GetTestSuite(i);
			for (int j = 0; j < suite->total_test_count() && !found; ++j)
				found = std::string_view(suite->GetTestInfo(j)->name()).find(token) != std::string_view::npos;
		}
		EXPECT_TRUE(found) << rule << " has no acceptance test";
	}
}

} // namespace
