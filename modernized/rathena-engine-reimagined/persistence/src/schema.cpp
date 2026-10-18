#include <algorithm>
#include <map>

#include "rathena/persist/service.hpp"

namespace rathena::persist {
namespace {

std::string for_dialect(std::string sql, SqlDialect d) {
	auto replace_all = [&](const std::string& from, const std::string& to) {
		for (std::size_t p = sql.find(from); p != std::string::npos; p = sql.find(from, p + to.size()))
			sql.replace(p, from.size(), to);
	};
	if (d == SqlDialect::Postgres) replace_all("BLOB", "BYTEA");
	return sql;
}

std::vector<Statement> core_tables(SqlDialect d) {
	const char* ddl[] = {
	    "CREATE TABLE persist_records ("
	    " scope BIGINT NOT NULL, owner_id BIGINT NOT NULL, kind TEXT NOT NULL,"
	    " schema_version BIGINT NOT NULL, revision BIGINT NOT NULL, payload BLOB NOT NULL,"
	    " PRIMARY KEY (scope, owner_id, kind))",
	    "CREATE TABLE kv_entries ("
	    " scope BIGINT NOT NULL, owner_ref TEXT NOT NULL, mod_id TEXT NOT NULL, entry_key TEXT NOT NULL,"
	    " row_version BIGINT NOT NULL, revision BIGINT NOT NULL, size BIGINT NOT NULL, value BLOB NOT NULL,"
	    " PRIMARY KEY (scope, owner_ref, mod_id, entry_key))",
	    "CREATE INDEX kv_entries_mod ON kv_entries (mod_id)",
	    "CREATE TABLE persist_counters (name TEXT PRIMARY KEY, value BIGINT NOT NULL)",
	    "INSERT INTO persist_counters (name, value) VALUES ('quarantine', 0)",
	    "CREATE TABLE persist_quarantine ("
	    " id BIGINT PRIMARY KEY, table_name TEXT NOT NULL, row_ref TEXT NOT NULL, owner_ref TEXT NOT NULL,"
	    " reason TEXT NOT NULL, raw BLOB NOT NULL)",
	    "CREATE INDEX persist_quarantine_ref ON persist_quarantine (table_name, row_ref, owner_ref)",
	    "CREATE TABLE persist_id_map ("
	    " kind TEXT NOT NULL, client_id BIGINT NOT NULL, namespaced TEXT NOT NULL, state BIGINT NOT NULL,"
	    " PRIMARY KEY (kind, client_id), UNIQUE (kind, namespaced))",
	    "CREATE TABLE persist_wal_progress (wal_id TEXT PRIMARY KEY, last_seq BIGINT NOT NULL)",
	};
	std::vector<Statement> out;
	for (const char* s : ddl) out.push_back({for_dialect(s, d), {}});
	return out;
}

std::int64_t as_int(const SqlValue& v) {
	if (const auto* i = std::get_if<std::int64_t>(&v)) return *i;
	if (const auto* u = std::get_if<std::uint64_t>(&v)) return static_cast<std::int64_t>(*u);
	return 0;
}
std::string as_text(const SqlValue& v) {
	if (const auto* s = std::get_if<std::string>(&v)) return *s;
	if (const auto* b = std::get_if<Blob>(&v)) return b->bytes;
	return {};
}

} // namespace

std::vector<Migration> builtin_migrations() {
	Migration m;
	m.version = 1;
	m.name = "persist_core_tables";
	m.checksum = "builtin-1-core-tables-v1";
	m.up_sqlite = core_tables(SqlDialect::Sqlite);
	m.up_postgres = core_tables(SqlDialect::Postgres);
	return {std::move(m)};
}

void ensure_schema(SqlConnection& db) {
	MigrationRunner(db, builtin_migrations(), MigrationOptions{1, kForeignMigrationBase}).apply_pending();
}

MigrationRunner::MigrationRunner(SqlConnection& db, std::vector<Migration> m, MigrationOptions o)
    : db_(db), migrations_(std::move(m)), options_(o) {}

namespace {
const char* kCreateTable =
    "CREATE TABLE IF NOT EXISTS schema_migrations ("
    " version BIGINT PRIMARY KEY, name TEXT NOT NULL, checksum TEXT NOT NULL)";

std::map<std::uint32_t, std::string> applied_rows(SqlConnection& db, const MigrationOptions& o) {
	std::map<std::uint32_t, std::string> out;
	for (const auto& row : db.query({"SELECT version, checksum FROM schema_migrations ORDER BY version", {}})) {
		const auto v = static_cast<std::uint32_t>(as_int(row.at("version")));
		if (v < o.managed_from || (o.managed_below != 0 && v >= o.managed_below)) continue;
		out[v] = as_text(row.at("checksum"));
	}
	return out;
}
} // namespace

std::uint32_t MigrationRunner::current_version() {
	try {
		db_.execute({kCreateTable, {}});
		const auto applied = applied_rows(db_, options_);
		return applied.empty() ? 0 : applied.rbegin()->first;
	} catch (const RepositoryError& e) {
		throw MigrationError(std::string("cannot read schema version: ") + e.what());
	}
}

std::vector<std::uint32_t> MigrationRunner::apply_pending() {
	for (std::size_t i = 0; i < migrations_.size(); ++i) {
		const auto expect = options_.managed_from + static_cast<std::uint32_t>(i);
		if (migrations_[i].version != expect)
			throw MigrationError("migration list is not contiguous and ordered: expected version " +
			                     std::to_string(expect) + " but found " + std::to_string(migrations_[i].version));
	}
	std::map<std::uint32_t, std::string> applied;
	try {
		db_.execute({kCreateTable, {}});
		applied = applied_rows(db_, options_);
	} catch (const RepositoryError& e) {
		throw MigrationError(std::string("cannot read schema_migrations: ") + e.what());
	}
	const std::uint32_t build_max = migrations_.empty() ? 0 : migrations_.back().version;
	for (const auto& [v, sum] : applied) {
		if (v > build_max || v < options_.managed_from)
			throw MigrationError("database schema version " + std::to_string(v) + " is newer than this build (" +
			                     std::to_string(build_max) + "); refusing to run an older binary");
		if (migrations_[v - options_.managed_from].checksum != sum)
			throw MigrationError("migration " + std::to_string(v) + " (" + migrations_[v - options_.managed_from].name +
			                     ") was edited after it was applied (checksum differs)");
	}
	std::vector<std::uint32_t> done;
	const bool pg = db_.dialect() == SqlDialect::Postgres;
	for (const auto& m : migrations_) {
		if (applied.count(m.version) != 0) continue;
		try {
			bool already = false;
			db_.transaction([&] {
				// another process may have applied it while we waited for the write lock
				const auto rows = db_.query({"SELECT version FROM schema_migrations WHERE version = ?",
				                             {SqlValue{static_cast<std::int64_t>(m.version)}}});
				if (!rows.empty()) { already = true; return; }
				for (const auto& st : (pg ? m.up_postgres : m.up_sqlite)) db_.execute(st);
				db_.execute({"INSERT INTO schema_migrations (version, name, checksum) VALUES (?, ?, ?)",
				             {SqlValue{static_cast<std::int64_t>(m.version)}, SqlValue{m.name}, SqlValue{m.checksum}}});
			});
			if (!already) done.push_back(m.version);
		} catch (const MigrationError&) {
			throw;
		} catch (const PersistError& e) {
			throw MigrationError("migration " + std::to_string(m.version) + " (" + m.name + ") failed and was rolled back: " + e.what());
		}
	}
	return done;
}

} // namespace rathena::persist
