#pragma once
// Overridable table names (spec §3.5). Defaults fix legacy defect D2 (migration step M5 is a code
// fix): legacy char.cpp:2726 overwrote `clan_table` with "clan_alliance", left
// `clan_alliance_table` empty and had no default for `elemental_db`.
#include <optional>
#include <string>
#include <string_view>

namespace rathena::persistence {

struct TableNames {
	std::string login = "login";
	std::string global_acc_reg_num = "global_acc_reg_num";
	std::string global_acc_reg_str = "global_acc_reg_str";
	std::string acc_reg_num = "acc_reg_num";
	std::string acc_reg_str = "acc_reg_str";
	std::string character = "char";
	std::string char_reg_num = "char_reg_num";
	std::string char_reg_str = "char_reg_str";
	std::string inventory = "inventory";
	std::string cart_inventory = "cart_inventory";
	std::string storage = "storage";
	std::string guild_storage = "guild_storage";
	std::string mail = "mail";
	std::string mail_attachments = "mail_attachments";
	std::string party = "party";
	std::string guild = "guild";
	std::string guild_member = "guild_member";
	std::string guild_position = "guild_position";
	std::string clan = "clan";
	std::string clan_alliance = "clan_alliance";
	std::string elemental = "elemental";
	std::string homunculus = "homunculus";
	std::string mercenary_owner = "mercenary_owner";
	std::string pet = "pet";
	std::string friends = "friends";
	std::string hotkey = "hotkey";
	std::string memo = "memo";
	std::string skill = "skill";
	std::string sc_data = "sc_data";
	std::string bonus_script = "bonus_script";
	std::string quest = "quest";
	std::string achievement = "achievement";
	std::string schema_migrations = "schema_migrations";

	/// Applies a `<name>_table: value` style override by logical key (e.g. "clan_table").
	/// Returns false for an unknown key or an empty / non-identifier value.
	bool set(std::string_view key, std::string_view value);
	[[nodiscard]] std::optional<std::string_view> get(std::string_view key) const;
};

/// True for a plain SQL identifier ([A-Za-z0-9_]+, 1..64 chars); checked before quoting
/// (quote_identifier() in sql_connection.hpp).
bool is_safe_identifier(std::string_view name) noexcept;

} // namespace rathena::persistence
