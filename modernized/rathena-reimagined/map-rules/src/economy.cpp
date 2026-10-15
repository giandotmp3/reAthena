#include "rathena/map_rules/economy.hpp"

#include <algorithm>
#include <cmath>

namespace rathena::map_rules::economy {

std::int32_t max_weight(std::int32_t job_base_weight, std::int32_t str, std::int32_t enlarge_weight_lv,
                        std::int32_t modifiers) {
	const std::int64_t w = static_cast<std::int64_t>(job_base_weight) + static_cast<std::int64_t>(str) * 300 +
	                       static_cast<std::int64_t>(std::max(enlarge_weight_lv, 0)) * 2000 + modifiers;
	return static_cast<std::int32_t>(std::clamp<std::int64_t>(w, 0, kMaxZeny));
}

WeightState weight_state(std::int32_t weight, std::int32_t max_w, const BattleConfig& cfg) {
	if (max_w <= 0) {
		return WeightState::Weight90;
	}
	const std::int64_t pct100 = static_cast<std::int64_t>(weight) * 100;
	if (pct100 >= static_cast<std::int64_t>(max_w) * 90) {
		return WeightState::Weight90;
	}
	const std::int64_t w50 = (cfg.mode == Mode::Renewal) ? 70 : 50;
	if (pct100 >= static_cast<std::int64_t>(max_w) * w50) {
		return WeightState::Weight50;
	}
	return WeightState::Normal;
}

bool can_open_box(std::int32_t weight, std::int32_t max_w, std::int32_t free_slots, const BattleConfig& cfg) {
	if (free_slots <= 10) {
		return false;
	}
	if (cfg.mode == Mode::Renewal && static_cast<std::int64_t>(weight) * 100 >= static_cast<std::int64_t>(max_w) * 90) {
		return false;
	}
	return true;
}

std::int32_t drop_adjust(std::int32_t base_rate, std::int32_t rate_adjust, std::int32_t rate_min,
                         std::int32_t rate_max, const BattleConfig& cfg) {
	double rate = base_rate;
	if (cfg.logarithmic_drops && rate_adjust > 0 && rate_adjust != 100 && base_rate > 0) {
		rate = rate * std::pow(5.0 - std::log10(rate), std::log(rate_adjust / 100.0) / std::log(5.0)) + 0.5;
	} else {
		rate = rate * rate_adjust / 100.0;
	}
	rate = std::clamp(rate, static_cast<double>(rate_min), static_cast<double>(rate_max));
	return static_cast<std::int32_t>(rate);
}

std::int32_t drop_rate_with_modifiers(const DropModifierInput& in, const BattleConfig& cfg) {
	std::int64_t rate = in.base_rate;
	if (in.is_player) {
		std::int64_t bonus = 100 + in.bonus_pct;
		std::int64_t cap = cfg.drop_rate_cap;
		if (in.vip) {
			bonus += cfg.vip_drop_increase;
			cap = cfg.drop_rate_cap_vip;
		}
		rate = static_cast<std::int64_t>(0.5 + static_cast<double>(rate) * static_cast<double>(bonus) / 100.0);
		if (rate > cap && in.base_rate < cap) {
			rate = cap;
		}
	}
	rate = std::min<std::int64_t>(rate, 10000);
	rate = std::max<std::int64_t>(rate, cfg.drop_rate0item ? 0 : 1);
	return static_cast<std::int32_t>(rate);
}

bool may_pick_up(const LootRights& r, CharId who, Tick now, const Position& item, const Position& player,
                 const BattleConfig& cfg) {
	if (!within_cells(item, player, 2)) {
		return false;
	}
	const Tick first = r.dropped_at + (r.mvp ? cfg.mvp_item_first_get_time : cfg.item_first_get_time);
	const Tick second = first + (r.mvp ? cfg.mvp_item_second_get_time : cfg.item_second_get_time);
	const Tick third = second + (r.mvp ? cfg.mvp_item_third_get_time : cfg.item_third_get_time);

	Tick wait_until = third;
	if (r.owners[0].value == 0 || r.owners[0] == who) {
		wait_until = 0;
	} else if (r.owners[1].value > 0 && r.owners[1] == who) {
		wait_until = first;
	} else if (r.owners[2].value > 0 && r.owners[2] == who) {
		wait_until = second;
	}
	return now >= wait_until;
}

VendingOpenError validate_vending_open(const VendingOpenRequest& req, const BattleConfig& cfg) {
	if (req.vending_skill_lv <= 0) {
		return VendingOpenError::NoSkill;
	}
	if (!req.has_cart) {
		return VendingOpenError::NoCart;
	}
	if (req.prices.empty()) {
		return VendingOpenError::NoItems;
	}
	if (req.prices.size() > static_cast<std::size_t>(2 + req.vending_skill_lv)) {
		return VendingOpenError::TooManyItems;
	}
	for (const auto p : req.prices) {
		if (p < 1 || p > cfg.vending_max_value) {
			return VendingOpenError::PriceOutOfRange;
		}
	}
	return VendingOpenError::None;
}

std::int64_t vending_tax(std::int64_t total, const BattleConfig& cfg) {
	if (cfg.vending_tax <= 0 || total < cfg.vending_tax_min) {
		return 0;
	}
	return total * cfg.vending_tax / 10000;
}

VendingBuyResult vending_buy(const VendingPurchase& p, const BattleConfig& cfg) {
	VendingBuyResult r;
	r.buyer_zeny = p.buyer_zeny;
	r.seller_zeny = p.seller_zeny;
	if (p.amount <= 0 || p.unit_price < 0) {
		r.error = VendingBuyError::BadAmount;
		return r;
	}
	if (p.amount > p.stock) {
		r.error = VendingBuyError::OutOfStock;
		return r;
	}
	const std::int64_t total = static_cast<std::int64_t>(p.unit_price) * p.amount;
	const auto buyer_after = p.buyer_zeny.debited(total);
	if (!buyer_after) {
		r.error = VendingBuyError::NotEnoughZeny;
		return r;
	}
	if (static_cast<std::int64_t>(p.buyer_weight) + static_cast<std::int64_t>(p.item_weight) * p.amount >
	    p.buyer_max_weight) {
		r.error = VendingBuyError::Overweight;
		return r;
	}
	const std::int64_t tax = vending_tax(total, cfg);
	const std::int64_t proceeds = total - tax;
	if (static_cast<std::int64_t>(p.seller_zeny.value()) + proceeds > kMaxZeny) {
		r.error = VendingBuyError::SellerZenyOverflow; // refuse rather than silently burn zeny
		return r;
	}
	r.buyer_zeny = *buyer_after;
	r.seller_zeny = p.seller_zeny.credited(proceeds);
	r.tax = tax;
	return r;
}

bool can_add_to_stack(std::int32_t current, std::int32_t add) {
	if (add <= 0 || current < 0) {
		return false;
	}
	return static_cast<std::int64_t>(current) + add <= kMaxStackAmount;
}

namespace {
std::int32_t merchant_rate(std::int32_t lv) { return lv > 0 ? 5 + lv * 2 - (lv == 10 ? 1 : 0) : 0; }
} // namespace

std::int32_t npc_buy_price(std::int32_t base_price, std::int32_t discount_lv, std::int32_t compulsion_lv,
                           const BattleConfig& cfg) {
	std::int32_t rate = merchant_rate(discount_lv);
	const std::int32_t rate2 = compulsion_lv > 0 ? 5 + compulsion_lv * 4 : 0;
	rate = std::max(rate, rate2);
	std::int64_t val = base_price;
	if (rate != 0) {
		val = static_cast<std::int64_t>(static_cast<double>(base_price) * (100 - rate) / 100.0);
	}
	return static_cast<std::int32_t>(std::max<std::int64_t>(val, cfg.min_shop_buy));
}

std::int32_t npc_sell_price(std::int32_t base_price, std::int32_t overcharge_lv, const BattleConfig& cfg) {
	const std::int32_t rate = merchant_rate(overcharge_lv);
	std::int64_t val = base_price;
	if (rate != 0) {
		val = static_cast<std::int64_t>(static_cast<double>(base_price) * (100 + rate) / 100.0);
	}
	return static_cast<std::int32_t>(std::clamp<std::int64_t>(val, cfg.min_shop_sell, kMaxZeny));
}

EquipError check_equip(const ItemDef& item, const EquipCandidate& who) {
	if ((item.job_mask & who.job_bit) == 0) {
		return EquipError::WrongJob;
	}
	if (who.base_level < item.equip_level_min) {
		return EquipError::LevelTooLow;
	}
	if (item.equip_level_max > 0 && who.base_level > item.equip_level_max) {
		return EquipError::LevelTooHigh;
	}
	if (item.sex && *item.sex != who.sex) {
		return EquipError::WrongSex;
	}
	if ((item.equip_locations & who.requested_location) == 0) {
		return EquipError::WrongLocation;
	}
	return EquipError::None;
}

bool job_can_use_item(const UsableItem& item, std::uint32_t class_) {
	// Every MAPID_* base job is below 64; legacy shifts unchecked (undefined above 63), here no bit matches.
	const std::uint32_t base = class_ & kMapIdFirstMask;
	const std::uint64_t job = base < 64 ? 1ULL << base : 0;
	std::size_t index = 0; // base job
	if ((class_ & kJobl21) != 0) {
		index = 1;
	} else if ((class_ & kJobl22) != 0) {
		index = 2;
	}
	return (item.class_base[index] & job) != 0;
}

bool item_class_allows(std::uint8_t up, std::uint32_t c, Mode mode) {
	if ((up & kItemJobNormal) != 0 && (c & (kJoblUpper | kJoblBaby | kJoblThird | kJoblFourth)) == 0) {
		return true;
	}
	if (mode == Mode::PreRenewal) {
		if ((up & kItemJobUpper) != 0 && (c & (kJoblUpper | kJoblThird)) != 0) {
			return true; // third jobs may use transcendent items
		}
		if ((up & kItemJobBaby) != 0 && (c & kJoblBaby) != 0) {
			return true;
		}
		// Any third-job item for any third job.
		return (up & (kItemJobThird | kItemJobThirdUpper | kItemJobThirdBaby)) != 0 && (c & kJoblThird) != 0;
	}
	const bool third = (c & kJoblThird) != 0;
	const bool fourth = (c & kJoblFourth) != 0;
	const bool upper = (c & kJoblUpper) != 0;
	const bool baby = (c & kJoblBaby) != 0;
	if ((up & kItemJobUpper) != 0 && upper && !third) {
		return true;
	}
	if ((up & kItemJobBaby) != 0 && baby && !third) {
		return true;
	}
	if ((up & kItemJobThird) != 0 && third && !upper && !baby && !fourth) {
		return true;
	}
	if ((up & kItemJobThirdUpper) != 0 && third && upper && !fourth) {
		return true;
	}
	if ((up & kItemJobThirdBaby) != 0 && third && baby && !fourth) {
		return true;
	}
	return (up & kItemJobFourth) != 0 && fourth;
}

UseItemError check_use_item(const UsableItem& item, const UseItemContext& ctx, const BattleConfig& cfg) {
	using E = UseItemError;
	if (item.type == ItemType::Other) {
		return E::NotConsumable;
	}
	if (ctx.unconditional) {
		return E::None;
	}
	if (ctx.map_no_item_consumption) {
		return E::MapNoItemConsumption;
	}
	// DIFF_TICK(canuseitem_tick, now) > 0
	if (ctx.can_use_item_tick > ctx.now || (item.cash_food && ctx.can_use_cash_food_tick > ctx.now)) {
		return E::UseInterval;
	}
	if (item.sitting_restricted && ctx.sitting && ctx.group_level < item.sitting_override) {
		return E::Sitting;
	}
	if (ctx.storage_open && item.type != ItemType::Cash) {
		return E::StorageOpen;
	}
	if (item.dead_branch && (ctx.map_no_branch || ctx.map_gvg)) {
		return E::DeadBranchMap;
	}
	if (item.no_teleport_group) {
		if (ctx.map_no_teleport || ctx.map_gvg) {
			return E::NoTeleportMap;
		}
		if (ctx.in_duel && !cfg.duel_allow_teleport) {
			return E::DuelNoTeleport;
		}
	}
	if (item.no_return_group) {
		if (ctx.map_no_return) {
			return E::NoReturnMap;
		}
		if (ctx.in_duel && !cfg.duel_allow_teleport) {
			return E::DuelNoTeleport;
		}
	}
	if (item.giant_fly_wing) {
		if (!ctx.in_party || !ctx.party_leader) {
			return E::NotPartyLeader;
		}
		if (!ctx.party_member_alive_on_map) {
			return E::NoPartyMemberOnMap;
		}
	}
	if (item.mercenary_potion != MercenaryPotion::None) {
		if (!ctx.has_mercenary || ctx.mercenary_cant_consume ||
		    (item.mercenary_potion == MercenaryPotion::Awakening && ctx.mercenary_level < 40) ||
		    (item.mercenary_potion == MercenaryPotion::Berserk && ctx.mercenary_level < 80)) {
			return E::MercenaryUnavailable;
		}
	}
	if (item.neuralizer && !ctx.map_reset) {
		return E::NotResetMap;
	}
	if (item.mercenary_scroll && ctx.has_mercenary) {
		return E::MercenaryScroll;
	}
	if (item.box || item.type == ItemType::Cash) {
		if (ctx.weight_pct >= cfg.open_box_weight_rate) {
			return E::Overweight;
		}
		// "The <= is intentional": official servers need 11 free slots.
		if (ctx.free_slots <= 10) {
			return E::InventoryFull;
		}
	}
	if (item.sex && *item.sex != ctx.sex) {
		return E::WrongSex;
	}
	if (item.equip_level_min != 0 && ctx.base_level < item.equip_level_min) {
		return E::LevelTooLow;
	}
	if (item.equip_level_max != 0 && ctx.base_level > item.equip_level_max) {
		return E::LevelTooHigh;
	}
	if (!job_can_use_item(item, ctx.class_)) {
		return E::WrongJob;
	}
	if (ctx.cant_consume) {
		return E::CantConsume;
	}
	if (!item_class_allows(item.class_upper, ctx.class_, cfg.mode)) {
		return E::WrongClassTier;
	}
	return E::None;
}

ItemUseTicks after_item_use(Tick now, bool cash_food, Tick previous_cash_food_tick, const BattleConfig& cfg) {
	return {now + cfg.item_use_interval, cash_food ? now + cfg.cashfood_use_interval : previous_cash_food_tick};
}

std::int32_t storage_capacity(bool vip, const BattleConfig& cfg) {
	const std::int32_t cap = vip ? kMinStorage + cfg.vip_storage_increase : kMinStorage;
	return std::clamp(cap, kMinStorage, kMaxStorage);
}

} // namespace rathena::map_rules::economy
