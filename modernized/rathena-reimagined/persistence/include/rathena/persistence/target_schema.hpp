#pragma once
// The clean target schema for the new backends (DATABASE_DECISION.md): created fresh with real
// constraints (foreign keys, CHECKs, NULL instead of the legacy `0` sentinel) instead of being
// altered in place from the MyISAM schema. Generated per dialect from one definition.
//
// Coverage: the login-owned tables (login, global_acc_reg_num/_str; target_0001) and the
// character-owned tables (target_0002): char and its items (inventory, cart_inventory), account
// storage, party, guild (+ guild_member, guild_position, guild_storage), mail (+ mail_attachments),
// clan, and every table the BR-CHAR-013 cleanup plan touches. Table and column names are the
// legacy ones (sql-files/main.sql); the differences are NULL instead of the `0` sentinel, the
// foreign keys of target_foreign_keys(), CHECKs, integer UNIX times and no `login.user_pass`.
#include <string_view>
#include <vector>

#include "rathena/persistence/row.hpp"
#include "rathena/persistence/sql_connection.hpp"
#include "rathena/persistence/table_names.hpp"

namespace rathena::persistence {

/// Version recorded in schema_migrations by create_target_schema().
inline constexpr std::string_view kTargetSchemaVersion = "target_0002_character_tables";

/// CREATE statements (idempotent: IF NOT EXISTS) for SqlDialect::Sqlite or ::Postgres.
std::vector<Statement> target_schema_statements(SqlDialect dialect, const TableNames& names = {});

/// Creates the target schema on `conn` in one transaction and records kTargetSchemaVersion.
/// Returns false (and changes nothing) if any statement fails.
bool create_target_schema(SqlConnection& conn, const TableNames& names = {});

} // namespace rathena::persistence
