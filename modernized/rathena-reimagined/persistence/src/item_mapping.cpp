#include "rathena/persistence/item_mapping.hpp"

#include <array>
#include <limits>
#include <string>

#include "rathena/persistence/errors.hpp"
#include "rathena/persistence/sql_connection.hpp"
#include "rathena/persistence/table_names.hpp"

namespace rathena::persistence {
namespace {

constexpr std::array<ItemTable, 5> kAll = {ItemTable::Inventory, ItemTable::Cart, ItemTable::Storage,
                                           ItemTable::GuildStorage, ItemTable::MailAttachments};

// Column presence taken from sql-files/main.sql (legacy schema, D6 kept until after M3b).
constexpr std::array<ItemTableProfile, 5> kProfiles = {{
    {ItemTable::Inventory, "char_id", false, true, true, true, true},
    {ItemTable::Cart, "char_id", false, true, true, false, false},
    {ItemTable::Storage, "account_id", false, true, true, false, false},
    {ItemTable::GuildStorage, "guild_id", false, true, true, false, false},
    {ItemTable::MailAttachments, "id", true, false, false, false, false},
}};

const std::string& table_name(const TableNames& n, ItemTable t) {
	switch (t) {
	case ItemTable::Inventory: return n.inventory;
	case ItemTable::Cart: return n.cart_inventory;
	case ItemTable::Storage: return n.storage;
	case ItemTable::GuildStorage: return n.guild_storage;
	case ItemTable::MailAttachments: return n.mail_attachments;
	}
	return n.inventory;
}

template <class T>
T get_in_range(const Row& row, const std::string& col) {
	const auto it = row.find(col);
	if (it == row.end()) throw MappingError("item row missing column `" + col + "`");
	std::int64_t s = 0;
	std::uint64_t u = 0;
	bool is_unsigned = false;
	if (const auto* pi = std::get_if<std::int64_t>(&it->second)) {
		s = *pi;
	} else if (const auto* pu = std::get_if<std::uint64_t>(&it->second)) {
		u = *pu;
		is_unsigned = true;
	} else {
		throw MappingError("item column `" + col + "` is NULL or not an integer");
	}
	using L = std::numeric_limits<T>;
	if (is_unsigned) {
		if (u > static_cast<std::uint64_t>(L::max())) throw MappingError("item column `" + col + "` out of range");
		return static_cast<T>(u);
	}
	if (s < 0 && !L::is_signed) throw MappingError("item column `" + col + "` is negative");
	if (L::is_signed) {
		if (s < static_cast<std::int64_t>(L::min()) || s > static_cast<std::int64_t>(L::max()))
			throw MappingError("item column `" + col + "` out of range");
	} else if (static_cast<std::uint64_t>(s) > static_cast<std::uint64_t>(L::max())) {
		throw MappingError("item column `" + col + "` out of range");
	}
	return static_cast<T>(s);
}

SqlValue sv(std::uint64_t v) { return SqlValue{v}; }
SqlValue sv_signed(std::int64_t v) { return SqlValue{v}; }

void require_representable(bool present, std::uint64_t value, const char* field, ItemTable t) {
	if (!present && value != 0)
		throw MappingError(std::string("item field `") + field + "` cannot be stored in table " +
		                   std::string(to_string(t)));
}

} // namespace

std::span<const ItemTable> all_item_tables() noexcept { return kAll; }

std::string_view to_string(ItemTable t) noexcept {
	switch (t) {
	case ItemTable::Inventory: return "inventory";
	case ItemTable::Cart: return "cart_inventory";
	case ItemTable::Storage: return "storage";
	case ItemTable::GuildStorage: return "guild_storage";
	case ItemTable::MailAttachments: return "mail_attachments";
	}
	return "unknown";
}

const ItemTableProfile& profile(ItemTable t) noexcept { return kProfiles[static_cast<std::size_t>(t)]; }

std::vector<std::string> item_columns(ItemTable t) {
	const auto& p = profile(t);
	std::vector<std::string> cols = {"nameid", "amount"};
	if (p.has_equip) cols.emplace_back("equip");
	cols.insert(cols.end(), {"identify", "refine", "attribute"});
	for (std::size_t i = 0; i < kMaxSlots; ++i) cols.push_back("card" + std::to_string(i));
	for (std::size_t i = 0; i < kMaxRandomOptions; ++i) {
		const auto n = std::to_string(i);
		cols.insert(cols.end(), {"option_id" + n, "option_val" + n, "option_parm" + n});
	}
	if (p.has_expire_time) cols.emplace_back("expire_time");
	if (p.has_favorite) cols.emplace_back("favorite");
	cols.insert(cols.end(), {"bound", "unique_id"});
	if (p.has_equip_switch) cols.emplace_back("equip_switch");
	cols.emplace_back("enchantgrade");
	return cols;
}

Row to_row(const Item& it, ItemTable t) {
	const auto& p = profile(t);
	require_representable(p.has_equip, it.equip, "equip", t);
	require_representable(p.has_expire_time, it.expire_time, "expire_time", t);
	require_representable(p.has_favorite, it.favorite, "favorite", t);
	require_representable(p.has_equip_switch, it.equip_switch, "equip_switch", t);

	Row r;
	r["nameid"] = sv(it.nameid);
	r["amount"] = sv(it.amount);
	if (p.has_equip) r["equip"] = sv(it.equip);
	r["identify"] = sv_signed(it.identify);
	r["refine"] = sv(it.refine);
	r["attribute"] = sv(it.attribute);
	for (std::size_t i = 0; i < kMaxSlots; ++i) r["card" + std::to_string(i)] = sv(it.card[i]);
	for (std::size_t i = 0; i < kMaxRandomOptions; ++i) {
		const auto n = std::to_string(i);
		r["option_id" + n] = sv_signed(it.option[i].id);
		r["option_val" + n] = sv_signed(it.option[i].value);
		r["option_parm" + n] = sv_signed(it.option[i].param);
	}
	if (p.has_expire_time) r["expire_time"] = sv(it.expire_time);
	if (p.has_favorite) r["favorite"] = sv(it.favorite);
	r["bound"] = sv(it.bound);
	r["unique_id"] = sv(it.unique_id);
	if (p.has_equip_switch) r["equip_switch"] = sv(it.equip_switch);
	r["enchantgrade"] = sv(it.enchantgrade);
	return r;
}

Item from_row(const Row& row, ItemTable t) {
	const auto& p = profile(t);
	Item it;
	it.nameid = get_in_range<std::uint32_t>(row, "nameid");
	it.amount = get_in_range<std::uint32_t>(row, "amount");
	if (p.has_equip) it.equip = get_in_range<std::uint32_t>(row, "equip");
	it.identify = get_in_range<std::int16_t>(row, "identify");
	it.refine = get_in_range<std::uint8_t>(row, "refine");
	it.attribute = get_in_range<std::uint8_t>(row, "attribute");
	for (std::size_t i = 0; i < kMaxSlots; ++i) it.card[i] = get_in_range<std::uint32_t>(row, "card" + std::to_string(i));
	for (std::size_t i = 0; i < kMaxRandomOptions; ++i) {
		const auto n = std::to_string(i);
		it.option[i].id = get_in_range<std::int16_t>(row, "option_id" + n);
		it.option[i].value = get_in_range<std::int16_t>(row, "option_val" + n);
		it.option[i].param = get_in_range<std::int8_t>(row, "option_parm" + n);
	}
	if (p.has_expire_time) it.expire_time = get_in_range<std::uint32_t>(row, "expire_time");
	if (p.has_favorite) it.favorite = get_in_range<std::uint8_t>(row, "favorite");
	it.bound = get_in_range<std::uint8_t>(row, "bound");
	it.unique_id = get_in_range<std::uint64_t>(row, "unique_id");
	if (p.has_equip_switch) it.equip_switch = get_in_range<std::uint32_t>(row, "equip_switch");
	it.enchantgrade = get_in_range<std::uint8_t>(row, "enchantgrade");
	return it;
}

// Identifiers use the portable double quotes (identical for SQLite and PostgreSQL).
Statement insert_statement(const TableNames& names, ItemTable t, std::uint64_t owner_id, const Item& item,
                           std::optional<std::uint32_t> index) {
	const auto& p = profile(t);
	if (p.has_index_column != index.has_value())
		throw MappingError("index column is required for mail_attachments and only there");
	const Row row = to_row(item, t);
	Statement st;
	std::string cols = quote_identifier(p.owner_column, SqlDialect::Sqlite);
	std::string marks = "?";
	st.params.emplace_back(owner_id);
	if (index) {
		cols += ", " + quote_identifier("index", SqlDialect::Sqlite);
		marks += ", ?";
		st.params.emplace_back(std::uint64_t{*index});
	}
	for (const auto& c : item_columns(t)) {
		cols += ", " + quote_identifier(c, SqlDialect::Sqlite);
		marks += ", ?";
		st.params.push_back(row.at(c));
	}
	st.sql = "INSERT INTO " + quote_identifier(table_name(names, t), SqlDialect::Sqlite) + " (" + cols + ") VALUES (" + marks + ")";
	return st;
}

Statement select_statement(const TableNames& names, ItemTable t, std::uint64_t owner_id) {
	const auto& p = profile(t);
	std::string cols = quote_identifier(p.has_index_column ? "index" : "id", SqlDialect::Sqlite);
	for (const auto& c : item_columns(t)) cols += ", " + quote_identifier(c, SqlDialect::Sqlite);
	Statement st;
	st.sql = "SELECT " + cols + " FROM " + quote_identifier(table_name(names, t), SqlDialect::Sqlite) + " WHERE " +
	         quote_identifier(p.owner_column, SqlDialect::Sqlite) + " = ?";
	st.params.emplace_back(owner_id);
	return st;
}

} // namespace rathena::persistence
