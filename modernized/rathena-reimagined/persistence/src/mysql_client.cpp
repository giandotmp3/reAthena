// MysqlConnection over the MySQL/MariaDB C client API. Compiled only when CMake finds a client
// library (see RATHENA_PERSISTENCE_WITH_MYSQL); mysql_client_stub.cpp is used otherwise.
//
// Every statement goes through mysql_stmt_prepare/bind: parameters are sent separately from the
// SQL text, so no value is ever spliced into a query. Errors never include the password.
#include "rathena/persistence/errors.hpp"
#include "rathena/persistence/mysql.hpp"

#include <mysql.h>

// MySQL 8 removed my_bool (the API takes bool); MariaDB Connector/C still defines it.
#if !defined(MARIADB_PACKAGE_VERSION) && !defined(MARIADB_BASE_VERSION) && defined(MYSQL_VERSION_ID) && MYSQL_VERSION_ID >= 80000
using my_bool = bool;
#endif

#include <cstring>
#include <string>
#include <type_traits>
#include <variant>

namespace rathena::persistence {

bool mysql_client_available() noexcept { return true; }

struct MysqlConnection::Impl {
	MYSQL* handle = nullptr;
	~Impl() {
		if (handle != nullptr) mysql_close(handle);
	}
};

namespace {

/// RAII for a prepared statement handle.
struct StmtHandle {
	MYSQL_STMT* stmt = nullptr;
	explicit StmtHandle(MYSQL* db) : stmt(mysql_stmt_init(db)) {}
	~StmtHandle() {
		if (stmt != nullptr) mysql_stmt_close(stmt);
	}
	StmtHandle(const StmtHandle&) = delete;
	StmtHandle& operator=(const StmtHandle&) = delete;
};

[[noreturn]] void fail(const std::string& what, MYSQL_STMT* stmt) {
	throw RepositoryError(what + ": " + mysql_stmt_error(stmt) + " (errno " + std::to_string(mysql_stmt_errno(stmt)) + ")");
}

/// Parameter storage that outlives mysql_stmt_bind_param/execute.
struct ParamBuffers {
	std::vector<MYSQL_BIND> binds;
	std::vector<long long> ints;
	std::vector<unsigned long long> uints;
	std::vector<unsigned long> lengths;
	std::vector<my_bool> nulls;

	explicit ParamBuffers(const std::vector<SqlValue>& params)
	    : binds(params.size()), ints(params.size()), uints(params.size()), lengths(params.size()),
	      nulls(params.size()) {
		for (std::size_t i = 0; i < params.size(); ++i) {
			MYSQL_BIND& b = binds[i];
			std::memset(&b, 0, sizeof b);
			std::visit(
			    [&](const auto& v) {
				    using T = std::decay_t<decltype(v)>;
				    if constexpr (std::is_same_v<T, std::monostate>) {
					    nulls[i] = 1;
					    b.buffer_type = MYSQL_TYPE_NULL;
					    b.is_null = &nulls[i];
				    } else if constexpr (std::is_same_v<T, std::int64_t>) {
					    ints[i] = v;
					    b.buffer_type = MYSQL_TYPE_LONGLONG;
					    b.buffer = &ints[i];
				    } else if constexpr (std::is_same_v<T, std::uint64_t>) {
					    uints[i] = v;
					    b.buffer_type = MYSQL_TYPE_LONGLONG;
					    b.buffer = &uints[i];
					    b.is_unsigned = 1;
				    } else {
					    lengths[i] = static_cast<unsigned long>(v.size());
					    b.buffer_type = MYSQL_TYPE_STRING;
					    b.buffer = const_cast<char*>(v.data()); // the client only reads it
					    b.buffer_length = lengths[i];
					    b.length = &lengths[i];
				    }
			    },
			    params[i]);
		}
	}
};

bool is_integer_type(enum_field_types t) {
	switch (t) {
	case MYSQL_TYPE_TINY:
	case MYSQL_TYPE_SHORT:
	case MYSQL_TYPE_INT24:
	case MYSQL_TYPE_LONG:
	case MYSQL_TYPE_LONGLONG:
	case MYSQL_TYPE_YEAR:
		return true;
	default:
		return false;
	}
}

/// Prepares, binds and executes `st`. The caller fetches results if any.
void prepare_and_execute(MYSQL_STMT* stmt, const Statement& st, ParamBuffers& params) {
	if (mysql_stmt_prepare(stmt, st.sql.data(), static_cast<unsigned long>(st.sql.size())) != 0)
		fail("prepare failed", stmt);
	if (mysql_stmt_param_count(stmt) != st.params.size())
		throw RepositoryError("parameter count mismatch: statement expects " +
		                      std::to_string(mysql_stmt_param_count(stmt)) + ", got " + std::to_string(st.params.size()));
	if (!st.params.empty() && mysql_stmt_bind_param(stmt, params.binds.data()) != 0) fail("bind failed", stmt);
	if (mysql_stmt_execute(stmt) != 0) fail("execute failed", stmt);
}

} // namespace

MysqlConnection::MysqlConnection(DbEndpoint endpoint) : endpoint_(std::move(endpoint)), impl_(std::make_unique<Impl>()) {}
MysqlConnection::~MysqlConnection() = default;

void MysqlConnection::connect() {
	if (impl_->handle != nullptr) return;
	MYSQL* db = mysql_init(nullptr);
	if (db == nullptr) throw RepositoryError("mysql_init failed (out of memory)");
	const unsigned int timeout_s = 5;
	mysql_options(db, MYSQL_OPT_CONNECT_TIMEOUT, &timeout_s);
	mysql_options(db, MYSQL_SET_CHARSET_NAME, "utf8mb4");
	if (mysql_real_connect(db, endpoint_.host.c_str(), endpoint_.user.c_str(), endpoint_.password.c_str(),
	                       endpoint_.database.c_str(), endpoint_.port, nullptr, 0) == nullptr) {
		const std::string err = mysql_error(db);
		mysql_close(db);
		// mysql_error() never echoes the password; the endpoint is printed redacted.
		throw RepositoryError("connect to " + endpoint_.redacted() + " failed: " + err);
	}
	impl_->handle = db;
}

void MysqlConnection::execute(const Statement& st) {
	connect();
	StmtHandle h(impl_->handle);
	if (h.stmt == nullptr) throw RepositoryError("mysql_stmt_init failed");
	ParamBuffers params(st.params);
	prepare_and_execute(h.stmt, st, params);
	affected_rows_ = mysql_stmt_affected_rows(h.stmt);
}

std::vector<Row> MysqlConnection::query(const Statement& st) {
	connect();
	StmtHandle h(impl_->handle);
	if (h.stmt == nullptr) throw RepositoryError("mysql_stmt_init failed");
	ParamBuffers params(st.params);
	prepare_and_execute(h.stmt, st, params);

	MYSQL_RES* meta = mysql_stmt_result_metadata(h.stmt);
	if (meta == nullptr) return {}; // statement produced no result set
	struct MetaGuard {
		MYSQL_RES* r;
		~MetaGuard() { mysql_free_result(r); }
	} guard{meta};

	my_bool update_max = 1;
	mysql_stmt_attr_set(h.stmt, STMT_ATTR_UPDATE_MAX_LENGTH, &update_max);
	if (mysql_stmt_store_result(h.stmt) != 0) fail("store_result failed", h.stmt);

	const unsigned int ncols = mysql_num_fields(meta);
	MYSQL_FIELD* fields = mysql_fetch_fields(meta);
	std::vector<MYSQL_BIND> binds(ncols);
	std::vector<std::string> buffers(ncols);
	std::vector<unsigned long> lengths(ncols);
	std::vector<my_bool> nulls(ncols);
	for (unsigned int c = 0; c < ncols; ++c) {
		buffers[c].resize(fields[c].max_length + 1); // every column is fetched as text
		std::memset(&binds[c], 0, sizeof binds[c]);
		binds[c].buffer_type = MYSQL_TYPE_STRING;
		binds[c].buffer = buffers[c].data();
		binds[c].buffer_length = static_cast<unsigned long>(buffers[c].size());
		binds[c].length = &lengths[c];
		binds[c].is_null = &nulls[c];
	}
	if (ncols > 0 && mysql_stmt_bind_result(h.stmt, binds.data()) != 0) fail("bind_result failed", h.stmt);

	std::vector<Row> rows;
	for (;;) {
		const int rc = mysql_stmt_fetch(h.stmt);
		if (rc == MYSQL_NO_DATA) break;
		if (rc == 1) fail("fetch failed", h.stmt);
		// MYSQL_DATA_TRUNCATED cannot happen: buffers are sized from max_length.
		Row row;
		for (unsigned int c = 0; c < ncols; ++c) {
			SqlValue v;
			if (!nulls[c]) {
				std::string text(buffers[c].data(), lengths[c]);
				if (is_integer_type(fields[c].type)) {
					if ((fields[c].flags & UNSIGNED_FLAG) != 0) v = static_cast<std::uint64_t>(std::stoull(text));
					else v = static_cast<std::int64_t>(std::stoll(text));
				} else {
					v = std::move(text);
				}
			}
			row.emplace(fields[c].name, std::move(v));
		}
		rows.push_back(std::move(row));
	}
	return rows;
}

bool MysqlConnection::execute_in_transaction(const std::vector<Statement>& statements) {
	connect();
	if (mysql_query(impl_->handle, "START TRANSACTION") != 0)
		throw RepositoryError(std::string("START TRANSACTION failed: ") + mysql_error(impl_->handle));
	try {
		for (const auto& st : statements) execute(st);
	} catch (const RepositoryError&) {
		mysql_query(impl_->handle, "ROLLBACK");
		return false;
	}
	if (mysql_query(impl_->handle, "COMMIT") != 0) {
		mysql_query(impl_->handle, "ROLLBACK");
		return false;
	}
	return true;
}

} // namespace rathena::persistence
