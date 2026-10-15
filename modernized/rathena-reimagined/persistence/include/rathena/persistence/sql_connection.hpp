#pragma once
// Backend-neutral SQL connection (DATABASE_DECISION.md). Repositories are written against this
// interface and the portable SQL subset; only identifier quoting, upserts and DDL differ.
//
// Backends: SQLite (embedded: local development, tests, single-host servers) and PostgreSQL 16+
// (servers). MySQL/MariaDB is no longer a backend: `rathena-migrate import` copies an existing
// MySQL database into one of these once, and is the only code that still talks to MySQL.
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "rathena/persistence/row.hpp"

namespace rathena::persistence {

enum class SqlDialect { Sqlite, Postgres };

std::string_view to_string(SqlDialect dialect) noexcept;

/// Double-quotes a validated identifier ("name"; the same for every dialect).
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
	/// Rows changed (PostgreSQL: matched) by the last execute().
	[[nodiscard]] virtual std::uint64_t affected_rows() const noexcept = 0;
	/// All statements in one transaction; ROLLBACK and false on any failure.
	virtual bool execute_in_transaction(const std::vector<Statement>& statements) = 0;
	/// Runs `body` in one transaction: COMMIT when it returns, ROLLBACK and rethrow when it throws.
	/// For writes whose later statements need an earlier result (a RETURNING id, a row read
	/// first); a fixed list of statements goes through execute_in_transaction(). Not reentrant.
	virtual void transaction(const std::function<void()>& body) = 0;
	/// Runs a multi-statement SQL script in one transaction (schema files shipped with rAthena,
	/// e.g. sql-files/postgresql/main.sql). No parameters: never pass text built from user input.
	/// Throws RepositoryError on failure, after rolling back.
	virtual void execute_script(std::string_view sql) = 0;
};

/// Opens a connection from a database URL:
///   sqlite::memory:                          private in-memory database (tests)
///   sqlite:///abs/path/x.db                  file database (absolute path)
///   sqlite:relative/x.db                     file database (relative to the working directory)
///   postgresql://user:pass@host[:5432]/db    PostgreSQL 16+ (also postgres://)
/// Throws DbConfigError for an unknown scheme, a mysql:// URL (MySQL is no longer supported; use
/// `rathena-migrate import`) or a backend this build does not include. The error text never
/// contains the password.
std::unique_ptr<SqlConnection> open_connection(std::string_view url);

} // namespace rathena::persistence
