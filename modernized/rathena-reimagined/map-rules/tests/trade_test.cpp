// Acceptance tests: rules/economy (trade), BR-MAP-038..041 (spec §5.4, CAP-18).
#include <gtest/gtest.h>

#include "rathena/map_rules/trade.hpp"

namespace mr = rathena::map_rules;
namespace ec = rathena::map_rules::economy;

namespace {

const mr::CharId kA{150001};
const mr::CharId kB{150002};

mr::ItemDef def(std::int32_t nameid, std::int32_t weight = 0, bool no_trade = false) {
	mr::ItemDef d;
	d.nameid = nameid;
	d.weight = weight;
	d.no_trade = no_trade;
	return d;
}

ec::TradeParticipant participant(mr::CharId id, std::int32_t x) {
	return {.id = id, .pos = {1, x, 0}};
}

mr::Item item(std::int32_t nameid, std::int32_t amount) {
	mr::Item i;
	i.nameid = nameid;
	i.amount = amount;
	return i;
}

ec::Wallet wallet(std::int32_t zeny, std::vector<mr::Item> items) {
	return {*mr::Zeny::checked(zeny), std::move(items), 0, 10000, 10};
}

ec::TradeSession open_session() {
	ec::TradeSession s{kA, kB};
	EXPECT_EQ(s.accept(kB, {1, 0, 0}, {1, 1, 0}), ec::TradeError::None);
	return s;
}

} // namespace

TEST(Trade, BR_MAP_038_RequestPreconditions) {
	const auto a = participant(kA, 0);
	EXPECT_EQ(ec::check_trade_request(a, participant(kB, 2), false), ec::TradeRequestError::None);
	EXPECT_EQ(ec::check_trade_request(a, participant(kB, 3), false), ec::TradeRequestError::TooFar);
	EXPECT_EQ(ec::check_trade_request(a, participant(kB, 1), true), ec::TradeRequestError::NoTradeMap);
	EXPECT_EQ(ec::check_trade_request(a, a, false), ec::TradeRequestError::SelfTrade);
	auto busy = participant(kB, 1);
	busy.busy = true;
	EXPECT_EQ(ec::check_trade_request(a, busy, false), ec::TradeRequestError::TargetBusy);
	auto trading = participant(kB, 1);
	trading.in_trade = true;
	EXPECT_EQ(ec::check_trade_request(a, trading, false), ec::TradeRequestError::TargetInTrade);
	auto gm = participant(kB, 1);
	gm.can_give_items = false;
	EXPECT_EQ(ec::check_trade_request(a, gm, false), ec::TradeRequestError::NotAllowed);
}

TEST(Trade, BR_MAP_039_OnlyTargetAcceptsAndMustStillBeInRange) {
	ec::TradeSession s{kA, kB};
	EXPECT_EQ(s.accept(kA, {1, 0, 0}, {1, 1, 0}), ec::TradeError::NotParticipant);
	EXPECT_EQ(s.accept(kB, {1, 0, 0}, {1, 5, 0}), ec::TradeError::TooFar);
	EXPECT_EQ(s.phase(), ec::TradePhase::Cancelled);

	ec::TradeSession ok{kA, kB};
	EXPECT_EQ(ok.accept(kB, {1, 0, 0}, {1, 1, 0}), ec::TradeError::None);
	EXPECT_EQ(ok.phase(), ec::TradePhase::Open);
}

TEST(Trade, BR_MAP_040_UntradeableAndBoundItemsRejected) {
	auto s = open_session();
	const auto no_trade = def(7000, 0, true);
	EXPECT_EQ(s.add_item(kA, item(7000, 1), no_trade), ec::TradeError::Untradeable);
	auto bound = item(1201, 1);
	bound.bound = 1;
	EXPECT_EQ(s.add_item(kA, bound, def(1201)), ec::TradeError::BoundItem);
	EXPECT_EQ(s.add_item(kA, item(1201, 1), def(1201)), ec::TradeError::None);
}

TEST(Trade, BR_MAP_040_ZenyOfferLimits) {
	auto s = open_session();
	const auto owned = *mr::Zeny::checked(1000);
	EXPECT_EQ(s.set_zeny(kA, -1, owned), ec::TradeError::BadZeny);
	EXPECT_EQ(s.set_zeny(kA, 1001, owned), ec::TradeError::BadZeny);
	EXPECT_EQ(s.set_zeny(kA, 1000, owned), ec::TradeError::None);
}

TEST(Trade, BR_MAP_040_ReceiverZenyOverflowAbortsTrade) {
	auto s = open_session();
	ASSERT_EQ(s.set_zeny(kA, 1000, *mr::Zeny::checked(1000)), ec::TradeError::None);
	ASSERT_EQ(s.lock(kA), ec::TradeError::None);
	ASSERT_EQ(s.lock(kB), ec::TradeError::None);
	ASSERT_EQ(s.confirm(kA), ec::TradeError::None);
	ASSERT_EQ(s.confirm(kB), ec::TradeError::None);
	const auto a = wallet(1000, {});
	const auto b = wallet(mr::kMaxZeny - 10, {});
	const auto r = s.commit(a, b);
	EXPECT_EQ(r.error, ec::TradeError::ZenyOverflow);
	EXPECT_EQ(r.requester.zeny.value(), 1000);          // untouched
	EXPECT_EQ(r.target.zeny.value(), mr::kMaxZeny - 10); // untouched
}

TEST(Trade, BR_MAP_041_TwoPhaseCommitLockConfirmSwap) {
	auto s = open_session();
	ASSERT_EQ(s.add_item(kA, item(501, 5), def(501, 70)), ec::TradeError::None);
	ASSERT_EQ(s.set_zeny(kB, 300, *mr::Zeny::checked(500)), ec::TradeError::None);

	EXPECT_EQ(s.confirm(kA), ec::TradeError::NotLocked); // cannot confirm before both lock
	ASSERT_EQ(s.lock(kA), ec::TradeError::None);
	EXPECT_EQ(s.add_item(kA, item(502, 1), def(502)), ec::TradeError::WrongState); // locked offer is frozen
	ASSERT_EQ(s.lock(kB), ec::TradeError::None);

	EXPECT_EQ(s.commit(wallet(0, {item(501, 10)}), wallet(500, {})).error, ec::TradeError::NotConfirmed);
	ASSERT_EQ(s.confirm(kA), ec::TradeError::None);
	ASSERT_EQ(s.confirm(kB), ec::TradeError::None);
	ASSERT_EQ(s.phase(), ec::TradePhase::Confirmed);

	const auto r = s.commit(wallet(0, {item(501, 10)}), wallet(500, {}));
	ASSERT_EQ(r.error, ec::TradeError::None);
	EXPECT_EQ(s.phase(), ec::TradePhase::Committed);
	EXPECT_EQ(r.requester.zeny.value(), 300);
	ASSERT_EQ(r.requester.items.size(), 1u);
	EXPECT_EQ(r.requester.items[0].amount, 5);
	EXPECT_EQ(r.target.zeny.value(), 200);
	ASSERT_EQ(r.target.items.size(), 1u);
	EXPECT_EQ(r.target.items[0].nameid, 501);
	EXPECT_EQ(r.target.items[0].amount, 5);
}

TEST(Trade, BR_MAP_041_CommitIsAllOrNothingWhenItemMissing) {
	auto s = open_session();
	ASSERT_EQ(s.add_item(kA, item(501, 5), def(501)), ec::TradeError::None);
	ASSERT_EQ(s.set_zeny(kB, 300, *mr::Zeny::checked(500)), ec::TradeError::None);
	ASSERT_EQ(s.lock(kA), ec::TradeError::None);
	ASSERT_EQ(s.lock(kB), ec::TradeError::None);
	ASSERT_EQ(s.confirm(kA), ec::TradeError::None);
	ASSERT_EQ(s.confirm(kB), ec::TradeError::None);
	// The requester dropped the potions between confirm and commit (duplication attempt).
	const auto r = s.commit(wallet(0, {item(501, 2)}), wallet(500, {}));
	EXPECT_EQ(r.error, ec::TradeError::ItemMissing);
	EXPECT_EQ(r.target.zeny.value(), 500);
	EXPECT_TRUE(r.target.items.empty());
	EXPECT_EQ(s.phase(), ec::TradePhase::Cancelled);
}
