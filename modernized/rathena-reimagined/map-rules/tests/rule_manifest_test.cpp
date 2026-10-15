// Traceability guard (architecture §8): every BR-MAP rule assigned to map-rules (all 61) has at
// least one acceptance test, passing or pending, and manifest/rule_manifest.tsv maps each one to
// its rule group and test file. Test names carry the BR-ID with '-' replaced by '_'.
#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <string_view>

namespace {

std::string rule_id(int n) {
	char buf[16];
	std::snprintf(buf, sizeof buf, "BR-MAP-%03d", n);
	return buf;
}

std::string to_test_token(std::string s) {
	for (auto& c : s) {
		if (c == '-') c = '_';
	}
	return s;
}

std::string expected_group(int n) {
	if (n >= 4 && n <= 20) return "combat";
	if (n >= 21 && n <= 34) return "progression";
	if (n >= 51 && n <= 55) return "skills";
	if (n >= 56 && n <= 61) return "anticheat";
	return "economy"; // 001-003, 035-050
}

} // namespace

TEST(RuleManifest, EveryAssignedRuleHasAnAcceptanceTest) {
	const auto* unit = ::testing::UnitTest::GetInstance();
	for (int n = 1; n <= 61; ++n) {
		const std::string token = to_test_token(rule_id(n));
		bool found = false;
		for (int i = 0; i < unit->total_test_suite_count() && !found; ++i) {
			const auto* suite = unit->GetTestSuite(i);
			if (std::string_view(suite->name()) == "RuleManifest") continue;
			for (int j = 0; j < suite->total_test_count() && !found; ++j) {
				found = std::string_view(suite->GetTestInfo(j)->name()).find(token) != std::string_view::npos;
			}
		}
		EXPECT_TRUE(found) << rule_id(n) << " has no acceptance test";
	}
}

TEST(RuleManifest, RuleManifestMapsEveryRuleToGroupAndExistingTestFile) {
	std::ifstream in(std::string(MAP_RULES_MANIFEST_DIR) + "/rule_manifest.tsv");
	ASSERT_TRUE(in) << "manifest/rule_manifest.tsv missing";
	std::map<std::string, std::pair<std::string, std::string>> rows;
	std::string line;
	while (std::getline(in, line)) {
		if (line.empty() || line[0] == '#') continue;
		std::istringstream ls(line);
		std::string id, group, test_file, status;
		std::getline(ls, id, '\t');
		std::getline(ls, group, '\t');
		std::getline(ls, test_file, '\t');
		std::getline(ls, status, '\t');
		ASSERT_TRUE(status == "implemented" || status == "pending") << id << ": bad status '" << status << "'";
		ASSERT_TRUE(rows.emplace(id, std::make_pair(group, test_file)).second) << "duplicate " << id;
	}
	for (int n = 1; n <= 61; ++n) {
		const auto it = rows.find(rule_id(n));
		ASSERT_NE(it, rows.end()) << rule_id(n) << " missing from rule_manifest.tsv";
		EXPECT_EQ(it->second.first, expected_group(n)) << rule_id(n) << " in wrong rule group (architecture §8)";
		std::ifstream test(std::string(MAP_RULES_TESTS_DIR) + "/" + it->second.second);
		ASSERT_TRUE(test) << rule_id(n) << ": test file " << it->second.second << " missing";
		std::stringstream body;
		body << test.rdbuf();
		EXPECT_NE(body.str().find(to_test_token(rule_id(n))), std::string::npos)
		    << rule_id(n) << " not referenced in " << it->second.second;
	}
	EXPECT_EQ(rows.size(), 61u);
}
