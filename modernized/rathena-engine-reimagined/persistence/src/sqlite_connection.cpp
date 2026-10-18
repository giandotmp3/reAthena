#include "rathena/persist/sqlite_connection.hpp"

#include <sqlite3.h>

#include <limits>
#include <type_traits>
#include <variant>

#include "rathena/persist/errors.hpp"

namespace rathena::persist {
namespace {

std::string_view trim_tail(const char* tail) {
	std::string_view t = tail == nullptr ? std::string_view{} : std::string_view(tail);
	while (!t.empty() && (t.front() == ' ' || t.front() == '\n' || t.front() == '\r' || t.front() == '\t' || t.front() == ';'))
		t.remove_prefix(1);
	return t;
}

/// Constraint, type-mismatch and size errors mean "this row's data is refused" while the database
/// itself is healthy. Everything else (I/O, busy, readonly, full, corrupt, missing table) is
/// treated as an outage by the writer.
bool is_row_rejection(int rc) {
	switch (rc & 0xff) {
	case SQLITE_CONSTRAINT:
	case SQLITE_MISMATCH:
	case SQLITE_TOOBIG:
		return true;
	default:
		return false;
	}
}

[[noreturn]] void fail(sqlite3* db, int rc, const char* what) {
	const std::string msg = std::string(what) + ": " + (db != nullptr ? sqlite3_errmsg(db) : "sqlite error");
	if (is_row_rejection(rc)) throw RowRejectedError(msg);
	throw RepositoryError(msg);
}

} // namespace

std::string SqliteConnection::library_version() { return sqlite3_libversion(); }

bool postgres_client_available() noexcept {
#ifdef RATHENA_PERSIST_HAVE_LIBPQ
	return true;
#else
	return false;
#endif
}

SqliteConnection::SqliteConnection(std::string path) : path_(std::move(path)) {}

SqliteConnection::~SqliteConnection() {
	clear_cache();
	if (db_ != nullptr) sqlite3_close_v2(db_);
}

void SqliteConnection::clear_cache() noexcept {
	for (auto& [sql, stmt] : cache_) sqlite3_finalize(stmt);
	cache_.clear();
}

void SqliteConnection::connect() {
	if (db_ != nullptr) return;
	const int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX;
	if (sqlite3_open_v2(path_.c_str(), &db_, flags, nullptr) != SQLITE_OK) {
		const std::string err = db_ != nullptr ? sqlite3_errmsg(db_) : "out of memory";
		sqlite3_close_v2(db_);
		db_ = nullptr;
		throw RepositoryError("open sqlite database failed: " + err);
	}
	sqlite3_busy_timeout(db_, 5000);
	sqlite3_extended_result_codes(db_, 1);
	execute({"PRAGMA foreign_keys = ON", {}});
	if (path_ != ":memory:") (void)run({"PRAGMA journal_mode = WAL", {}}, true);
}

sqlite3_stmt* SqliteConnection::prepare_cached(const std::string& sql) {
	if (auto it = cache_.find(sql); it != cache_.end()) return it->second;
	sqlite3_stmt* stmt = nullptr;
	const char* tail = nullptr;
	const int rc = sqlite3_prepare_v2(db_, sql.c_str(), static_cast<int>(sql.size()), &stmt, &tail);
	if (rc != SQLITE_OK) fail(db_, rc, "prepare failed");
	if (stmt == nullptr) throw RepositoryError("empty statement");
	if (!trim_tail(tail).empty()) {
		sqlite3_finalize(stmt);
		throw RepositoryError("only one SQL statement per call is allowed");
	}
	if (cache_.size() >= 256) clear_cache();  // bounded: dynamic SQL cannot grow it forever
	cache_.emplace(sql, stmt);
	return stmt;
}

std::vector<Row> SqliteConnection::run(const Statement& st, bool want_rows) {
	connect();
	sqlite3_stmt* stmt = prepare_cached(st.sql);
	struct Reset {
		sqlite3_stmt* s;
		~Reset() { sqlite3_reset(s); sqlite3_clear_bindings(s); }
	} reset{stmt};
	if (sqlite3_bind_parameter_count(stmt) != static_cast<int>(st.params.size()))
		throw RepositoryError("parameter count mismatch: statement expects " +
		                      std::to_string(sqlite3_bind_parameter_count(stmt)) + ", got " + std::to_string(st.params.size()));

	for (std::size_t i = 0; i < st.params.size(); ++i) {
		const int idx = static_cast<int>(i) + 1;
		int rc = SQLITE_OK;
		std::visit(
		    [&](const auto& v) {
			    using T = std::decay_t<decltype(v)>;
			    if constexpr (std::is_same_v<T, std::monostate>) {
				    rc = sqlite3_bind_null(stmt, idx);
			    } else if constexpr (std::is_same_v<T, std::int64_t>) {
				    rc = sqlite3_bind_int64(stmt, idx, v);
			    } else if constexpr (std::is_same_v<T, std::uint64_t>) {
				    if (v > static_cast<std::uint64_t>(std::numeric_limits<sqlite3_int64>::max()))
					    throw RowRejectedError("unsigned value out of SQLite integer range");
				    rc = sqlite3_bind_int64(stmt, idx, static_cast<sqlite3_int64>(v));
			    } else if constexpr (std::is_same_v<T, Blob>) {
				    rc = sqlite3_bind_blob64(stmt, idx, v.bytes.data(), v.bytes.size(), SQLITE_TRANSIENT);
			    } else {
				    rc = sqlite3_bind_text64(stmt, idx, v.data(), v.size(), SQLITE_TRANSIENT, SQLITE_UTF8);
			    }
		    },
		    st.params[i]);
		if (rc != SQLITE_OK) fail(db_, rc, "bind failed");
	}

	std::vector<Row> rows;
	for (;;) {
		const int rc = sqlite3_step(stmt);
		if (rc == SQLITE_DONE) break;
		if (rc != SQLITE_ROW) fail(db_, rc, "execute failed");
		if (!want_rows) continue;
		Row row;
		const int ncols = sqlite3_column_count(stmt);
		for (int c = 0; c < ncols; ++c) {
			SqlValue v;
			switch (sqlite3_column_type(stmt, c)) {
			case SQLITE_INTEGER: v = static_cast<std::int64_t>(sqlite3_column_int64(stmt, c)); break;
			case SQLITE_NULL: break;
			case SQLITE_BLOB: {
				const auto* bytes = static_cast<const char*>(sqlite3_column_blob(stmt, c));
				v = Blob{std::string(bytes == nullptr ? "" : bytes, static_cast<std::size_t>(sqlite3_column_bytes(stmt, c)))};
				break;
			}
			default: {
				const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, c));
				v = std::string(text == nullptr ? "" : text, static_cast<std::size_t>(sqlite3_column_bytes(stmt, c)));
			}
			}
			row.emplace(sqlite3_column_name(stmt, c), std::move(v));
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
	const int rc = sqlite3_exec(db_, script.c_str(), nullptr, nullptr, &err);
	if (rc != SQLITE_OK) {
		const std::string msg = err != nullptr ? err : sqlite3_errmsg(db_);
		sqlite3_free(err);
		sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
		if (is_row_rejection(rc)) throw RowRejectedError("script failed: " + msg);
		throw RepositoryError("script failed: " + msg);
	}
	execute({"COMMIT", {}});
}

void SqliteConnection::transaction(const std::function<void()>& body) {
	connect();
	// IMMEDIATE takes the write lock up front, so two server processes sharing one file never
	// deadlock upgrading a read transaction.
	execute({"BEGIN IMMEDIATE", {}});
	try {
		body();
		execute({"COMMIT", {}});
	} catch (...) {
		sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
		throw;
	}
}

bool SqliteConnection::execute_in_transaction(const std::vector<Statement>& statements) {
	try {
		transaction([&] {
			for (const auto& st : statements) execute(st);
		});
	} catch (const RepositoryError&) {
		return false;
	}
	return true;
}

} // namespace rathena::persist
