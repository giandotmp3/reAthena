// Acceptance tests: injectable Rng (architecture §3.2 step 1, §6 determinism; spec §4 NFR
// "Combat RNG must follow the legacy distributions").
#include <gtest/gtest.h>

#include <set>

#include "rathena/core/rng.hpp"

using namespace rathena::core;

TEST(CORE_Rng, CORE_Rng_SeededStreamIsReproducible) {
	SeededRng a(42), b(42), c(43);
	bool differs = false;
	for (int i = 0; i < 1000; ++i) {
		const auto x = a.next_u32();
		EXPECT_EQ(x, b.next_u32());
		differs |= (x != c.next_u32());
	}
	EXPECT_TRUE(differs);
}

TEST(CORE_Rng, CORE_Rng_EngineMatchesStandardMt19937) {
	// The standard fixes mt19937's 10000th output for the default seed 5489 (portable across K4 toolchains).
	SeededRng rng(5489u);
	std::uint32_t v = 0;
	for (int i = 0; i < 10000; ++i) {
		v = rng.next_u32();
	}
	EXPECT_EQ(v, 4123659995u);
}

TEST(CORE_Rng, CORE_Rng_RangeIsInclusiveAndBounded) {
	SeededRng rng(7);
	std::set<std::int32_t> seen;
	for (int i = 0; i < 5000; ++i) {
		const auto v = rng.range(-2, 2);
		ASSERT_GE(v, -2);
		ASSERT_LE(v, 2);
		seen.insert(v);
	}
	EXPECT_EQ(seen.size(), 5u);
	EXPECT_EQ(rng.range(9, 9), 9);
	const auto swapped = rng.range(10, 1);
	EXPECT_GE(swapped, 1);
	EXPECT_LE(swapped, 10);
	const auto full = rng.range(INT32_MIN, INT32_MAX);
	(void)full; // must not overflow / trap
}

TEST(CORE_Rng, CORE_Rng_BelowRejectsBiasedLowBucket) {
	// bound 3: threshold = 2^32 mod 3 = 1, so raw 0 is rejected and the next value is used.
	SequenceRng rng{0u, 5u};
	EXPECT_EQ(rng.below(3), 5u % 3u);
	EXPECT_EQ(rng.calls(), 2u);
	EXPECT_EQ(rng.below(0), 0u);
	EXPECT_EQ(rng.below(1), 0u);
}

TEST(CORE_Rng, CORE_Rng_ChancePerMilleEdgeCases) {
	SeededRng rng(3);
	EXPECT_FALSE(rng.chance(0, 1000));
	EXPECT_TRUE(rng.chance(1000, 1000));
	EXPECT_TRUE(rng.chance(1500, 1000));
	EXPECT_FALSE(rng.chance(5, 0));
	// Scripted: below(1000) yields raw % 1000 for raw >= 2^32 mod 1000 (= 296); 88 < 89 hits,
	// 89 does not (8.9% crit, BR-MAP-010 example).
	SequenceRng hit{1088u};
	EXPECT_TRUE(hit.chance(89, 1000));
	SequenceRng miss{1089u};
	EXPECT_FALSE(miss.chance(89, 1000));
}

TEST(CORE_Rng, CORE_Rng_ChanceFrequencyApproximatesProbability) {
	SeededRng rng(2024);
	int hits = 0;
	constexpr int kTrials = 200000;
	for (int i = 0; i < kTrials; ++i) {
		hits += rng.chance(89, 1000) ? 1 : 0;
	}
	const double p = static_cast<double>(hits) / kTrials;
	EXPECT_NEAR(p, 0.089, 0.004);
}

TEST(CORE_Rng, CORE_Rng_InjectableThroughBaseReference) {
	SequenceRng scripted{1u, 2u, 3u};
	Rng& r = scripted;
	EXPECT_EQ(r.next_u32(), 1u);
	EXPECT_EQ(r.next_u32(), 2u);
	EXPECT_EQ(r.next_u32(), 3u);
	EXPECT_EQ(r.next_u32(), 1u); // cycles
}

TEST(CORE_Rng, PENDING_ARCH_6_GoldenVectorsMatchLegacyRnd) {
	// Architecture §3.2 step 1 / §6: golden vectors captured from an instrumented legacy build
	// (src/common/random.hpp with the injectable Rng patch) are not yet available.
	GTEST_SKIP() << "PENDING ARCH-6-GOLDEN-RNG: needs golden vectors from the instrumented legacy build";
}
