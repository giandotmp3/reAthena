// rules/economy: CAP-17 items/economy and CAP-18 vending, BR-MAP-001..003, 035..037, 042..050.
// Player-to-player trade (BR-MAP-038..041) lives in trade.hpp in the same rule group.
// Legacy homes: src/map/pc.cpp, status.cpp (weight), mob.cpp (drops), vending.cpp, chrif.cpp.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "rathena/map_rules/battle_config.hpp"
#include "rathena/map_rules/domain.hpp"

namespace rathena::map_rules::economy {

// --- Weight -----------------------------------------------------------------------------------

/// BR-MAP-001: job base + STR x 300 + Enlarge Weight Limit x 2000 + modifiers (1/10 weight units).
std::int32_t max_weight(std::int32_t job_base_weight, std::int32_t str, std::int32_t enlarge_weight_lv,
                        std::int32_t modifiers = 0);

enum class WeightState : std::uint8_t { Normal, Weight50, Weight90 };
/// BR-MAP-002: >= 70% (RE) / 50% (pre-RE) -> WEIGHT50; >= 90% -> WEIGHT90.
WeightState weight_state(std::int32_t weight, std::int32_t max_weight, const BattleConfig& cfg);

/// BR-MAP-003: opening a box is blocked at >= 90% weight (RE only) or <= 10 free inventory slots.
bool can_open_box(std::int32_t weight, std::int32_t max_weight, std::int32_t free_slots, const BattleConfig& cfg);

// --- Drops ------------------------------------------------------------------------------------

/// BR-MAP-035: global rate scaling (item_rate_*), linear or logarithmic, clamped to [min, max].
/// Rates are in 1/10000 (100 = 1%).
std::int32_t drop_adjust(std::int32_t base_rate, std::int32_t rate_adjust, std::int32_t rate_min,
                         std::int32_t rate_max, const BattleConfig& cfg);

struct DropModifierInput {
	std::int32_t base_rate = 0;   // after drop_adjust
	std::int32_t bonus_pct = 0;   // summed class/race/SC bonuses, additive percent
	bool vip = false;
	bool is_player = true;
};
/// BR-MAP-036: rate x (100 + bonus)% rounded; capped at drop_rate_cap (90%) unless the base was
/// already above it; final cap 100%; floor 0.01% (unless drop_rate0item). 1% + 100% -> 2%.
std::int32_t drop_rate_with_modifiers(const DropModifierInput& in, const BattleConfig& cfg);

struct LootRights {
	std::array<CharId, 3> owners{}; // top attackers; 0 = none
	Tick dropped_at = 0;
	bool mvp = false;
};
/// BR-MAP-037: loot priority windows 3 s / 2 s / 2 s (MVP 10 s / 10 s / 2 s); pickup range 2 cells.
/// Party-share loot priority is not yet extracted (handled by the legacy call site).
bool may_pick_up(const LootRights& rights, CharId who, Tick now, const Position& item, const Position& player,
                 const BattleConfig& cfg);

// --- Vending ----------------------------------------------------------------------------------

struct VendingOpenRequest {
	std::int32_t vending_skill_lv = 0; // MC_VENDING
	bool has_cart = false;
	std::vector<std::int32_t> prices;  // one per listed item
};
enum class VendingOpenError : std::uint8_t { None, NoSkill, NoCart, NoItems, TooManyItems, PriceOutOfRange };
/// BR-MAP-042: needs MC_VENDING and a cart; up to 2 + skill level items; 1 <= price <= vending_max_value.
VendingOpenError validate_vending_open(const VendingOpenRequest& req, const BattleConfig& cfg);

struct VendingPurchase {
	std::int32_t unit_price = 0;
	std::int32_t amount = 0;
	std::int32_t stock = 0;
	std::int32_t item_weight = 0;   // per unit
	Zeny buyer_zeny;
	std::int32_t buyer_weight = 0;
	std::int32_t buyer_max_weight = 0;
	Zeny seller_zeny;
};
enum class VendingBuyError : std::uint8_t { None, BadAmount, OutOfStock, NotEnoughZeny, Overweight, SellerZenyOverflow };
struct VendingBuyResult {
	VendingBuyError error = VendingBuyError::None;
	Zeny buyer_zeny;
	Zeny seller_zeny;
	std::int64_t tax = 0;
};
/// BR-MAP-043: stock, zeny and weight checks; BR-MAP-044: tax applied to the seller's proceeds.
VendingBuyResult vending_buy(const VendingPurchase& p, const BattleConfig& cfg);

/// BR-MAP-044: tax = total x vending_tax/10000 when total >= vending_tax_min (5% on >= 100M).
std::int64_t vending_tax(std::int64_t total, const BattleConfig& cfg);

// --- Stacks, NPC prices, storage --------------------------------------------------------------

/// BR-MAP-046: an inventory stack can hold at most 30,000.
bool can_add_to_stack(std::int32_t current, std::int32_t add);

/// BR-MAP-047: Discount (MC_DISCOUNT 5+2lv, -1 at lv10; RG_COMPULSION 5+4lv; best wins), min min_shop_buy.
std::int32_t npc_buy_price(std::int32_t base_price, std::int32_t discount_lv, std::int32_t compulsion_lv,
                           const BattleConfig& cfg);
/// BR-MAP-047: Overcharge (MC_OVERCHARGE 5+2lv, -1 at lv10), min min_shop_sell.
std::int32_t npc_sell_price(std::int32_t base_price, std::int32_t overcharge_lv, const BattleConfig& cfg);

struct EquipCandidate {
	std::uint64_t job_bit = 1;
	std::int32_t base_level = 1;
	Sex sex = Sex::Male;
	std::uint32_t requested_location = 0;
};
enum class EquipError : std::uint8_t { None, WrongJob, LevelTooLow, LevelTooHigh, WrongSex, WrongLocation };
/// BR-MAP-048: job, level, sex and location requirements.
EquipError check_equip(const ItemDef& item, const EquipCandidate& who);

/// BR-MAP-049 (pending): consumable-use requirements; nullopt until extracted.
enum class UseItemError : std::uint8_t { None, WrongJob, LevelTooLow, Restricted, Cooldown };
std::optional<UseItemError> check_use_item(const ItemDef& item, const EquipCandidate& who, Tick now);

/// BR-MAP-050: personal storage 300 (MIN_STORAGE), VIP + vip_storage_increase, max 600.
std::int32_t storage_capacity(bool vip, const BattleConfig& cfg);

} // namespace rathena::map_rules::economy
