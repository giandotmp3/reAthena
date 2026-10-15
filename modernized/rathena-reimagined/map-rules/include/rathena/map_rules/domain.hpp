// Pure domain value types used by map rules (architecture §2 `domain/`, spec §2.2).
//
// These are the map-side subset of the shared domain: Zeny (checked, >= 0, <= INT32_MAX),
// ids, Stats, the one Item value type, combat enums and tick arithmetic. No IO, no globals.
#pragma once

#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace rathena::map_rules {

/// Milliseconds on the sim clock (legacy t_tick).
using Tick = std::int64_t;

inline constexpr std::int32_t kMaxZeny = std::numeric_limits<std::int32_t>::max(); // MAX_ZENY
inline constexpr std::int32_t kMaxStackAmount = 30000;                            // MAX_AMOUNT
inline constexpr std::int32_t kMinStorage = 300;                                  // MIN_STORAGE
inline constexpr std::int32_t kMaxStorage = 600;                                  // MAX_STORAGE
inline constexpr std::int32_t kStartAccountNum = 2000000;   // BR-INTER-009
inline constexpr std::int32_t kEndAccountNum = 100000000;
inline constexpr std::int32_t kStartCharNum = 150000;

struct AccountId {
	std::int32_t value = 0;
	friend bool operator==(AccountId, AccountId) = default;
};
struct CharId {
	std::int32_t value = 0;
	friend bool operator==(CharId, CharId) = default;
};

/// Checked zeny amount. Invariant: 0 <= value() <= INT32_MAX (BR-MAP-045).
class Zeny {
public:
	constexpr Zeny() = default;
	/// Clamps into range; use for values that come from trusted persistence.
	static Zeny clamped(std::int64_t v);
	/// Nullopt if `v` is out of range.
	static std::optional<Zeny> checked(std::int64_t v);

	constexpr std::int32_t value() const noexcept { return value_; }

	/// Credit: saturates at INT32_MAX (legacy pc_getzeny clamps rather than failing).
	Zeny credited(std::int64_t amount) const;
	/// Debit: nullopt if amount < 0 or the balance would go negative (never negative).
	std::optional<Zeny> debited(std::int64_t amount) const;

	friend bool operator==(Zeny, Zeny) = default;

private:
	constexpr explicit Zeny(std::int32_t v) : value_(v) {}
	std::int32_t value_ = 0;
};

struct Stats {
	std::int32_t str = 1, agi = 1, vit = 1, int_ = 1, dex = 1, luk = 1;
	// 4th-job traits (renewal only).
	std::int32_t pow = 0, sta = 0, wis = 0, spl = 0, con = 0, crt = 0;
};

enum class StatType : std::uint8_t { Str, Agi, Vit, Int, Dex, Luk };

enum class ClassTier : std::uint8_t { Normal, Transcendent, Third, Fourth };

enum class Element : std::uint8_t { Neutral, Water, Earth, Fire, Wind, Poison, Holy, Shadow, Ghost, Undead };
inline constexpr std::size_t kElementCount = 10;
inline constexpr std::size_t kElementLevels = 4;

enum class Size : std::uint8_t { Small, Medium, Large };
inline constexpr std::size_t kSizeCount = 3;

/// Legacy weapon_type order (W_FIST = 0 ...). Only the order matters for table indexing.
enum class WeaponType : std::uint8_t {
	Fist, Dagger, OneHandSword, TwoHandSword, OneHandSpear, TwoHandSpear, OneHandAxe, TwoHandAxe,
	Mace, TwoHandMace, Staff, Bow, Knuckle, Musical, Whip, Book, Katar, Revolver, Rifle, Gatling,
	Shotgun, Grenade, Huuma, TwoHandStaff,
};
inline constexpr std::size_t kWeaponTypeCount = 24;

enum class Sex : std::uint8_t { Female = 0, Male = 1 };

/// The one Item value type (spec §2.2 "one value type with one persistence mapping").
struct Item {
	std::int32_t nameid = 0;
	std::int32_t amount = 0;
	std::uint32_t equip = 0;
	std::int32_t refine = 0;
	std::array<std::int32_t, 4> card{};
	std::int32_t bound = 0; // 0 = unbound (bound_type)
	std::uint64_t unique_id = 0;
	std::int32_t enchantgrade = 0;
	bool identify = true;
};

/// Static item-db facts needed by trade/equip/use rules.
struct ItemDef {
	std::int32_t nameid = 0;
	std::int32_t weight = 0; // 1/10 weight units, as legacy
	bool stackable = true;
	bool no_trade = false;   // trade restriction flags (item_trade.yml)
	std::uint64_t job_mask = ~0ull;
	std::int32_t equip_level_min = 0;
	std::int32_t equip_level_max = 0; // 0 = no max
	std::optional<Sex> sex;           // nullopt = both
	std::uint32_t equip_locations = 0;
};

/// Spatial position (cells) on one map.
struct Position {
	std::int32_t map_id = 0;
	std::int32_t x = 0;
	std::int32_t y = 0;
};

/// Legacy check_distance: Chebyshev distance on the same map.
bool within_cells(const Position& a, const Position& b, std::int32_t range);

} // namespace rathena::map_rules
