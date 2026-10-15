// rules/economy (trade): CAP-18 player-to-player trade, BR-MAP-038..041.
// Legacy home: src/map/trade.cpp. The rebuild models trade as an explicit two-phase commit:
// both sides lock, both confirm, then one all-or-nothing swap over value-typed wallets.
#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "rathena/map_rules/battle_config.hpp"
#include "rathena/map_rules/domain.hpp"

namespace rathena::map_rules::economy {

inline constexpr std::int32_t kTradeDistance = 2; // TRADE_DISTANCE (trade.cpp:24)
inline constexpr std::size_t kMaxTradeItems = 10;

struct TradeParticipant {
	CharId id;
	Position pos;
	bool can_give_items = true; // group permission (pc_can_give_items)
	bool busy = false;          // vending / buying store / NPC / chat room / pending invite
	bool in_trade = false;
	bool dead = false;
};

enum class TradeRequestError : std::uint8_t { None, NoTradeMap, SelfTrade, TooFar, TargetBusy, TargetInTrade, NotAllowed };
/// BR-MAP-038: no-trade map, self, distance (same map, <= 2 cells), state and group permission.
TradeRequestError check_trade_request(const TradeParticipant& requester, const TradeParticipant& target,
                                      bool notrade_map);

/// A side's possessions at commit time (value snapshot from the session).
struct Wallet {
	Zeny zeny;
	std::vector<Item> items;
	std::int32_t weight = 0;
	std::int32_t max_weight = 0;
	std::int32_t free_slots = 0;
};

struct TradeOffer {
	Item item;
	std::int32_t unit_weight = 0;
	bool stackable = true;
};

enum class TradeError : std::uint8_t {
	None,
	WrongState,
	NotParticipant,
	TooFar,
	Untradeable,     // BR-MAP-040
	BoundItem,       // BR-MAP-040
	TooManyItems,
	BadZeny,         // BR-MAP-040: negative or more than owned
	ZenyOverflow,    // BR-MAP-040: receiver would exceed INT32_MAX
	ItemMissing,     // offered item no longer owned at commit
	Overweight,
	NoInventorySpace,
	NotLocked,
	NotConfirmed,
};

enum class TradePhase : std::uint8_t { Requested, Open, Confirmed, Committed, Cancelled };

/// BR-MAP-039 / 041: trade session state machine.
class TradeSession {
public:
	TradeSession(CharId requester, CharId target);

	TradePhase phase() const noexcept { return phase_; }

	/// BR-MAP-039: only the target may accept; both must still be in range.
	TradeError accept(CharId who, const Position& requester_pos, const Position& target_pos);
	/// BR-MAP-040: untradeable / bound items rejected unless the group may trade them.
	TradeError add_item(CharId who, const Item& item, const ItemDef& def, bool group_can_trade_restricted = false);
	/// BR-MAP-040: zeny offer must be 0 <= amount <= owned.
	TradeError set_zeny(CharId who, std::int64_t amount, Zeny owned);
	/// Phase 1a: "OK" locks the side's offer; a locked side cannot change its offer.
	TradeError lock(CharId who);
	/// Phase 1b: "Trade" confirms; requires both sides locked.
	TradeError confirm(CharId who);
	void cancel() { phase_ = TradePhase::Cancelled; }

	struct CommitResult {
		TradeError error = TradeError::None;
		Wallet requester;
		Wallet target;
	};
	/// Phase 2 (BR-MAP-041): atomic swap. On any failure neither wallet changes.
	CommitResult commit(const Wallet& requester, const Wallet& target);

private:
	struct Side {
		CharId id;
		std::vector<TradeOffer> offers;
		std::int64_t zeny = 0;
		bool locked = false;
		bool confirmed = false;
	};
	Side* side_of(CharId who);

	Side a_;
	Side b_;
	TradePhase phase_ = TradePhase::Requested;
};

} // namespace rathena::map_rules::economy
