#pragma once
// Backend-neutral SQL connection (DATABASE_DECISION.md). Repositories are written against this
// interface and the portable SQL subset; only identifier quoting, upserts and DDL differ.
//
// Backends: SQLite (embedded: local development, tests, single-host servers), PostgreSQL
// (servers; pending), MySQL/MariaDB (transition only, until the legacy writers are gone).
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "rathena/persistence/row.hpp"

namespace rathena::persistence {

enum class SqlDialect { MySql, Sqlite, Postgres };

/// Quotes a validated identifier for `dialect`: `name` for MySQL, "name" otherwise.
/// Throws std::invalid_argument for anything that is not [A-Za-z0-9_]+.
std::string quote_identifier(std::string_view name, SqlDialect dialect);

class SqlConnection {
public:
	virtual ~SqlConnection() = default;
	[[nodiscard]] virtual SqlDialect dialect() const noexcept = 0;
	virtual void connect() = 0;
	/// One statement; parameters are bound, never spliced. Throws RepositoryError on failure.
	virtual void execute(const Statement& st) = 0;
	virtual std::vector<Row> query(const Statement& st) = 0;
	/// Rows changed by the last execute().
	[[nodiscard]] virtual std::uint64_t affected_rows() const noexcept = 0;
	/// All statements in one transaction; ROLLBACK and false on any failure.
	virtual bool execute_in_transaction(const std::vector<Statement>& statements) = 0;
};

/// Opens a connection from a database URL:
///   sqlite::memory:            private in-memory database (tests)
///   sqlite:///abs/path/x.db    file database (absolute path)
///   sqlite:relative/x.db       file database (relative to the working directory)
///   mysql://user:pass@host:port/db
/// Throws DbConfigError for an unknown scheme or a backend this build does not include.
std::unique_ptr<SqlConnection> open_connection(std::string_view url);

} // namespace rathena::persistence
