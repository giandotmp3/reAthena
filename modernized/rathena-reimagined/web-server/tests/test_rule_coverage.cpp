// Architecture §8: every rule owned by this service has at least one test
// (passing or pending). Fails if a BR-WEB-NNN suite disappears.
#include <cstdio>
#include <set>
#include <string>

#include <gtest/gtest.h>

namespace {

TEST(BR_WEB_Manifest, EveryOwnedRuleHasATest) {
    std::set<std::string> suites;
    const auto* unit = ::testing::UnitTest::GetInstance();
    for (int i = 0; i < unit->total_test_suite_count(); ++i) {
        suites.insert(unit->GetTestSuite(i)->name());
    }
    for (int n = 1; n <= 17; ++n) {
        char prefix[16];
        std::snprintf(prefix, sizeof(prefix), "BR_WEB_%03d_", n);
        bool found = false;
        for (const auto& s : suites) {
            if (s.rfind(prefix, 0) == 0) {
                found = true;
                break;
            }
        }
        EXPECT_TRUE(found) << "no test suite for " << prefix;
    }
}

}  // namespace
