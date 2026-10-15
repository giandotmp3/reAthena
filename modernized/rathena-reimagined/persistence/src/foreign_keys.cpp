#include "rathena/persistence/foreign_keys.hpp"

#include <array>
#include <stdexcept>
#include <string>
#include <utility>

namespace rathena::persistence {
namespace {

using F = std::string TableNames::*;
constexpr std::array<std::pair<std::string_view, F>, 33> kFields = {{
    {"login", &TableNames::login},
    {"global_acc_reg_num", &TableNames::global_acc_reg_num},
    {"global_acc_reg_str", &TableNames::global_acc_reg_str},
    {"acc_reg_num", &TableNames::acc_reg_num},
    {"acc_reg_str", &TableNames::acc_reg_str},
    {"character", &TableNames::character},
    {"char_reg_num", &TableNames::char_reg_num},
    {"char_reg_str", &TableNames::char_reg_str},
    {"inventory", &TableNames::inventory},
    {"cart_inventory", &TableNames::cart_inventory},
    {"storage", &TableNames::storage},
    {"guild_storage", &TableNames::guild_storage},
    {"mail", &TableNames::mail},
    {"mail_attachments", &TableNames::mail_attachments},
    {"party", &TableNames::party},
    {"guild", &TableNames::guild},
    {"guild_member", &TableNames::guild_member},
    {"guild_position", &TableNames::guild_position},
    {"clan", &TableNames::clan},
    {"clan_alliance", &TableNames::clan_alliance},
    {"elemental", &TableNames::elemental},
    {"homunculus", &TableNames::homunculus},
    {"mercenary_owner", &TableNames::mercenary_owner},
    {"pet", &TableNames::pet},
    {"friends", &TableNames::friends},
    {"hotkey", &TableNames::hotkey},
    {"memo", &TableNames::memo},
    {"skill", &TableNames::skill},
    {"sc_data", &TableNames::sc_data},
    {"bonus_script", &TableNames::bonus_script},
    {"quest", &TableNames::quest},
    {"achievement", &TableNames::achievement},
    {"schema_migrations", &TableNames::schema_migrations},
}};

// Children of `char` that BR-CHAR-013 deletes -> CASCADE; references it clears -> SET NULL.
// Account deletion (BR-LOGIN-037) removes only the login row and `##` regs, so `char` and
// account storage RESTRICT on login rather than cascading a behavior change.
constexpr std::array<ForeignKeySpec, 35> kForeignKeys = {{
    {"global_acc_reg_num", "account_id", "login", "account_id", OnDelete::Cascade},
    {"global_acc_reg_str", "account_id", "login", "account_id", OnDelete::Cascade},
    {"character", "account_id", "login", "account_id", OnDelete::Restrict},
    {"storage", "account_id", "login", "account_id", OnDelete::Restrict},
    {"character", "party_id", "party", "party_id", OnDelete::SetNull},
    {"character", "guild_id", "guild", "guild_id", OnDelete::SetNull},
    {"character", "clan_id", "clan", "clan_id", OnDelete::SetNull},
    {"character", "partner_id", "character", "char_id", OnDelete::SetNull},
    {"character", "father", "character", "char_id", OnDelete::SetNull},
    {"character", "mother", "character", "char_id", OnDelete::SetNull},
    {"character", "child", "character", "char_id", OnDelete::SetNull},
    {"inventory", "char_id", "character", "char_id", OnDelete::Cascade},
    {"cart_inventory", "char_id", "character", "char_id", OnDelete::Cascade},
    {"skill", "char_id", "character", "char_id", OnDelete::Cascade},
    {"memo", "char_id", "character", "char_id", OnDelete::Cascade},
    {"hotkey", "char_id", "character", "char_id", OnDelete::Cascade},
    {"char_reg_num", "char_id", "character", "char_id", OnDelete::Cascade},
    {"char_reg_str", "char_id", "character", "char_id", OnDelete::Cascade},
    {"sc_data", "char_id", "character", "char_id", OnDelete::Cascade},
    {"bonus_script", "char_id", "character", "char_id", OnDelete::Cascade},
    {"quest", "char_id", "character", "char_id", OnDelete::Cascade},
    {"achievement", "char_id", "character", "char_id", OnDelete::Cascade},
    {"friends", "char_id", "character", "char_id", OnDelete::Cascade},
    {"friends", "friend_id", "character", "char_id", OnDelete::Cascade},
    {"homunculus", "char_id", "character", "char_id", OnDelete::Cascade},
    {"elemental", "char_id", "character", "char_id", OnDelete::Cascade},
    {"mercenary_owner", "char_id", "character", "char_id", OnDelete::Cascade},
    {"guild_member", "char_id", "character", "char_id", OnDelete::Cascade},
    // The guild master row RESTRICTs: the plan must break the guild before deleting the char.
    {"guild", "char_id", "character", "char_id", OnDelete::Restrict},
    {"mail", "dest_id", "character", "char_id", OnDelete::Cascade},
    {"mail", "send_id", "character", "char_id", OnDelete::SetNull},
    // BR-CHAR-042: breaking a guild deletes all of its data; a mail takes its attachments along.
    {"guild_member", "guild_id", "guild", "guild_id", OnDelete::Cascade},
    {"guild_position", "guild_id", "guild", "guild_id", OnDelete::Cascade},
    {"guild_storage", "guild_id", "guild", "guild_id", OnDelete::Cascade},
    {"mail_attachments", "id", "mail", "id", OnDelete::Cascade},
}};

} // namespace

std::span<const ForeignKeySpec> target_foreign_keys() noexcept { return kForeignKeys; }

std::string_view resolve_table(const TableNames& names, std::string_view logical_table) {
	for (const auto& [k, f] : kFields)
		if (k == logical_table) return names.*f;
	throw std::invalid_argument("unknown logical table: " + std::string(logical_table));
}

} // namespace rathena::persistence
