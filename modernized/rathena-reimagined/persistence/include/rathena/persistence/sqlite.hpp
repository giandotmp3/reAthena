#pragma once
// SQLite backend (vendored amalgamation; no server, no install). Used for local development,
// every repository test on every CI platform, and single-host small servers.
//
// Connection settings: foreign_keys=ON, busy_timeout 5 s, WAL journal for file databases,
// double-quoted string literals disabled at build time (SQLITE_DQS=0). One statement per call;
// a trailing second statement is rejected.
#include <string>

#include "rathena/persistence/sql_connection.hpp"

struct sqlite3;

namespace rathena::persistence {

class SqliteConnection final : public SqlConnection {
public:
	/// `path` is a filesystem path or ":memory:".
	explicit SqliteConnection(std::string path);
	~SqliteConnection() override;
	SqliteConnection(const SqliteConnection&) = delete;
	SqliteConnection& operator=(const SqliteConnection&) = delete;

	[[nodiscard]] SqlDialect dialect() const noexcept override { return SqlDialect::Sqlite; }
	void connect() override;
	void execute(const Statement& st) override;
	std::vector<Row> query(const Statement& st) override;
	[[nodiscard]] std::uint64_t affected_rows() const noexcept override { return affected_rows_; }
	bool execute_in_transaction(const std::vector<Statement>& statements) override;

	[[nodiscard]] const std::string& path() const noexcept { return path_; }

private:
	std::vector<Row> run(const Statement& st, bool want_rows);
	std::string path_;
	sqlite3* db_ = nullptr;
	std::uint64_t affected_rows_ = 0;
};

} // namespace rathena::persistence
