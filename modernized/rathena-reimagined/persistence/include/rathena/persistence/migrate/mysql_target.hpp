#pragma once
// MigrationTarget over a live MySQL/MariaDB database (rathena-migrate, architecture §5).
#include <set>
#include <string>

#include "rathena/persistence/migrate/migrator.hpp"
#include "rathena/persistence/mysql.hpp"

namespace rathena::persistence::migrate {

/// Fingerprints of the unmodified legacy schema at the d4b8e7b baseline, for the table sets
/// operators install (main.sql, optionally web.sql). Log tables and the static-data SQL mirrors
/// are not part of the fingerprint (see MysqlMigrationTarget::inspect_schema). A database on an
/// older revision must first apply the legacy sql-files/upgrades/ scripts.
/// Kept honest by MysqlMigrationIT.KnownBaselinesMatchShippedSqlFiles.
const std::set<std::string>& known_legacy_baselines();

class MysqlMigrationTarget final : public MigrationTarget {
public:
	MysqlMigrationTarget(MysqlConnection& conn, TableNames names) : conn_(conn), names_(std::move(names)) {}

	/// Tables of the connected database, excluding: schema_migrations, `_archive_*`/`_orphans_*`
	/// tables written by this tool, logs.sql tables (they may live in ${LOG_DB_URL}), and the
	/// item_db*/mob_db*/mob_skill_db* mirrors (use_sql_db was dropped at HITL #2).
	SchemaSnapshot inspect_schema() override;
	std::vector<std::string> applied_versions() override;
	/// M1: per M3b foreign key, child rows whose non-zero reference has no parent row.
	OrphanReport orphan_report() override;
	/// Runs the step's statements, then records `version`. MySQL commits DDL implicitly, so a
	/// failure part-way leaves the step unrecorded; every step is written to be re-runnable.
	void apply(std::string_view version, const std::vector<Statement>& statements) override;

private:
	bool table_exists(std::string_view table);
	MysqlConnection& conn_;
	TableNames names_;
};

} // namespace rathena::persistence::migrate
