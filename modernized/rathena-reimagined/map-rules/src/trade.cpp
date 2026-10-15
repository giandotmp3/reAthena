#include "rathena/map_rules/trade.hpp"

#include <algorithm>

namespace rathena::map_rules::economy {

TradeRequestError check_trade_request(const TradeParticipant& requester, const TradeParticipant& target,
                                      bool notrade_map) {
	if (notrade_map) {
		return TradeRequestError::NoTradeMap;
	}
	if (requester.id == target.id) {
		return TradeRequestError::SelfTrade;
	}
	if (target.busy || target.dead || requester.busy || requester.dead) {
		return TradeRequestError::TargetBusy;
	}
	if (target.in_trade) {
		return TradeRequestError::TargetInTrade;
	}
	if (!requester.can_give_items || !target.can_give_items) {
		return TradeRequestError::NotAllowed;
	}
	if (!within_cells(requester.pos, target.pos, kTradeDistance)) {
		return TradeRequestError::TooFar;
	}
	return TradeRequestError::None;
}

TradeSession::TradeSession(CharId requester, CharId target) {
	a_.id = requester;
	b_.id = target;
}

TradeSession::Side* TradeSession::side_of(CharId who) {
	if (who == a_.id) {
		return &a_;
	}
	if (who == b_.id) {
		return &b_;
	}
	return nullptr;
}

TradeError TradeSession::accept(CharId who, const Position& requester_pos, const Position& target_pos) {
	if (phase_ != TradePhase::Requested) {
		return TradeError::WrongState;
	}
	if (!(who == b_.id)) {
		return TradeError::NotParticipant;
	}
	if (!within_cells(requester_pos, target_pos, kTradeDistance)) {
		phase_ = TradePhase::Cancelled;
		return TradeError::TooFar;
	}
	phase_ = TradePhase::Open;
	return TradeError::None;
}

TradeError TradeSession::add_item(CharId who, const Item& item, const ItemDef& def, bool group_can_trade_restricted) {
	Side* s = side_of(who);
	if (s == nullptr) {
		return TradeError::NotParticipant;
	}
	if (phase_ != TradePhase::Open || s->locked) {
		return TradeError::WrongState;
	}
	if (item.amount <= 0) {
		return TradeError::ItemMissing;
	}
	if (def.no_trade && !group_can_trade_restricted) {
		return TradeError::Untradeable;
	}
	if (item.bound != 0 && !group_can_trade_restricted) {
		return TradeError::BoundItem;
	}
	if (s->offers.size() >= kMaxTradeItems) {
		return TradeError::TooManyItems;
	}
	s->offers.push_back({item, def.weight, def.stackable});
	return TradeError::None;
}

TradeError TradeSession::set_zeny(CharId who, std::int64_t amount, Zeny owned) {
	Side* s = side_of(who);
	if (s == nullptr) {
		return TradeError::NotParticipant;
	}
	if (phase_ != TradePhase::Open || s->locked) {
		return TradeError::WrongState;
	}
	if (amount < 0 || amount > owned.value()) {
		return TradeError::BadZeny;
	}
	s->zeny = amount;
	return TradeError::None;
}

TradeError TradeSession::lock(CharId who) {
	Side* s = side_of(who);
	if (s == nullptr) {
		return TradeError::NotParticipant;
	}
	if (phase_ != TradePhase::Open) {
		return TradeError::WrongState;
	}
	s->locked = true;
	return TradeError::None;
}

TradeError TradeSession::confirm(CharId who) {
	Side* s = side_of(who);
	if (s == nullptr) {
		return TradeError::NotParticipant;
	}
	if (phase_ != TradePhase::Open) {
		return TradeError::WrongState;
	}
	if (!a_.locked || !b_.locked) {
		return TradeError::NotLocked;
	}
	s->confirmed = true;
	if (a_.confirmed && b_.confirmed) {
		phase_ = TradePhase::Confirmed;
	}
	return TradeError::None;
}

namespace {

TradeError remove_offers(Wallet& w, const std::vector<TradeOffer>& offers, std::int64_t zeny) {
	for (const auto& o : offers) {
		auto it = std::find_if(w.items.begin(), w.items.end(), [&](const Item& i) {
			return i.nameid == o.item.nameid && i.amount >= o.item.amount &&
			       (o.item.unique_id == 0 || i.unique_id == o.item.unique_id);
		});
		if (it == w.items.end()) {
			return TradeError::ItemMissing;
		}
		it->amount -= o.item.amount;
		w.weight -= o.unit_weight * o.item.amount;
		if (it->amount == 0) {
			w.items.erase(it);
			++w.free_slots;
		}
	}
	const auto after = w.zeny.debited(zeny);
	if (!after) {
		return TradeError::BadZeny;
	}
	w.zeny = *after;
	return TradeError::None;
}

TradeError add_offers(Wallet& w, const std::vector<TradeOffer>& offers, std::int64_t zeny) {
	for (const auto& o : offers) {
		w.weight += o.unit_weight * o.item.amount;
		auto it = std::find_if(w.items.begin(), w.items.end(), [&](const Item& i) {
			return o.stackable && i.nameid == o.item.nameid && i.bound == o.item.bound && i.refine == o.item.refine &&
			       i.card == o.item.card;
		});
		if (it != w.items.end() && static_cast<std::int64_t>(it->amount) + o.item.amount <= kMaxStackAmount) {
			it->amount += o.item.amount;
			continue;
		}
		if (w.free_slots <= 0) {
			return TradeError::NoInventorySpace;
		}
		--w.free_slots;
		w.items.push_back(o.item);
	}
	if (w.weight > w.max_weight) {
		return TradeError::Overweight;
	}
	if (static_cast<std::int64_t>(w.zeny.value()) + zeny > kMaxZeny) {
		return TradeError::ZenyOverflow;
	}
	w.zeny = w.zeny.credited(zeny);
	return TradeError::None;
}

} // namespace

TradeSession::CommitResult TradeSession::commit(const Wallet& requester, const Wallet& target) {
	CommitResult r{TradeError::None, requester, target};
	if (phase_ != TradePhase::Confirmed) {
		r.error = TradeError::NotConfirmed;
		return r;
	}
	// Work on copies; publish only if every step succeeds (all-or-nothing).
	Wallet a = requester;
	Wallet b = target;
	TradeError e = remove_offers(a, a_.offers, a_.zeny);
	if (e == TradeError::None) e = remove_offers(b, b_.offers, b_.zeny);
	if (e == TradeError::None) e = add_offers(a, b_.offers, b_.zeny);
	if (e == TradeError::None) e = add_offers(b, a_.offers, a_.zeny);
	if (e != TradeError::None) {
		phase_ = TradePhase::Cancelled;
		r.error = e;
		return r; // original wallets returned untouched
	}
	phase_ = TradePhase::Committed;
	r.requester = std::move(a);
	r.target = std::move(b);
	return r;
}

} // namespace rathena::map_rules::economy
