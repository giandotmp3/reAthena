#include "rathena/persistence/migrate/mysql_target.hpp"

#include <algorithm>
#include <cctype>
#include <map>

#include "rathena/persistence/errors.hpp"
#include "rathena/persistence/migrate/plan.hpp"

namespace rathena::persistence::migrate {
namespace {

std::string lower(std::string_view s) {
	std::string out(s);
	for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return out;
}

std::string text(const Row& r, std::string_view col) {
	const auto it = r.find(col);
	if (it == r.end()) throw RepositoryError("missing column " + std::string(col));
	if (const auto* s = std::get_if<std::string>(&it->second)) return *s;
	return {};
}

std::uint64_t count(const Row& r) {
	if (r.empty()) return 0;
	const auto& v = r.begin()->second;
	if (const auto* u = std::get_if<std::uint64_t>(&v)) return *u;
	if (const auto* i = std::get_if<std::int64_t>(&v)) return static_cast<std::uint64_t>(*i);
	return 0;
}

bool excluded_from_snapshot(const std::string& lname, const TableNames& names) {
	if (lname == lower(names.schema_migrations)) return true;
	if (lname.starts_with("_archive_") || lname.starts_with("_orphans_")) return true;
	if (lname.starts_with("item_db") || lname.starts_with("mob_db") || lname.starts_with("mob_skill_db")) return true;
	const auto logs = log_tables();
	return std::find(logs.begin(), logs.end(), lname) != logs.end();
}

} // namespace

const std::set<std::string>& known_legacy_baselines() {
	// Regenerate with MysqlMigrationIT.KnownBaselinesMatchShippedSqlFiles when sql-files change.
	static const std::set<std::string> kBaselines = {
	    "fnv1a64:c49b2a29aef98b18", // sql-files/main.sql @ d4b8e7b
	    "fnv1a64:61e54cc3b42c1532", // sql-files/main.sql + web.sql @ d4b8e7b
	};
	return kBaselines;
}

SchemaSnapshot MysqlMigrationTarget::inspect_schema() {
	std::map<std::string, TableSchema> tables;
	for (const auto& r : conn_.query({"SELECT TABLE_NAME AS name, ENGINE AS engine FROM information_schema.TABLES"
	                                  " WHERE TABLE_SCHEMA = DATABASE() AND TABLE_TYPE = 'BASE TABLE'",
	                                  {}})) {
		const auto name = text(r, "name");
		if (excluded_from_snapshot(lower(name), names_)) continue;
		tables[name] = TableSchema{name, text(r, "engine"), {}};
	}
	for (const auto& r : conn_.query({"SELECT TABLE_NAME AS tbl, COLUMN_NAME AS col, COLUMN_TYPE AS type,"
	                                  " IS_NULLABLE AS nullable FROM information_schema.COLUMNS"
	                                  " WHERE TABLE_SCHEMA = DATABASE() ORDER BY TABLE_NAME, ORDINAL_POSITION",
	                                  {}})) {
		const auto it = tables.find(text(r, "tbl"));
		if (it == tables.end()) continue;
		it->second.columns.push_back({text(r, "col"), text(r, "type"), text(r, "nullable") == "YES"});
	}
	SchemaSnapshot snap;
	for (auto& [name, t] : tables) snap.tables.push_back(std::move(t));
	return snap;
}

bool MysqlMigrationTarget::table_exists(std::string_view table) {
	const auto rows = conn_.query({"SELECT COUNT(*) AS n FROM information_schema.TABLES"
	                               " WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = ?",
	                               {std::string(table)}});
	return !rows.empty() && count(rows.front()) > 0;
}

std::vector<std::string> MysqlMigrationTarget::applied_versions() {
	if (!table_exists(names_.schema_migrations)) return {};
	std::vector<std::string> out;
	for (const auto& r : conn_.query({"SELECT `version` FROM " + quote_identifier(names_.schema_migrations) +
	                                      " ORDER BY `version`",
	                                  {}}))
		out.push_back(text(r, "version"));
	return out;
}

OrphanReport MysqlMigrationTarget::orphan_report() {
	OrphanReport report;
	for (const auto& fk : m3b_foreign_keys()) {
		const std::string child(resolve_table(names_, fk.child_table));
		const std::string parent(resolve_table(names_, fk.parent_table));
		if (!table_exists(child) || !table_exists(parent)) continue;
		const auto c = quote_identifier(std::string(fk.column));
		const auto pc = quote_identifier(std::string(fk.parent_column));
		// `0` is the legacy "none" sentinel until M3a converts it to NULL.
		const auto rows = conn_.query({"SELECT COUNT(*) AS n FROM " + quote_identifier(child) + " c LEFT JOIN " +
		                                   quote_identifier(parent) + " p ON c." + c + " = p." + pc + " WHERE c." + c +
		                                   " IS NOT NULL AND c." + c + " <> 0 AND p." + pc + " IS NULL",
		                               {}});
		const auto n = rows.empty() ? 0 : count(rows.front());
		if (n > 0) report.findings.push_back({child, std::string(fk.column), parent, n});
	}
	return report;
}

void MysqlMigrationTarget::apply(std::string_view version, const std::vector<Statement>& statements) {
	for (const auto& st : statements) conn_.execute(st);
	// M0 creates schema_migrations, so the first record lands right after it exists.
	conn_.execute({"INSERT INTO " + quote_identifier(names_.schema_migrations) +
	                   " (`version`, `applied_at`) VALUES (?, NOW())",
	               {std::string(version)}});
}

} // namespace rathena::persistence::migrate
