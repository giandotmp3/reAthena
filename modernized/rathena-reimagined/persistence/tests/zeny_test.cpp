// Acceptance tests: BR-MAP-045 -- zeny never goes negative; credits clamp at 2,147,483,647.
// Plus the M4 storage invariant (INT + CHECK (zeny >= 0)): out-of-range DB values are rejected.
#include <gtest/gtest.h>

#include <limits>
#include <stdexcept>

#include "rathena/persistence/zeny.hpp"

using rathena::persistence::Zeny;
using rathena::persistence::ZenyRangeError;

TEST(BR_MAP_045_Zeny, BR_MAP_045_MaxIsInt32Max) {
	EXPECT_EQ(Zeny::kMax, 2'147'483'647);
	EXPECT_EQ(Zeny::of(2'147'483'647).value(), 2'147'483'647);
}

TEST(BR_MAP_045_Zeny, BR_MAP_045_ConstructionRejectsNegative) {
	EXPECT_THROW(Zeny::of(-1), ZenyRangeError);
}

TEST(BR_MAP_045_Zeny, BR_MAP_045_ConstructionRejectsAboveMax) {
	EXPECT_THROW(Zeny::of(std::int64_t{Zeny::kMax} + 1), ZenyRangeError);
}

TEST(BR_MAP_045_Zeny, BR_MAP_045_CreditClampsAtMax) {
	const auto r = Zeny::of(2'147'483'000).credit(1'000);
	EXPECT_EQ(r.balance.value(), Zeny::kMax);
	EXPECT_EQ(r.clamped, 353);
}

TEST(BR_MAP_045_Zeny, BR_MAP_045_CreditAtMaxStaysAtMax) {
	const auto r = Zeny::of(Zeny::kMax).credit(std::numeric_limits<std::int64_t>::max() / 2);
	EXPECT_EQ(r.balance.value(), Zeny::kMax);
}

TEST(BR_MAP_045_Zeny, BR_MAP_045_CreditWithinRangeIsExact) {
	const auto r = Zeny::of(100).credit(250);
	EXPECT_EQ(r.balance.value(), 350);
	EXPECT_EQ(r.clamped, 0);
}

TEST(BR_MAP_045_Zeny, BR_MAP_045_NegativeCreditIsRejected) {
	EXPECT_THROW((void)Zeny::of(100).credit(-1), std::invalid_argument);
}

TEST(BR_MAP_045_Zeny, BR_MAP_045_DebitNeverGoesNegative) {
	EXPECT_FALSE(Zeny::of(100).debit(101).has_value());
	EXPECT_FALSE(Zeny::of(0).debit(1).has_value());
}

TEST(BR_MAP_045_Zeny, BR_MAP_045_DebitToExactlyZeroIsAllowed) {
	const auto z = Zeny::of(100).debit(100);
	ASSERT_TRUE(z.has_value());
	EXPECT_EQ(z->value(), 0);
}

TEST(BR_MAP_045_Zeny, BR_MAP_045_NegativeDebitIsRejected) {
	EXPECT_FALSE(Zeny::of(100).debit(-5).has_value());
}

TEST(BR_MAP_045_Zeny, BR_MAP_045_FromDbRejectsValuesOutsideInvariant) {
	// Legacy `char.zeny` is INT UNSIGNED while C++ used int32 (D3); M4 makes it INT + CHECK >= 0.
	EXPECT_FALSE(Zeny::from_db(-1).has_value());
	EXPECT_FALSE(Zeny::from_db(4'000'000'000LL).has_value());
	ASSERT_TRUE(Zeny::from_db(0).has_value());
	EXPECT_EQ(Zeny::from_db(Zeny::kMax)->value(), Zeny::kMax);
}

TEST(BR_MAP_045_Zeny, BR_MAP_045_PropertyBalanceStaysInRange) {
	// Deterministic sweep standing in for the rapidcheck property (architecture §4).
	Zeny z{};
	std::uint64_t x = 0x9E3779B97F4A7C15ULL;
	for (int i = 0; i < 10'000; ++i) {
		x ^= x << 13; x ^= x >> 7; x ^= x << 17;
		const auto amount = static_cast<std::int64_t>(x % 3'000'000'000ULL);
		if (x & 1) {
			z = z.credit(amount).balance;
		} else if (const auto d = z.debit(amount)) {
			z = *d;
		}
		ASSERT_GE(z.value(), 0);
		ASSERT_LE(z.value(), Zeny::kMax);
	}
}
