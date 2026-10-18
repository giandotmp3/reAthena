#pragma once
// SQLite backend, lifted from modernized/rathena-reimagined/persistence (namespace renamed,
// typed poison-row errors and a prepared statement cache added). Links the system libsqlite3.
#include <map>
#include <string>

#include "rathena/persist/sql_connection.hpp"

struct sqlite3;
struct sqlite3_stmt;

namespace rathena::persist {

class SqliteConnection final : public SqlConnection {
public:
	explicit SqliteConnection(std::string path);
	~SqliteConnection() override;
	SqliteConnection(const SqliteConnection&) = delete;
	SqliteConnection& operator=(const SqliteConnection&) = delete;

	[[nodiscard]] SqlDialect dialect() const noexcept override { return SqlDialect::Sqlite; }
	void connect() override;  ///< idempotent
	void execute(const Statement& st) override;
	std::vector<Row> query(const Statement& st) override;
	[[nodiscard]] std::uint64_t affected_rows() const noexcept override { return affected_rows_; }
	bool execute_in_transaction(const std::vector<Statement>& statements) override;
	void transaction(const std::function<void()>& body) override;
	void execute_script(std::string_view sql) override;

	/// Version string of the linked SQLite library.
	static std::string library_version();

private:
	std::vector<Row> run(const Statement& st, bool want_rows);
	sqlite3_stmt* prepare_cached(const std::string& sql);
	void clear_cache() noexcept;
	std::string path_;
	sqlite3* db_{nullptr};
	std::uint64_t affected_rows_{0};
	std::map<std::string, sqlite3_stmt*, std::less<>> cache_;
};

bool postgres_client_available() noexcept;

} // namespace rathena::persist
