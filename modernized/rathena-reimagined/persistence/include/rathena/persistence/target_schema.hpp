#pragma once
// The clean target schema for the new backends (DATABASE_DECISION.md): created fresh with real
// constraints (foreign keys, CHECKs, NULL instead of the legacy `0` sentinel) instead of being
// altered in place from the MyISAM schema. Generated per dialect from one definition.
//
// Coverage grows with the repository ports: today the login-owned tables (login,
// global_acc_reg_num, global_acc_reg_str), i.e. what moves first at stage B.
#include <string_view>
#include <vector>

#include "rathena/persistence/row.hpp"
#include "rathena/persistence/sql_connection.hpp"
#include "rathena/persistence/table_names.hpp"

namespace rathena::persistence {

/// Version recorded in schema_migrations by create_target_schema().
inline constexpr std::string_view kTargetSchemaVersion = "target_0001_login_tables";

/// CREATE statements (idempotent: IF NOT EXISTS) for SqlDialect::Sqlite or ::Postgres.
std::vector<Statement> target_schema_statements(SqlDialect dialect, const TableNames& names = {});

/// Creates the target schema on `conn` in one transaction and records kTargetSchemaVersion.
/// Returns false (and changes nothing) if any statement fails.
bool create_target_schema(SqlConnection& conn, const TableNames& names = {});

} // namespace rathena::persistence
