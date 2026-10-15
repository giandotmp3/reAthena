#include "rathena/persistence/table_names.hpp"
#include "rathena/persistence/sql_connection.hpp"

#include <array>
#include <stdexcept>
#include <utility>

namespace rathena::persistence {
namespace {

using Field = std::string TableNames::*;
// Legacy inter_athena.conf key -> field. Only keys the persistence repositories use.
constexpr std::array<std::pair<std::string_view, Field>, 33> kKeys = {{
    {"login_server_account_db", &TableNames::login},
    {"global_acc_reg_num_table", &TableNames::global_acc_reg_num},
    {"global_acc_reg_str_table", &TableNames::global_acc_reg_str},
    {"acc_reg_num_table", &TableNames::acc_reg_num},
    {"acc_reg_str_table", &TableNames::acc_reg_str},
    {"char_db", &TableNames::character},
    {"char_reg_num_table", &TableNames::char_reg_num},
    {"char_reg_str_table", &TableNames::char_reg_str},
    {"inventory_db", &TableNames::inventory},
    {"cart_db", &TableNames::cart_inventory},
    {"storage_db", &TableNames::storage},
    {"guild_storage_db", &TableNames::guild_storage},
    {"mail_db", &TableNames::mail},
    {"mail_attachment_db", &TableNames::mail_attachments},
    {"party_db", &TableNames::party},
    {"guild_db", &TableNames::guild},
    {"guild_member_db", &TableNames::guild_member},
    {"guild_position_db", &TableNames::guild_position},
    {"clan_table", &TableNames::clan},
    {"clan_alliance_table", &TableNames::clan_alliance},
    {"elemental_db", &TableNames::elemental},
    {"homunculus_db", &TableNames::homunculus},
    {"mercenary_owner_db", &TableNames::mercenary_owner},
    {"pet_db", &TableNames::pet},
    {"friend_db", &TableNames::friends},
    {"hotkey_db", &TableNames::hotkey},
    {"memo_db", &TableNames::memo},
    {"skill_db", &TableNames::skill},
    {"scdata_db", &TableNames::sc_data},
    {"bonus_script_db", &TableNames::bonus_script},
    {"quest_db", &TableNames::quest},
    {"achievement_table", &TableNames::achievement},
    {"schema_migrations_table", &TableNames::schema_migrations},
}};

} // namespace

bool TableNames::set(std::string_view key, std::string_view value) {
	if (!is_safe_identifier(value)) return false;
	for (const auto& [k, f] : kKeys) {
		if (k == key) {
			this->*f = std::string(value);
			return true;
		}
	}
	return false;
}

std::optional<std::string_view> TableNames::get(std::string_view key) const {
	for (const auto& [k, f] : kKeys)
		if (k == key) return std::string_view(this->*f);
	return std::nullopt;
}

bool is_safe_identifier(std::string_view name) noexcept {
	if (name.empty() || name.size() > 64) return false;
	for (const char c : name) {
		const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
		if (!ok) return false;
	}
	return true;
}

std::string_view to_string(SqlDialect dialect) noexcept {
	switch (dialect) {
	case SqlDialect::Sqlite: return "sqlite";
	case SqlDialect::Postgres: return "postgresql";
	}
	return "?";
}

std::string quote_identifier(std::string_view name, SqlDialect) {
	if (!is_safe_identifier(name)) throw std::invalid_argument("unsafe SQL identifier");
	return '"' + std::string(name) + '"';
}

} // namespace rathena::persistence
