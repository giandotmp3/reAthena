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

/// BR-MAP-002/003: pc_getpercentweight, bit for bit: [weight x 100 / max(max_weight, 1)] computed in
/// uint32 (legacy sd->weight and sd->max_weight are uint32; the product wraps) and truncated to uint16.
std::uint16_t weight_percent(std::int32_t weight, std::int32_t max_weight);

enum class WeightState : std::uint8_t { Normal, Weight50, Weight90 };
/// BR-MAP-002 (pc_updateweightstatus): weight_percent >= major_overweight_rate (90) -> WEIGHT90;
/// >= natural_heal_weight_rate (70 RE / 50 pre-RE) -> WEIGHT50.
WeightState weight_state(std::int32_t weight, std::int32_t max_weight, const BattleConfig& cfg);

/// BR-MAP-003: opening a box (or using a cash item) is blocked when weight_percent is
/// >= open_box_weight_rate (shipped: 90 renewal, 70 pre-renewal), or with <= 10 free inventory
/// slots. pc.cpp pc_isUseitem.
bool can_open_box(std::int32_t weight, std::int32_t max_weight, std::int32_t free_slots, const BattleConfig& cfg);

// --- Drops ------------------------------------------------------------------------------------

/// BR-MAP-035: global rate scaling (item_rate_*), linear or logarithmic, clamped to [min, max].
/// Rates are in 1/10000 (100 = 1%). The clamp is legacy cap_value (the max bound is tested first,
/// so a misconfigured min > max yields max) and the result is converted as (uint32)rate.
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

/// BR-MAP-046: an inventory stack can hold at most 30,000: a positive `add` fits when
/// current + add <= 30,000 (legacy `amount > MAX_AMOUNT - current` refuses).
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

// --- Using consumables (pc.cpp pc_isUseitem) ---------------------------------------------------

/// Legacy item_types that matter here; every other type is not consumable.
enum class ItemType : std::uint8_t { Healing, Usable, Cash, Other };

/// Legacy class_ bits (map.hpp) used by the job and class-tier checks.
inline constexpr std::uint32_t kMapIdFirstMask = 0xff; // MAPID_FIRSTMASK
inline constexpr std::uint32_t kJobl21 = 0x100;        // JOBL_2_1
inline constexpr std::uint32_t kJobl22 = 0x200;        // JOBL_2_2
inline constexpr std::uint32_t kJoblThird = 0x1000;    // JOBL_THIRD
inline constexpr std::uint32_t kJoblFourth = 0x10000;  // JOBL_FOURTH
inline constexpr std::uint32_t kJoblUpper = 0x100000;  // JOBL_UPPER
inline constexpr std::uint32_t kJoblBaby = 0x200000;   // JOBL_BABY

/// Legacy e_item_job (itemdb.hpp): the item_db `Classes` bits.
inline constexpr std::uint8_t kItemJobNormal = 0x01;
inline constexpr std::uint8_t kItemJobUpper = 0x02;
inline constexpr std::uint8_t kItemJobBaby = 0x04;
inline constexpr std::uint8_t kItemJobThird = 0x08;
inline constexpr std::uint8_t kItemJobThirdUpper = 0x10;
inline constexpr std::uint8_t kItemJobThirdBaby = 0x20;
inline constexpr std::uint8_t kItemJobFourth = 0x40;
inline constexpr std::uint8_t kItemJobAll = 0x7f;

/// Mercenary potions (pc_isUseitem `switch (nameid)`).
enum class MercenaryPotion : std::uint8_t {
	None,
	Basic,     // ITEMID_MERCENARY_RED_POTION, _BLUE_POTION, ITEMID_M_CENTER_POTION
	Awakening, // ITEMID_M_AWAKENING_POTION: mercenary level >= 40
	Berserk,   // ITEMID_M_BERSERK_POTION: mercenary level >= 80
};

/// The item_db / item_group facts pc_isUseitem looks at (the caller resolves them from the db).
struct UsableItem {
	ItemType type = ItemType::Usable;
	bool cash_food = false;          // in IG_CASH_FOOD
	bool sitting_restricted = false; // item_usage.sitting
	std::int32_t sitting_override = 100; // item_usage.override (group level that ignores it)
	bool dead_branch = false;        // flag.dead_branch
	bool no_teleport_group = false;  // in IG_MF_NOTELEPORT (fly wings, ...)
	bool no_return_group = false;    // in IG_MF_NORETURN (butterfly wings, ...)
	bool giant_fly_wing = false;     // in IG_GIANT_FLY_WING
	MercenaryPotion mercenary_potion = MercenaryPotion::None;
	bool neuralizer = false;         // ITEMID_NEURALIZER
	bool mercenary_scroll = false;   // in IG_MERCENARY
	bool box = false;                // flag.group (item boxes)
	std::optional<Sex> sex{};        // nullopt = both
	std::int32_t equip_level_min = 0; // elv (0 = none)
	std::int32_t equip_level_max = 0; // elvmax (0 = none)
	std::array<std::uint64_t, 3> class_base{~0ull, ~0ull, ~0ull}; // job bits for base / 2-1 / 2-2 jobs
	std::uint8_t class_upper = kItemJobAll;
};

/// The player and map state pc_isUseitem looks at.
struct UseItemContext {
	bool unconditional = false;       // PC_PERM_ITEM_UNCONDITIONAL
	Tick now = 0;
	Tick can_use_item_tick = 0;       // sd->canuseitem_tick
	Tick can_use_cash_food_tick = 0;  // sd->canusecashfood_tick
	bool sitting = false;
	std::int32_t group_level = 0;
	bool storage_open = false;        // sd->state.storage_flag
	bool in_duel = false;
	// Map flags.
	bool map_no_item_consumption = false;
	bool map_no_branch = false;
	bool map_gvg = false;             // mapdata_flag_gvg2 (GvG, WoE castles, battlegrounds-as-gvg)
	bool map_no_teleport = false;
	bool map_no_return = false;
	bool map_reset = false;           // MF_RESET
	// Party (Giant Fly Wing).
	bool in_party = false;
	bool party_leader = false;
	bool party_member_alive_on_map = false; // another online, living member on the same map
	// Mercenary.
	bool has_mercenary = false;
	bool mercenary_cant_consume = false;
	std::int32_t mercenary_level = 0; // mercenary db level
	// Boxes.
	std::int32_t weight_pct = 0;      // pc_getpercentweight: weight x 100 / max_weight
	std::int32_t free_slots = 100;    // pc_inventoryblank
	// Character.
	Sex sex = Sex::Male;
	std::int32_t base_level = 1;
	std::uint32_t class_ = 0;         // sd->class_ (MAPID_* with JOBL_* bits)
	bool cant_consume = false;        // sc.cant.consume
};

enum class UseItemError : std::uint8_t {
	None,
	NotConsumable,
	MapNoItemConsumption,
	UseInterval,         // within item_use_interval (or cashfood_use_interval for cash food)
	Sitting,
	StorageOpen,
	DeadBranchMap,
	NoTeleportMap,
	DuelNoTeleport,
	NoReturnMap,
	NotPartyLeader,      // also "not in a party" (same client message)
	NoPartyMemberOnMap,
	MercenaryUnavailable, // no mercenary, it cannot consume, or its level is too low
	NotResetMap,         // Neuralizer outside an MF_RESET map
	MercenaryScroll,     // already has a mercenary
	Overweight,          // boxes / cash items: weight >= open_box_weight_rate
	InventoryFull,       // boxes / cash items: <= 10 free slots
	WrongSex,
	LevelTooLow,
	LevelTooHigh,
	WrongJob,
	CantConsume,
	WrongClassTier,
};

/// BR-MAP-049: pc_isUseitem, check by check in legacy order (the first failure is returned).
/// Renewal and pre-renewal differ only in the class-tier check (`item_class_allows`).
/// A dead-branch item that passes is logged by the caller (log_branch).
UseItemError check_use_item(const UsableItem& item, const UseItemContext& ctx, const BattleConfig& cfg);

/// BR-MAP-049: pc_job_can_use_item: bit (class_ & MAPID_FIRSTMASK) of class_base[0 base | 1 2-1 | 2 2-2].
bool job_can_use_item(const UsableItem& item, std::uint32_t class_);

/// BR-MAP-049: pc_isItemClass. Pre-renewal lets third jobs use transcendent items and every third
/// job use any third-job item; renewal matches each tier exactly (fourth jobs only ITEMJ_FOURTH).
bool item_class_allows(std::uint8_t class_upper, std::uint32_t class_, Mode mode);

struct ItemUseTicks {
	Tick can_use_item = 0;
	Tick can_use_cash_food = 0;
};
/// BR-MAP-049: pc_useitem stamps the next allowed use: now + item_use_interval, and for cash food
/// also now + cashfood_use_interval (otherwise the cash-food tick is unchanged).
ItemUseTicks after_item_use(Tick now, bool cash_food, Tick previous_cash_food_tick, const BattleConfig& cfg);

/// BR-MAP-050: personal storage 300 (MIN_STORAGE), VIP + vip_storage_increase, max 600.
std::int32_t storage_capacity(bool vip, const BattleConfig& cfg);

} // namespace rathena::map_rules::economy
