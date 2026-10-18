// Implemented: domain value helpers.
#include "support.hpp"

using namespace rathena::persist;

TEST(PERSIST_DOMAIN_001, ZeroAndEmptyValuesAreRecognised) {
	EXPECT_TRUE(is_zero_or_empty(Value{std::int64_t{0}}));
	EXPECT_TRUE(is_zero_or_empty(Value{""}));
	EXPECT_TRUE(is_zero_or_empty(Value{false}));
	EXPECT_TRUE(is_zero_or_empty(Value{0.0}));
	EXPECT_FALSE(is_zero_or_empty(Value{std::int64_t{7}}));
	EXPECT_FALSE(is_zero_or_empty(Value{"x"}));
	EXPECT_FALSE(is_zero_or_empty(Value{std::make_shared<const ValueTable>()}));
}

TEST(PERSIST_DOMAIN_002, OwnersOrderByScopeThenId) {
	EXPECT_LT((OwnerRef{OwnerScope::Character, 9}), (OwnerRef{OwnerScope::Account, 1}));
	EXPECT_EQ((OwnerRef{OwnerScope::GlobalAccount, 0}), (OwnerRef{OwnerScope::GlobalAccount, 0}));
	EXPECT_NE(testing_support::account(5), testing_support::character(5));
}

TEST(PERSIST_DOMAIN_003, EncodedSizeCountsNestedTables) {
	auto t = std::make_shared<ValueTable>();
	t->entries.emplace_back("ab", Value{"xyz"});
	EXPECT_EQ(encoded_size(Value{std::shared_ptr<const ValueTable>(t)}), 5u);
}
