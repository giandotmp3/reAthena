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

// --- BR-MAP-049: pc.cpp pc_isUseitem / pc_job_can_use_item / pc_isItemClass / pc_useitem ------

namespace {
using UE = ec::UseItemError;
constexpr std::uint32_t kSwordman = 0x1;                  // MAPID_SWORDMAN
constexpr std::uint32_t kKnight = ec::kJobl21 | kSwordman;  // MAPID_KNIGHT
constexpr std::uint32_t kCrusader = ec::kJobl22 | kSwordman; // MAPID_CRUSADER
} // namespace

TEST(Economy, BR_MAP_049_OnlyConsumablesAndUnconditionalBypass) {
	const ec::UsableItem potion;
	EXPECT_EQ(ec::check_use_item(potion, {}, re()), UE::None);
	EXPECT_EQ(ec::check_use_item({.type = ec::ItemType::Other}, {}, re()), UE::NotConsumable);
	EXPECT_EQ(ec::check_use_item({.type = ec::ItemType::Other}, {.unconditional = true}, re()), UE::NotConsumable);
	EXPECT_EQ(ec::check_use_item(potion, {.map_no_item_consumption = true}, re()), UE::MapNoItemConsumption);
	// PC_PERM_ITEM_UNCONDITIONAL skips every later check.
	EXPECT_EQ(ec::check_use_item({.sex = mr::Sex::Female, .equip_level_min = 99},
	                             {.unconditional = true, .map_no_item_consumption = true}, re()),
	          UE::None);
}

TEST(Economy, BR_MAP_049_UseIntervalAndCashFoodInterval) {
	const ec::UsableItem potion;
	// DIFF_TICK(canuseitem_tick, now) > 0 blocks: 1001 vs now 1000 blocks, 1000 does not.
	EXPECT_EQ(ec::check_use_item(potion, {.now = 1000, .can_use_item_tick = 1001}, re()), UE::UseInterval);
	EXPECT_EQ(ec::check_use_item(potion, {.now = 1000, .can_use_item_tick = 1000}, re()), UE::None);
	// The cash-food tick only gates IG_CASH_FOOD items.
	EXPECT_EQ(ec::check_use_item(potion, {.now = 1000, .can_use_cash_food_tick = 50000}, re()), UE::None);
	EXPECT_EQ(ec::check_use_item({.cash_food = true}, {.now = 1000, .can_use_cash_food_tick = 50000}, re()),
	          UE::UseInterval);
	// pc_useitem: canuseitem_tick = now + item_use_interval (100); cash food also + 60000.
	const auto t = ec::after_item_use(1000, false, 7, re());
	EXPECT_EQ(t.can_use_item, 1100);
	EXPECT_EQ(t.can_use_cash_food, 7);
	EXPECT_EQ(ec::after_item_use(1000, true, 7, re()).can_use_cash_food, 61000);
}

TEST(Economy, BR_MAP_049_SittingStorageAndMapRestrictions) {
	const ec::UsableItem sit_item{.sitting_restricted = true, .sitting_override = 50};
	EXPECT_EQ(ec::check_use_item(sit_item, {.sitting = true, .group_level = 49}, re()), UE::Sitting);
	EXPECT_EQ(ec::check_use_item(sit_item, {.sitting = true, .group_level = 50}, re()), UE::None);
	EXPECT_EQ(ec::check_use_item(sit_item, {.sitting = false}, re()), UE::None);
	// Storage open blocks everything except cash items.
	EXPECT_EQ(ec::check_use_item({}, {.storage_open = true}, re()), UE::StorageOpen);
	EXPECT_EQ(ec::check_use_item({.type = ec::ItemType::Cash}, {.storage_open = true}, re()), UE::None);
	// Dead branch: MF_NOBRANCH or GvG.
	EXPECT_EQ(ec::check_use_item({.dead_branch = true}, {.map_gvg = true}, re()), UE::DeadBranchMap);
	EXPECT_EQ(ec::check_use_item({.dead_branch = true}, {.map_no_branch = true}, re()), UE::DeadBranchMap);
	EXPECT_EQ(ec::check_use_item({.dead_branch = true}, {}, re()), UE::None);
	// Fly Wing (IG_MF_NOTELEPORT): no-teleport or GvG map; in a duel unless duel_allow_teleport.
	const ec::UsableItem fly{.no_teleport_group = true};
	EXPECT_EQ(ec::check_use_item(fly, {.map_gvg = true}, re()), UE::NoTeleportMap);
	EXPECT_EQ(ec::check_use_item(fly, {.map_no_teleport = true}, re()), UE::NoTeleportMap);
	EXPECT_EQ(ec::check_use_item(fly, {.in_duel = true}, re()), UE::DuelNoTeleport);
	auto duel_ok = re();
	duel_ok.duel_allow_teleport = true;
	EXPECT_EQ(ec::check_use_item(fly, {.in_duel = true}, duel_ok), UE::None);
	// Butterfly Wing (IG_MF_NORETURN): GvG alone does not block it.
	const ec::UsableItem butterfly{.no_return_group = true};
	EXPECT_EQ(ec::check_use_item(butterfly, {.map_no_return = true}, re()), UE::NoReturnMap);
	EXPECT_EQ(ec::check_use_item(butterfly, {.map_gvg = true}, re()), UE::None);
	EXPECT_EQ(ec::check_use_item(butterfly, {.in_duel = true}, re()), UE::DuelNoTeleport);
	// Neuralizer only on MF_RESET maps.
	EXPECT_EQ(ec::check_use_item({.neuralizer = true}, {}, re()), UE::NotResetMap);
	EXPECT_EQ(ec::check_use_item({.neuralizer = true}, {.map_reset = true}, re()), UE::None);
}

TEST(Economy, BR_MAP_049_GiantFlyWingAndMercenaryItems) {
	const ec::UsableItem giant{.giant_fly_wing = true};
	EXPECT_EQ(ec::check_use_item(giant, {}, re()), UE::NotPartyLeader); // no party
	EXPECT_EQ(ec::check_use_item(giant, {.in_party = true}, re()), UE::NotPartyLeader);
	EXPECT_EQ(ec::check_use_item(giant, {.in_party = true, .party_leader = true}, re()), UE::NoPartyMemberOnMap);
	EXPECT_EQ(ec::check_use_item(giant, {.in_party = true, .party_leader = true, .party_member_alive_on_map = true},
	                             re()),
	          UE::None);
	const ec::UsableItem red{.mercenary_potion = ec::MercenaryPotion::Basic};
	const ec::UsableItem awake{.mercenary_potion = ec::MercenaryPotion::Awakening};
	const ec::UsableItem berserk{.mercenary_potion = ec::MercenaryPotion::Berserk};
	EXPECT_EQ(ec::check_use_item(red, {}, re()), UE::MercenaryUnavailable);
	EXPECT_EQ(ec::check_use_item(red, {.has_mercenary = true, .mercenary_level = 1}, re()), UE::None);
	EXPECT_EQ(ec::check_use_item(red, {.has_mercenary = true, .mercenary_cant_consume = true}, re()),
	          UE::MercenaryUnavailable);
	EXPECT_EQ(ec::check_use_item(awake, {.has_mercenary = true, .mercenary_level = 39}, re()), UE::MercenaryUnavailable);
	EXPECT_EQ(ec::check_use_item(awake, {.has_mercenary = true, .mercenary_level = 40}, re()), UE::None);
	EXPECT_EQ(ec::check_use_item(berserk, {.has_mercenary = true, .mercenary_level = 79}, re()), UE::MercenaryUnavailable);
	EXPECT_EQ(ec::check_use_item(berserk, {.has_mercenary = true, .mercenary_level = 80}, re()), UE::None);
	// A mercenary scroll is refused while a mercenary is out.
	EXPECT_EQ(ec::check_use_item({.mercenary_scroll = true}, {.has_mercenary = true}, re()), UE::MercenaryScroll);
	EXPECT_EQ(ec::check_use_item({.mercenary_scroll = true}, {}, re()), UE::None);
}

TEST(Economy, BR_MAP_049_BoxesNeedWeightHeadroomAndElevenFreeSlots) {
	const ec::UsableItem box{.box = true};
	const auto pre = mr::BattleConfig::shipped(mr::Mode::PreRenewal);
	// open_box_weight_rate: 90 renewal, 70 pre-renewal (code defaults; the conf line is commented out).
	EXPECT_EQ(ec::check_use_item(box, {.weight_pct = 90}, re()), UE::Overweight);
	EXPECT_EQ(ec::check_use_item(box, {.weight_pct = 89}, re()), UE::None);
	EXPECT_EQ(ec::check_use_item(box, {.weight_pct = 70}, pre), UE::Overweight);
	EXPECT_EQ(ec::check_use_item(box, {.weight_pct = 69}, pre), UE::None);
	// pc_inventoryblank <= 10 fails ("the <= is intentional").
	EXPECT_EQ(ec::check_use_item(box, {.free_slots = 10}, re()), UE::InventoryFull);
	EXPECT_EQ(ec::check_use_item(box, {.free_slots = 11}, re()), UE::None);
	// Cash items get the same checks; ordinary potions do not.
	EXPECT_EQ(ec::check_use_item({.type = ec::ItemType::Cash}, {.free_slots = 0}, re()), UE::InventoryFull);
	EXPECT_EQ(ec::check_use_item({}, {.weight_pct = 100, .free_slots = 0}, re()), UE::None);
}

TEST(Economy, BR_MAP_049_SexLevelJobAndConsumeChecks) {
	EXPECT_EQ(ec::check_use_item({.sex = mr::Sex::Female}, {.sex = mr::Sex::Male}, re()), UE::WrongSex);
	EXPECT_EQ(ec::check_use_item({.sex = mr::Sex::Female}, {.sex = mr::Sex::Female}, re()), UE::None);
	EXPECT_EQ(ec::check_use_item({.equip_level_min = 50}, {.base_level = 49}, re()), UE::LevelTooLow);
	EXPECT_EQ(ec::check_use_item({.equip_level_min = 50}, {.base_level = 50}, re()), UE::None);
	EXPECT_EQ(ec::check_use_item({.equip_level_max = 50}, {.base_level = 51}, re()), UE::LevelTooHigh);
	// pc_job_can_use_item: bit (class_ & 0xff) of class_base[0 base | 1 2-1 job | 2 2-2 job].
	const ec::UsableItem knight_only{.class_base = {0, 1ULL << kSwordman, 0}};
	EXPECT_EQ(ec::check_use_item(knight_only, {.class_ = kKnight}, re()), UE::None);
	EXPECT_EQ(ec::check_use_item(knight_only, {.class_ = kCrusader}, re()), UE::WrongJob);
	EXPECT_EQ(ec::check_use_item(knight_only, {.class_ = kSwordman}, re()), UE::WrongJob);
	EXPECT_EQ(ec::check_use_item({}, {.cant_consume = true}, re()), UE::CantConsume);
	// The first failing check wins: sex is checked before level.
	EXPECT_EQ(ec::check_use_item({.sex = mr::Sex::Female, .equip_level_min = 99}, {}, re()), UE::WrongSex);
}

TEST(Economy, BR_MAP_049_ClassTierRenewalVersusPreRenewal) {
	using mr::Mode;
	constexpr std::uint32_t third = ec::kJoblThird;
	constexpr std::uint32_t third_upper = ec::kJoblThird | ec::kJoblUpper;
	constexpr std::uint32_t fourth = ec::kJoblFourth | ec::kJoblThird | ec::kJoblUpper;
	// Normal items: only classes without upper/baby/third/fourth bits.
	EXPECT_TRUE(ec::item_class_allows(ec::kItemJobNormal, kKnight, Mode::Renewal));
	EXPECT_FALSE(ec::item_class_allows(ec::kItemJobNormal, ec::kJoblUpper | kKnight, Mode::Renewal));
	// Transcendent items: pre-renewal also lets third jobs use them; renewal excludes them.
	EXPECT_TRUE(ec::item_class_allows(ec::kItemJobUpper, ec::kJoblUpper | kKnight, Mode::Renewal));
	EXPECT_FALSE(ec::item_class_allows(ec::kItemJobUpper, third_upper, Mode::Renewal));
	EXPECT_TRUE(ec::item_class_allows(ec::kItemJobUpper, third, Mode::PreRenewal));
	// Third-job items: pre-renewal any third-job bit serves any third job; renewal matches exactly.
	EXPECT_TRUE(ec::item_class_allows(ec::kItemJobThirdBaby, third, Mode::PreRenewal));
	EXPECT_FALSE(ec::item_class_allows(ec::kItemJobThirdBaby, third, Mode::Renewal));
	EXPECT_TRUE(ec::item_class_allows(ec::kItemJobThird, third, Mode::Renewal));
	EXPECT_FALSE(ec::item_class_allows(ec::kItemJobThird, third_upper, Mode::Renewal));
	EXPECT_TRUE(ec::item_class_allows(ec::kItemJobThirdUpper, third_upper, Mode::Renewal));
	// Fourth-job items: renewal only (pre-renewal has no fourth-job rule).
	EXPECT_TRUE(ec::item_class_allows(ec::kItemJobFourth, fourth, Mode::Renewal));
	EXPECT_FALSE(ec::item_class_allows(ec::kItemJobThirdUpper, fourth, Mode::Renewal));
	EXPECT_FALSE(ec::item_class_allows(ec::kItemJobFourth, fourth, Mode::PreRenewal));
	// Through check_use_item, as the last check.
	const ec::UsableItem trans_only{.class_upper = ec::kItemJobUpper};
	EXPECT_EQ(ec::check_use_item(trans_only, {.class_ = third}, re()), UE::WrongClassTier);
	EXPECT_EQ(ec::check_use_item(trans_only, {.class_ = third}, pre_re()), UE::None);
}

TEST(Economy, BR_MAP_050_StorageCapacity300Vip600) {
	EXPECT_EQ(ec::storage_capacity(false, re()), 300);
	EXPECT_EQ(ec::storage_capacity(true, re()), 600);
}
