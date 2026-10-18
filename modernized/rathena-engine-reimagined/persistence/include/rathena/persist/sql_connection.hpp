#pragma once
// Backend-neutral SQL connection, lifted (interface only) from the earlier scaffold's connection
// layer. Backends: SQLite (embedded) and PostgreSQL 16+. MySQL/MariaDB is not a backend.
// The concrete SqliteConnection / PostgresConnection are lifted in a follow-up step
// (see README "Lift plan"); this scaffold ships the interface and the URL factory.
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "rathena/persist/row.hpp"

namespace rathena::persist {

enum class SqlDialect { Sqlite, Postgres };

std::string_view to_string(SqlDialect dialect) noexcept;

/// Double-quotes a validated identifier. Throws std::invalid_argument unless [A-Za-z0-9_]+.
std::string quote_identifier(std::string_view name, SqlDialect dialect);

class SqlConnection {
public:
	virtual ~SqlConnection() = default;
	[[nodiscard]] virtual SqlDialect dialect() const noexcept = 0;
	virtual void connect() = 0;
	/// One statement; parameters are bound, never spliced. Throws RepositoryError on failure.
	virtual void execute(const Statement& st) = 0;
	virtual std::vector<Row> query(const Statement& st) = 0;
	[[nodiscard]] virtual std::uint64_t affected_rows() const noexcept = 0;
	/// All statements in one transaction; ROLLBACK and false on any failure.
	virtual bool execute_in_transaction(const std::vector<Statement>& statements) = 0;
	/// COMMIT when `body` returns, ROLLBACK and rethrow when it throws. Not reentrant.
	virtual void transaction(const std::function<void()>& body) = 0;
	/// Multi-statement script in one transaction. No parameters: never pass user-built text.
	virtual void execute_script(std::string_view sql) = 0;
};

/// Opens a connection from a database URL (sqlite::memory:, sqlite:path, postgresql://...).
/// Throws DbConfigError for an unknown scheme or mysql://. Error text never contains a password.
/// SCAFFOLD: a valid URL currently throws NotImplementedError until the connection layer is lifted.
std::unique_ptr<SqlConnection> open_connection(std::string_view url);

} // namespace rathena::persist
