// Acceptance tests: rules/economy, BR-MAP-001..003, 035..037, 042..050 (spec §5.4, CAP-17/18).
#include <gtest/gtest.h>

#include "rathena/map_rules/economy.hpp"

namespace mr = rathena::map_rules;
namespace ec = rathena::map_rules::economy;

namespace {
mr::BattleConfig re() { return {}; }
mr::BattleConfig pre_re() {
	mr::BattleConfig c;
	c.mode = mr::Mode::PreRenewal;
	return c;
}
} // namespace

TEST(Economy, BR_MAP_001_MaxWeightWorkedExample) {
	// Base 20000, STR 50, Enlarge Weight Lv5 -> 45000.
	EXPECT_EQ(ec::max_weight(20000, 50, 5), 45000);
}

TEST(Economy, BR_MAP_002_WeightThresholdsRenewalAndPreRenewal) {
	EXPECT_EQ(ec::weight_state(699, 1000, re()), ec::WeightState::Normal);
	EXPECT_EQ(ec::weight_state(700, 1000, re()), ec::WeightState::Weight50);
	EXPECT_EQ(ec::weight_state(500, 1000, pre_re()), ec::WeightState::Weight50);
	EXPECT_EQ(ec::weight_state(499, 1000, pre_re()), ec::WeightState::Normal);
	EXPECT_EQ(ec::weight_state(900, 1000, re()), ec::WeightState::Weight90);
	EXPECT_EQ(ec::weight_state(900, 1000, pre_re()), ec::WeightState::Weight90);
}

TEST(Economy, BR_MAP_003_BoxBlockedAtNinetyPercentOrTenFreeSlots) {
	EXPECT_FALSE(ec::can_open_box(900, 1000, 50, re()));
	EXPECT_TRUE(ec::can_open_box(899, 1000, 50, re()));
	EXPECT_FALSE(ec::can_open_box(0, 1000, 10, re()));
	EXPECT_TRUE(ec::can_open_box(0, 1000, 11, re()));
	EXPECT_TRUE(ec::can_open_box(900, 1000, 50, pre_re())); // weight gate is renewal-only
}

TEST(Economy, BR_MAP_035_LinearDropScalingClamped) {
	const auto cfg = re();
	EXPECT_EQ(ec::drop_adjust(100, 200, 1, 10000, cfg), 200);
	EXPECT_EQ(ec::drop_adjust(8000, 200, 1, 10000, cfg), 10000);
	EXPECT_EQ(ec::drop_adjust(1, 10, 1, 10000, cfg), 1);
}

TEST(Economy, BR_MAP_035_LogarithmicDropScaling) {
	auto cfg = re();
	cfg.logarithmic_drops = true;
	// 1% at x2: 100 x (5 - log10(100))^(ln 2 / ln 5) + 0.5 = 161.
	EXPECT_EQ(ec::drop_adjust(100, 200, 1, 10000, cfg), 161);
}

TEST(Economy, BR_MAP_036_DropModifierWorkedExample) {
	// 1% base with +100% -> 2%.
	EXPECT_EQ(ec::drop_rate_with_modifiers({.base_rate = 100, .bonus_pct = 100}, re()), 200);
}

TEST(Economy, BR_MAP_036_CapsAndFloor) {
	const auto cfg = re();
	EXPECT_EQ(ec::drop_rate_with_modifiers({.base_rate = 5000, .bonus_pct = 100}, cfg), 9000);  // 90% cap
	EXPECT_EQ(ec::drop_rate_with_modifiers({.base_rate = 9500, .bonus_pct = 100}, cfg), 10000); // 100% cap
	EXPECT_EQ(ec::drop_rate_with_modifiers({.base_rate = 0}, cfg), 1);                          // 0.01% floor
}

TEST(Economy, BR_MAP_037_LootPriorityWindowsAndRange) {
	const auto cfg = re();
	ec::LootRights r{{mr::CharId{1}, mr::CharId{2}, mr::CharId{3}}, 1000, false};
	const mr::Position item{1, 10, 10};
	const mr::Position near{1, 12, 12};
	EXPECT_TRUE(ec::may_pick_up(r, mr::CharId{1}, 1000, item, near, cfg));
	EXPECT_FALSE(ec::may_pick_up(r, mr::CharId{2}, 3999, item, near, cfg));
	EXPECT_TRUE(ec::may_pick_up(r, mr::CharId{2}, 4000, item, near, cfg));  // after 3 s
	EXPECT_TRUE(ec::may_pick_up(r, mr::CharId{3}, 6000, item, near, cfg));  // after 3 + 2 s
	EXPECT_FALSE(ec::may_pick_up(r, mr::CharId{9}, 7999, item, near, cfg));
	EXPECT_TRUE(ec::may_pick_up(r, mr::CharId{9}, 8000, item, near, cfg));  // after 3 + 2 + 2 s
	EXPECT_FALSE(ec::may_pick_up(r, mr::CharId{1}, 1000, item, {1, 13, 10}, cfg)); // 3 cells away
}

TEST(Economy, BR_MAP_037_MvpFirstWindowIsTenSeconds) {
	ec::LootRights r{{mr::CharId{1}, mr::CharId{2}, {}}, 0, true};
	const mr::Position p{1, 0, 0};
	EXPECT_FALSE(ec::may_pick_up(r, mr::CharId{2}, 9999, p, p, re()));
	EXPECT_TRUE(ec::may_pick_up(r, mr::CharId{2}, 10000, p, p, re()));
}

TEST(Economy, BR_MAP_042_VendingNeedsSkillCartItemLimitAndPriceCap) {
	const auto cfg = re();
	EXPECT_EQ(ec::validate_vending_open({0, true, {100}}, cfg), ec::VendingOpenError::NoSkill);
	EXPECT_EQ(ec::validate_vending_open({3, false, {100}}, cfg), ec::VendingOpenError::NoCart);
	EXPECT_EQ(ec::validate_vending_open({3, true, {1, 1, 1, 1, 1}}, cfg), ec::VendingOpenError::None);
	EXPECT_EQ(ec::validate_vending_open({3, true, {1, 1, 1, 1, 1, 1}}, cfg), ec::VendingOpenError::TooManyItems);
	EXPECT_EQ(ec::validate_vending_open({3, true, {1000000001}}, cfg), ec::VendingOpenError::PriceOutOfRange);
	EXPECT_EQ(ec::validate_vending_open({3, true, {0}}, cfg), ec::VendingOpenError::PriceOutOfRange);
}

TEST(Economy, BR_MAP_043_VendingPurchaseChecksStockZenyWeight) {
	const auto cfg = re();
	ec::VendingPurchase p{.unit_price = 1000, .amount = 2, .stock = 5, .item_weight = 10,
	                      .buyer_zeny = *mr::Zeny::checked(5000), .buyer_weight = 0, .buyer_max_weight = 1000,
	                      .seller_zeny = *mr::Zeny::checked(0)};
	const auto ok = ec::vending_buy(p, cfg);
	ASSERT_EQ(ok.error, ec::VendingBuyError::None);
	EXPECT_EQ(ok.buyer_zeny.value(), 3000);
	EXPECT_EQ(ok.seller_zeny.value(), 2000);

	auto out = p;
	out.amount = 6;
	EXPECT_EQ(ec::vending_buy(out, cfg).error, ec::VendingBuyError::OutOfStock);
	auto poor = p;
	poor.buyer_zeny = *mr::Zeny::checked(1999);
	EXPECT_EQ(ec::vending_buy(poor, cfg).error, ec::VendingBuyError::NotEnoughZeny);
	auto heavy = p;
	heavy.buyer_weight = 990;
	EXPECT_EQ(ec::vending_buy(heavy, cfg).error, ec::VendingBuyError::Overweight);
}

TEST(Economy, BR_MAP_044_VendingTaxFivePercentFromHundredMillion) {
	const auto cfg = re();
	EXPECT_EQ(ec::vending_tax(100000000, cfg), 5000000);
	EXPECT_EQ(ec::vending_tax(99999999, cfg), 0);
	ec::VendingPurchase p{.unit_price = 100000000, .amount = 1, .stock = 1, .item_weight = 0,
	                      .buyer_zeny = *mr::Zeny::checked(100000000), .buyer_max_weight = 10,
	                      .seller_zeny = *mr::Zeny::checked(0)};
	const auto r = ec::vending_buy(p, cfg);
	EXPECT_EQ(r.seller_zeny.value(), 95000000);
	EXPECT_EQ(r.tax, 5000000);
}

TEST(Economy, BR_MAP_045_ZenyNeverNegativeAndCreditsClamp) {
	const auto z = *mr::Zeny::checked(100);
	EXPECT_FALSE(z.debited(101).has_value());
	EXPECT_EQ(z.debited(100)->value(), 0);
	EXPECT_EQ(mr::Zeny::checked(2147483000)->credited(1000).value(), 2147483647);
	EXPECT_FALSE(mr::Zeny::checked(-1).has_value());
}

TEST(Economy, BR_MAP_046_StackLimitThirtyThousand) {
	EXPECT_TRUE(ec::can_add_to_stack(29999, 1));
	EXPECT_FALSE(ec::can_add_to_stack(30000, 1));
	EXPECT_FALSE(ec::can_add_to_stack(0, 0));
}

TEST(Economy, BR_MAP_047_DiscountAndOvercharge) {
	const auto cfg = re();
	EXPECT_EQ(ec::npc_buy_price(1000, 10, 0, cfg), 760);  // Discount Lv10: 24%
	EXPECT_EQ(ec::npc_buy_price(1000, 10, 5, cfg), 750);  // Compulsion Lv5: 25% wins
	EXPECT_EQ(ec::npc_buy_price(1, 10, 0, cfg), 1);       // min_shop_buy
	EXPECT_EQ(ec::npc_sell_price(1000, 10, cfg), 1240);   // Overcharge Lv10: 24%
	EXPECT_EQ(ec::npc_sell_price(1000, 0, cfg), 1000);
}

TEST(Economy, BR_MAP_048_EquipRequirements) {
	mr::ItemDef def{.nameid = 1201, .job_mask = 0b10, .equip_level_min = 10, .equip_level_max = 50,
	                .sex = mr::Sex::Female, .equip_locations = 0x2};
	const ec::EquipCandidate ok{0b10, 20, mr::Sex::Female, 0x2};
	EXPECT_EQ(ec::check_equip(def, ok), ec::EquipError::None);
	auto c = ok;
	c.job_bit = 0b1;
	EXPECT_EQ(ec::check_equip(def, c), ec::EquipError::WrongJob);
	c = ok;
	c.base_level = 9;
	EXPECT_EQ(ec::check_equip(def, c), ec::EquipError::LevelTooLow);
	c = ok;
	c.base_level = 51;
	EXPECT_EQ(ec::check_equip(def, c), ec::EquipError::LevelTooHigh);
	c = ok;
	c.sex = mr::Sex::Male;
	EXPECT_EQ(ec::check_equip(def, c), ec::EquipError::WrongSex);
	c = ok;
	c.requested_location = 0x4;
	EXPECT_EQ(ec::check_equip(def, c), ec::EquipError::WrongLocation);
}

TEST(Economy, BR_MAP_049_ConsumableUseRequirements) {
	if (!ec::check_use_item({}, {}, 0).has_value()) {
		GTEST_SKIP() << "PENDING BR-MAP-049: consumable-use requirements (pc.cpp:6276-6442) not extracted";
	}
}

TEST(Economy, BR_MAP_050_StorageCapacity300Vip600) {
	EXPECT_EQ(ec::storage_capacity(false, re()), 300);
	EXPECT_EQ(ec::storage_capacity(true, re()), 600);
}
