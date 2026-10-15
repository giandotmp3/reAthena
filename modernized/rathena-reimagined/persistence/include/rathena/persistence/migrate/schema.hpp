#pragma once
// Legacy schema fingerprint (formerly migration step M0). `rathena-migrate import` refuses a
// MySQL source whose shape is not a known rAthena baseline: legacy has 73 files in
// sql-files/upgrades/ and no applied-upgrade tracking, so the only way to know which revision a
// database is on is to fingerprint its shape.
#include <cstdint>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rathena::persistence::migrate {

struct ColumnSchema {
	std::string name;
	std::string type;   ///< as reported by information_schema.COLUMNS.COLUMN_TYPE
	bool nullable = false;
};

struct TableSchema {
	std::string name;
	std::vector<ColumnSchema> columns;
};

struct SchemaSnapshot {
	std::vector<TableSchema> tables;
	[[nodiscard]] const TableSchema* find(std::string_view table) const noexcept;
};

/// Order-independent, case-normalized fingerprint of table/column names, types and nullability
/// (integer display widths ignored, so MariaDB and MySQL 8 agree):
/// "fnv1a64:<16 hex>". Detects drift, not tampering (not a security hash).
std::string schema_fingerprint(const SchemaSnapshot& s);

/// Tables of sql-files/logs.sql. They may live in a separate database (log_db_*).
std::span<const std::string_view> log_tables() noexcept;

/// Tables left out of the fingerprint: logs.sql tables (they may live elsewhere), the
/// item_db*/mob_db*/mob_skill_db* mirrors of the dropped use_sql_db option, and tables written by
/// earlier rathena-migrate versions (schema_migrations, _archive_*, _orphans_*).
bool excluded_from_fingerprint(std::string_view table);

/// Fingerprints of the unmodified legacy schema at the d4b8e7b baseline, for the table sets
/// operators install (main.sql, optionally web.sql). A database on an older revision must first
/// apply the legacy sql-files/upgrades/ scripts. The CI importer job loads sql-files/main.sql into
/// MariaDB and imports it, which fails if these drift from the shipped file.
const std::set<std::string>& known_legacy_baselines();

} // namespace rathena::persistence::migrate
