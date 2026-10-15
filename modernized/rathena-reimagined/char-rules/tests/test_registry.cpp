// Acceptance tests: registries and cooldown persistence (CAP-07). BR-CHAR-050, 054; BR-INTER-015.
#include <gtest/gtest.h>

#include "rathena/char_rules/registry.hpp"

using namespace rathena::char_rules;

// ---- BR-CHAR-050 -----------------------------------------------------------------------------------
TEST(Registry, BR_CHAR_050_ScopesByPrefix) {
	EXPECT_EQ(registry_scope("##CASHPOINTS"), RegistryScope::GlobalAccount);
	EXPECT_EQ(registry_scope("#KAFRAPOINTS"), RegistryScope::Account);
	EXPECT_EQ(registry_scope("quest_step"), RegistryScope::Character);
	EXPECT_EQ(registry_scope("$event_flag"), RegistryScope::MapServer);
	EXPECT_EQ(registry_scope("@temp"), RegistryScope::Temporary);
	EXPECT_EQ(registry_scope(".npcvar"), RegistryScope::Temporary);
	EXPECT_EQ(registry_scope("'instvar"), RegistryScope::Temporary);
	EXPECT_EQ(registry_scope(""), RegistryScope::Invalid);
	EXPECT_EQ(registry_scope("##"), RegistryScope::Invalid);
	EXPECT_TRUE(is_string_variable("#name$"));
	EXPECT_FALSE(is_string_variable("#name"));
}

TEST(Registry, BR_CHAR_050_ZeroOrEmptyValueDeletes) {
	EXPECT_EQ(registry_op({"#a", 0, std::int64_t{0}}), RegistryOp::Delete);
	EXPECT_EQ(registry_op({"#a", 0, std::int64_t{5}}), RegistryOp::Upsert);
	EXPECT_EQ(registry_op({"#a$", 0, std::string{}}), RegistryOp::Delete);
	EXPECT_EQ(registry_op({"#a$", 0, std::string{"x"}}), RegistryOp::Upsert);
	EXPECT_EQ(registry_op({"a", 3, std::int64_t{-1}}), RegistryOp::Upsert);
}

// ---- BR-INTER-015 ----------------------------------------------------------------------------------
TEST(Registry, BR_INTER_015_GlobalRegistryRoutedByPrefix) {
	EXPECT_EQ(registry_owner(registry_scope("##GLOBAL")), RegistryOwner::Login);
	EXPECT_EQ(registry_owner(registry_scope("#ACCOUNT")), RegistryOwner::Char);
	EXPECT_EQ(registry_owner(registry_scope("charvar")), RegistryOwner::Char);
	EXPECT_EQ(registry_owner(registry_scope("$mapvar")), RegistryOwner::Map);
	EXPECT_EQ(registry_owner(registry_scope("@tmp")), RegistryOwner::None);
}

// ---- BR-CHAR-054 -----------------------------------------------------------------------------------
TEST(Registry, BR_CHAR_054_AtMost20CooldownsPersisted) {
	std::vector<SkillCooldown> active;
	for (std::uint16_t i = 1; i <= 25; ++i) active.push_back({i, 1000});
	const auto saved = cooldowns_to_persist(active);
	ASSERT_EQ(saved.size(), kMaxSkillCooldown);
	EXPECT_EQ(saved.front().skill_id, 1);
	EXPECT_EQ(saved.back().skill_id, 20);
	EXPECT_TRUE(cooldowns_to_persist({{5, 0}, {6, -10}}).empty()); // elapsed cooldowns dropped
}

TEST(Registry, BR_CHAR_054_RestoredOnceOnLoad) {
	CooldownStore store;
	store.save(150'001, {{10, 5000}, {11, 2500}});
	const auto first = store.load_once(150'001);
	EXPECT_EQ(first, (std::vector<SkillCooldown>{{10, 5000}, {11, 2500}}));
	EXPECT_TRUE(store.load_once(150'001).empty());
	EXPECT_TRUE(store.load_once(150'002).empty());
}
