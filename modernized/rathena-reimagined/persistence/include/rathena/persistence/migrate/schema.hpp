#pragma once
// Schema snapshot + fingerprint used by M0 (baseline by fingerprint; unknown drift -> refuse).
// Legacy has 73 files in sql-files/upgrades/ and no applied-upgrade tracking, so the only way to
// know which baseline a database is on is to fingerprint its shape.
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace rathena::persistence::migrate {

struct ColumnSchema {
	std::string name;
	std::string type;   ///< as reported by information_schema.COLUMNS.COLUMN_TYPE
	bool nullable = false;
};

struct TableSchema {
	std::string name;
	std::string engine; ///< MyISAM / InnoDB (not part of the fingerprint; M2 changes it)
	std::vector<ColumnSchema> columns;
};

struct SchemaSnapshot {
	std::vector<TableSchema> tables;
	[[nodiscard]] const TableSchema* find(std::string_view table) const noexcept;
};

/// Order-independent, case-normalized fingerprint of table/column names, types and nullability:
/// "fnv1a64:<16 hex>". Detects drift, not tampering (not a security hash).
std::string schema_fingerprint(const SchemaSnapshot& s);

} // namespace rathena::persistence::migrate
