#include "rathena/persistence/sqlite.hpp"

#include <sqlite3.h>

#include <limits>
#include <type_traits>
#include <variant>

#include "rathena/persistence/errors.hpp"

namespace rathena::persistence {
namespace {

struct StmtGuard {
	sqlite3_stmt* stmt = nullptr;
	~StmtGuard() { sqlite3_finalize(stmt); }
};

std::string_view trim_tail(const char* tail) {
	std::string_view t = tail == nullptr ? std::string_view{} : std::string_view(tail);
	while (!t.empty() && (t.front() == ' ' || t.front() == '\n' || t.front() == '\r' || t.front() == '\t' || t.front() == ';'))
		t.remove_prefix(1);
	return t;
}

} // namespace

SqliteConnection::SqliteConnection(std::string path) : path_(std::move(path)) {}

SqliteConnection::~SqliteConnection() {
	if (db_ != nullptr) sqlite3_close_v2(db_);
}

void SqliteConnection::connect() {
	if (db_ != nullptr) return;
	const int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX;
	if (sqlite3_open_v2(path_.c_str(), &db_, flags, nullptr) != SQLITE_OK) {
		const std::string err = db_ != nullptr ? sqlite3_errmsg(db_) : "out of memory";
		sqlite3_close_v2(db_);
		db_ = nullptr;
		throw RepositoryError("open sqlite database '" + path_ + "' failed: " + err);
	}
	sqlite3_busy_timeout(db_, 5000);
	sqlite3_extended_result_codes(db_, 1);
	execute({"PRAGMA foreign_keys = ON", {}});
	if (path_ != ":memory:") (void)run({"PRAGMA journal_mode = WAL", {}}, true);
}

std::vector<Row> SqliteConnection::run(const Statement& st, bool want_rows) {
	connect();
	StmtGuard g;
	const char* tail = nullptr;
	if (sqlite3_prepare_v2(db_, st.sql.c_str(), static_cast<int>(st.sql.size()), &g.stmt, &tail) != SQLITE_OK)
		throw RepositoryError(std::string("prepare failed: ") + sqlite3_errmsg(db_));
	if (g.stmt == nullptr) throw RepositoryError("empty statement");
	if (!trim_tail(tail).empty()) throw RepositoryError("only one SQL statement per call is allowed");
	if (sqlite3_bind_parameter_count(g.stmt) != static_cast<int>(st.params.size()))
		throw RepositoryError("parameter count mismatch: statement expects " +
		                      std::to_string(sqlite3_bind_parameter_count(g.stmt)) + ", got " + std::to_string(st.params.size()));

	for (std::size_t i = 0; i < st.params.size(); ++i) {
		const int idx = static_cast<int>(i) + 1;
		int rc = SQLITE_OK;
		std::visit(
		    [&](const auto& v) {
			    using T = std::decay_t<decltype(v)>;
			    if constexpr (std::is_same_v<T, std::monostate>) {
				    rc = sqlite3_bind_null(g.stmt, idx);
			    } else if constexpr (std::is_same_v<T, std::int64_t>) {
				    rc = sqlite3_bind_int64(g.stmt, idx, v);
			    } else if constexpr (std::is_same_v<T, std::uint64_t>) {
				    if (v > static_cast<std::uint64_t>(std::numeric_limits<sqlite3_int64>::max()))
					    throw RepositoryError("unsigned value out of SQLite integer range");
				    rc = sqlite3_bind_int64(g.stmt, idx, static_cast<sqlite3_int64>(v));
			    } else if constexpr (std::is_same_v<T, Blob>) {
				    rc = sqlite3_bind_blob64(g.stmt, idx, v.bytes.data(), v.bytes.size(), SQLITE_TRANSIENT);
			    } else {
				    rc = sqlite3_bind_text64(g.stmt, idx, v.data(), v.size(), SQLITE_TRANSIENT, SQLITE_UTF8);
			    }
		    },
		    st.params[i]);
		if (rc != SQLITE_OK) throw RepositoryError(std::string("bind failed: ") + sqlite3_errmsg(db_));
	}

	std::vector<Row> rows;
	for (;;) {
		const int rc = sqlite3_step(g.stmt);
		if (rc == SQLITE_DONE) break;
		if (rc != SQLITE_ROW) throw RepositoryError(std::string("execute failed: ") + sqlite3_errmsg(db_));
		if (!want_rows) continue;
		Row row;
		const int ncols = sqlite3_column_count(g.stmt);
		for (int c = 0; c < ncols; ++c) {
			SqlValue v;
			switch (sqlite3_column_type(g.stmt, c)) {
			case SQLITE_INTEGER: v = static_cast<std::int64_t>(sqlite3_column_int64(g.stmt, c)); break;
			case SQLITE_NULL: break;
			case SQLITE_BLOB: {
				const auto* bytes = static_cast<const char*>(sqlite3_column_blob(g.stmt, c));
				v = Blob{std::string(bytes == nullptr ? "" : bytes, static_cast<std::size_t>(sqlite3_column_bytes(g.stmt, c)))};
				break;
			}
			default: {
				const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(g.stmt, c));
				v = std::string(text == nullptr ? "" : text, static_cast<std::size_t>(sqlite3_column_bytes(g.stmt, c)));
			}
			}
			row.emplace(sqlite3_column_name(g.stmt, c), std::move(v));
		}
		rows.push_back(std::move(row));
	}
	affected_rows_ = static_cast<std::uint64_t>(sqlite3_changes64(db_));
	return rows;
}

void SqliteConnection::execute(const Statement& st) { (void)run(st, false); }

std::vector<Row> SqliteConnection::query(const Statement& st) { return run(st, true); }

void SqliteConnection::execute_script(std::string_view sql) {
	connect();
	const std::string script(sql);
	execute({"BEGIN IMMEDIATE", {}});
	char* err = nullptr;
	if (sqlite3_exec(db_, script.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
		const std::string msg = err != nullptr ? err : sqlite3_errmsg(db_);
		sqlite3_free(err);
		sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
		throw RepositoryError("script failed: " + msg);
	}
	execute({"COMMIT", {}});
}

bool SqliteConnection::execute_in_transaction(const std::vector<Statement>& statements) {
	connect();
	// IMMEDIATE takes the write lock up front, so two server processes sharing one file never
	// deadlock upgrading a read transaction.
	execute({"BEGIN IMMEDIATE", {}});
	try {
		for (const auto& st : statements) execute(st);
		execute({"COMMIT", {}});
	} catch (const RepositoryError&) {
		sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
		return false;
	}
	return true;
}

} // namespace rathena::persistence
