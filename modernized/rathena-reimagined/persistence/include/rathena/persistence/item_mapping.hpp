#pragma once
// The ONE persistence mapping for Item, reused by the five live item tables (D6 handled in code;
// schema consolidation is deferred until after M3b, architecture §5).
// `auction` is not mapped: it is archived and dropped by M7 (HITL #1 drop decision 1).
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "rathena/persistence/item.hpp"
#include "rathena/persistence/row.hpp"

namespace rathena::persistence {

struct TableNames;

enum class ItemTable { Inventory, Cart, Storage, GuildStorage, MailAttachments };

std::span<const ItemTable> all_item_tables() noexcept;
std::string_view to_string(ItemTable t) noexcept;

/// Which optional Item columns a legacy table carries (they differ: e.g. mail_attachments has
/// no `equip`/`expire_time`; only inventory has `favorite`/`equip_switch`).
struct ItemTableProfile {
	ItemTable table;
	std::string_view owner_column;      ///< char_id / account_id / guild_id / id (mail id)
	bool has_index_column;              ///< mail_attachments (`index` within the mail)
	bool has_equip;
	bool has_expire_time;
	bool has_favorite;
	bool has_equip_switch;
};

const ItemTableProfile& profile(ItemTable t) noexcept;

/// Item value columns present in `t`, in a stable order (no row id, owner or index column).
std::vector<std::string> item_columns(ItemTable t);

/// Item -> row for table `t`. Strict: throws MappingError if a non-zero field cannot be stored
/// in `t` (e.g. an equipped item into mail_attachments), so nothing is silently dropped.
Row to_row(const Item& item, ItemTable t);

/// Row -> Item for table `t`. Columns absent from `t` read as 0. Throws MappingError on a missing
/// required column, a NULL, or a value outside the field's range.
Item from_row(const Row& row, ItemTable t);

/// Parameterized INSERT for one item row: owner (+ index) first, then item_columns(t).
Statement insert_statement(const TableNames& names, ItemTable t, std::uint64_t owner_id,
                           const Item& item, std::optional<std::uint32_t> index = std::nullopt);

/// Parameterized SELECT of all items for one owner.
Statement select_statement(const TableNames& names, ItemTable t, std::uint64_t owner_id);

} // namespace rathena::persistence
