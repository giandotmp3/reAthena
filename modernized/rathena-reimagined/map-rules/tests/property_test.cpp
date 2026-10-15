// Property tests for invariants (architecture §4 "GoogleTest + rapidcheck": hit in [5,100]%,
// zeny in [0, INT32_MAX]). Implemented as seeded GoogleTest loops so the scaffold builds offline;
// the generators can be swapped for rapidcheck::gtest without changing the properties.
#include <gtest/gtest.h>

#include <cstdint>

#include "rathena/map_rules/combat.hpp"
#include "rathena/map_rules/economy.hpp"
#include "rathena/map_rules/trade.hpp"

namespace mr = rathena::map_rules;

namespace {

constexpr int kCases = 20000;

std::int32_t any_i32(mr::Rng& rng, std::int32_t lo, std::int32_t hi) {
	const auto span = static_cast<std::uint32_t>(static_cast<std::int64_t>(hi) - lo + 1);
	return static_cast<std::int32_t>(lo + static_cast<std::int64_t>(rng.below(span)));
}

std::int64_t any_i64_amount(mr::Rng& rng) {
	// Mix small, boundary and huge amounts (both signs).
	switch (rng.below(4)) {
	case 0: return static_cast<std::int64_t>(rng.below(1000));
	case 1: return static_cast<std::int64_t>(rng.next_u32());
	case 2: return -static_cast<std::int64_t>(rng.next_u32());
	default: return static_cast<std::int64_t>(mr::kMaxZeny) - rng.below(10);
	}
}

} // namespace

TEST(Property, BR_MAP_011_HitChanceAlwaysWithinFiveToHundredPercent) {
	mr::SeededRng rng{11};
	for (int i = 0; i < kCases; ++i) {
		mr::BattleConfig cfg;
		cfg.mode = rng.below(2) ? mr::Mode::Renewal : mr::Mode::PreRenewal;
		const auto pct = mr::combat::hit_chance_pct(any_i32(rng, -100000, 100000), any_i32(rng, -100000, 100000), cfg);
		ASSERT_GE(pct, 5);
		ASSERT_LE(pct, 100);
	}
}

TEST(Property, BR_MAP_010_CritChanceNeverNegative) {
	mr::SeededRng rng{10};
	for (int i = 0; i < kCases; ++i) {
		const auto c = mr::combat::crit_chance_permille({.cri = any_i32(rng, 0, 2000),
		                                                 .target_luk = any_i32(rng, 0, 500),
		                                                 .attacker_is_player = rng.below(2) == 0,
		                                                 .target_is_player = rng.below(2) == 0,
		                                                 .target_sleeping = rng.below(2) == 0});
		ASSERT_GE(c, 0);
	}
}

TEST(Property, BR_MAP_045_ZenyStaysWithinZeroAndInt32Max) {
	mr::SeededRng rng{45};
	auto z = mr::Zeny::clamped(0);
	for (int i = 0; i < kCases; ++i) {
		const auto amount = any_i64_amount(rng);
		if (rng.below(2) == 0) {
			z = z.credited(amount);
		} else if (const auto d = z.debited(amount)) {
			z = *d;
		}
		ASSERT_GE(z.value(), 0);
		ASSERT_LE(z.value(), mr::kMaxZeny);
	}
}

TEST(Property, BR_MAP_044_VendingNeverCreatesZenyOrGoesNegative) {
	mr::SeededRng rng{44};
	const mr::BattleConfig cfg;
	for (int i = 0; i < kCases; ++i) {
		mr::economy::VendingPurchase p{.unit_price = any_i32(rng, 1, cfg.vending_max_value),
		                               .amount = any_i32(rng, 1, 10),
		                               .stock = any_i32(rng, 1, 10),
		                               .item_weight = 0,
		                               .buyer_zeny = mr::Zeny::clamped(any_i64_amount(rng)),
		                               .buyer_weight = 0,
		                               .buyer_max_weight = 1,
		                               .seller_zeny = mr::Zeny::clamped(any_i64_amount(rng))};
		const auto r = mr::economy::vending_buy(p, cfg);
		const std::int64_t before = std::int64_t{p.buyer_zeny.value()} + p.seller_zeny.value();
		const std::int64_t after = std::int64_t{r.buyer_zeny.value()} + r.seller_zeny.value() + r.tax;
		ASSERT_EQ(before, after) << "zeny must be conserved (tax is the only sink)";
		ASSERT_GE(r.buyer_zeny.value(), 0);
		ASSERT_GE(r.seller_zeny.value(), 0);
	}
}

TEST(Property, BR_MAP_041_TradeCommitConservesZeny) {
	mr::SeededRng rng{41};
	const mr::CharId a{150001};
	const mr::CharId b{150002};
	for (int i = 0; i < 2000; ++i) {
		const auto za = mr::Zeny::clamped(any_i64_amount(rng));
		const auto zb = mr::Zeny::clamped(any_i64_amount(rng));
		mr::economy::TradeSession s{a, b};
		ASSERT_EQ(s.accept(b, {1, 0, 0}, {1, 1, 1}), mr::economy::TradeError::None);
		(void)s.set_zeny(a, rng.below(static_cast<std::uint32_t>(za.value()) + 1u), za);
		(void)s.set_zeny(b, rng.below(static_cast<std::uint32_t>(zb.value()) + 1u), zb);
		(void)s.lock(a);
		(void)s.lock(b);
		(void)s.confirm(a);
		(void)s.confirm(b);
		const auto r = s.commit({za, {}, 0, 1, 1}, {zb, {}, 0, 1, 1});
		ASSERT_EQ(std::int64_t{za.value()} + zb.value(),
		          std::int64_t{r.requester.zeny.value()} + r.target.zeny.value());
	}
}
