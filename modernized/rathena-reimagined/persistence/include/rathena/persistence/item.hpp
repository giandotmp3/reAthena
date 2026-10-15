#pragma once
// Item: the ONE shared value type for every item-carrying table (spec §2.2, defect D6).
// Field set mirrors legacy `struct item` (common/mmo.hpp:313-331) minus the row id.
#include <array>
#include <cstddef>
#include <cstdint>

namespace rathena::persistence {

inline constexpr std::size_t kMaxSlots = 4;          ///< MAX_SLOTS
inline constexpr std::size_t kMaxRandomOptions = 5;  ///< MAX_ITEM_RDM_OPT

struct RandomOption {
	std::int16_t id = 0;
	std::int16_t value = 0;
	std::int8_t param = 0;
	friend bool operator==(const RandomOption&, const RandomOption&) = default;
};

struct Item {
	std::uint32_t nameid = 0;
	/// INT UNSIGNED in every table after M4 (D3: legacy C++ used int16). Stack limits are a
	/// map rule (BR-MAP-046), not a storage invariant.
	std::uint32_t amount = 0;
	std::uint32_t equip = 0;          ///< equip_pos bitmask
	std::int16_t identify = 0;
	std::uint8_t refine = 0;
	std::uint8_t attribute = 0;
	std::array<std::uint32_t, kMaxSlots> card{};
	std::array<RandomOption, kMaxRandomOptions> option{};
	std::uint32_t expire_time = 0;
	std::uint8_t favorite = 0;
	std::uint8_t bound = 0;
	std::uint64_t unique_id = 0;
	std::uint32_t equip_switch = 0;
	std::uint8_t enchantgrade = 0;

	friend bool operator==(const Item&, const Item&) = default;
};

/// An item as stored in a row (row id is storage identity, not part of the value).
struct StoredItem {
	std::uint64_t row_id = 0;
	Item item;
	friend bool operator==(const StoredItem&, const StoredItem&) = default;
};

} // namespace rathena::persistence
