// Manifest sanity (architecture §8): every rule assigned to char-rules has exactly one home.
#include <gtest/gtest.h>

#include <cstdio>
#include <set>
#include <string>

#include "rathena/char_rules/rule_manifest.hpp"

using namespace rathena::char_rules;

TEST(RuleManifest, MANIFEST_All55CharRulesAndFiveInterRulesAssignedOnce) {
	std::set<std::string> ids;
	int chars = 0;
	int inter = 0;
	for (const auto& r : kRuleManifest) {
		EXPECT_TRUE(ids.insert(std::string(r.id)).second) << "duplicate " << r.id;
		EXPECT_FALSE(r.module.empty());
		chars += r.id.starts_with("BR-CHAR-") ? 1 : 0;
		inter += r.id.starts_with("BR-INTER-") ? 1 : 0;
	}
	EXPECT_EQ(chars, 55);
	EXPECT_EQ(inter, 5);
	for (int i = 1; i <= 55; ++i) {
		char buf[16];
		std::snprintf(buf, sizeof buf, "BR-CHAR-%03d", i);
		EXPECT_TRUE(ids.count(buf)) << buf;
	}
	for (const char* id : {"BR-INTER-008", "BR-INTER-009", "BR-INTER-015", "BR-INTER-018", "BR-INTER-019"}) {
		EXPECT_TRUE(ids.count(id)) << id;
	}
}
