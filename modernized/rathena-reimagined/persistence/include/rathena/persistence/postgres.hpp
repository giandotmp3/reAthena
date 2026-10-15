#pragma once
// PostgreSQL 16+ backend (libpq). Used by servers in production (DATABASE_DECISION.md).
//
// Every statement goes through PQexecParams: the repositories' '?' placeholders are rewritten to
// $1..$n and values are sent separately from the SQL text (text format, type inferred by the
// server; Blob values in binary format as bytea), so no value is ever spliced into a query.
// Result values: int2/int4/int8/bool columns come back as int64, bytea as Blob, NULL as
// monostate, everything else as the server's text form.
//
// rAthena databases are created with ENCODING 'SQL_ASCII' LC_COLLATE 'C' LC_CTYPE 'C', so text is
// stored byte for byte (client strings are raw EUC-KR/CP1252 bytes). Text values cannot contain
// a NUL byte on PostgreSQL.
//
// Built when CMake finds libpq (RATHENA_PERSISTENCE_WITH_POSTGRESQL); otherwise every operation
// throws DbConfigError and open_connection() refuses postgres:// URLs.
#include <memory>
#include <string>
#include <string_view>

#include "rathena/persistence/db_config.hpp"
#include "rathena/persistence/sql_connection.hpp"

namespace rathena::persistence {

/// True when this build links libpq.
bool postgres_client_available() noexcept;

/// Rewrites '?' placeholders to $1, $2, ... in order. Question marks inside '...' string literals,
/// "..." identifiers, $$ ... $$ bodies and -- or /* */ comments are left alone. `count`, if given,
/// receives the number of placeholders.
std::string to_postgres_placeholders(std::string_view sql, std::size_t* count = nullptr);

class PostgresConnection final : public SqlConnection {
public:
	explicit PostgresConnection(DbEndpoint endpoint);
	~PostgresConnection() override;
	PostgresConnection(const PostgresConnection&) = delete;
	PostgresConnection& operator=(const PostgresConnection&) = delete;

	[[nodiscard]] SqlDialect dialect() const noexcept override { return SqlDialect::Postgres; }
	void connect() override; ///< throws RepositoryError (DbConfigError without libpq)
	void execute(const Statement& st) override;
	std::vector<Row> query(const Statement& st) override;
	[[nodiscard]] std::uint64_t affected_rows() const noexcept override { return affected_rows_; }
	bool execute_in_transaction(const std::vector<Statement>& statements) override;
	void transaction(const std::function<void()>& body) override;
	void execute_script(std::string_view sql) override;

	[[nodiscard]] const DbEndpoint& endpoint() const noexcept { return endpoint_; }

private:
	struct Impl;
	std::vector<Row> run(const Statement& st, bool want_rows);
	DbEndpoint endpoint_;
	std::unique_ptr<Impl> impl_;
	std::uint64_t affected_rows_ = 0;
};

} // namespace rathena::persistence
