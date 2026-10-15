// PostgresConnection over libpq. Compiled only when CMake finds libpq (see
// RATHENA_PERSISTENCE_WITH_POSTGRESQL); postgres_connection_stub.cpp is used otherwise.
//
// Errors never include the password: libpq's messages do not echo it, and the endpoint is only
// ever printed redacted.
#include "rathena/persistence/errors.hpp"
#include "rathena/persistence/postgres.hpp"

#include <libpq-fe.h>

#include <cstdlib>
#include <cstring>
#include <type_traits>
#include <variant>

namespace rathena::persistence {

bool postgres_client_available() noexcept { return true; }

struct PostgresConnection::Impl {
	PGconn* conn = nullptr;
	~Impl() {
		if (conn != nullptr) PQfinish(conn);
	}
};

namespace {

// Type OIDs from pg_type.h (stable across releases).
constexpr Oid kBoolOid = 16;
constexpr Oid kByteaOid = 17;
constexpr Oid kInt8Oid = 20;
constexpr Oid kInt2Oid = 21;
constexpr Oid kInt4Oid = 23;

struct ResultGuard {
	PGresult* res;
	explicit ResultGuard(PGresult* r) : res(r) {}
	~ResultGuard() { PQclear(res); }
	ResultGuard(const ResultGuard&) = delete;
	ResultGuard& operator=(const ResultGuard&) = delete;
};

/// NOTICEs (e.g. "extension citext already exists, skipping") are not errors; keep stderr clean.
void ignore_notice(void*, const char*) {}

std::string trimmed(const char* msg) {
	std::string s = msg == nullptr ? std::string{} : std::string(msg);
	while (!s.empty() && (s.back() == '\n' || s.back() == ' ')) s.pop_back();
	return s;
}

bool ok(const PGresult* res) {
	const auto st = PQresultStatus(res);
	return st == PGRES_COMMAND_OK || st == PGRES_TUPLES_OK;
}

} // namespace

PostgresConnection::PostgresConnection(DbEndpoint endpoint) : endpoint_(std::move(endpoint)), impl_(std::make_unique<Impl>()) {}
PostgresConnection::~PostgresConnection() = default;

void PostgresConnection::connect() {
	if (impl_->conn != nullptr) return;
	const std::string port = std::to_string(endpoint_.port);
	std::vector<const char*> keys = {"host", "port", "user", "password", "dbname"};
	std::vector<const char*> values = {endpoint_.host.c_str(), port.c_str(), endpoint_.user.c_str(),
	                                   endpoint_.password.c_str(), endpoint_.database.c_str()};
	if (!endpoint_.params.contains("connect_timeout")) {
		keys.push_back("connect_timeout");
		values.push_back("10");
	}
	for (const auto& [k, v] : endpoint_.params) {
		keys.push_back(k.c_str());
		values.push_back(v.c_str());
	}
	keys.push_back(nullptr);
	values.push_back(nullptr);
	PGconn* c = PQconnectdbParams(keys.data(), values.data(), 0);
	if (c == nullptr) throw RepositoryError("PQconnectdbParams failed (out of memory)");
	if (PQstatus(c) != CONNECTION_OK) {
		const std::string err = trimmed(PQerrorMessage(c));
		PQfinish(c);
		throw RepositoryError("connect to " + endpoint_.redacted() + " failed: " + err);
	}
	PQsetNoticeProcessor(c, ignore_notice, nullptr);
	impl_->conn = c;
}

std::vector<Row> PostgresConnection::run(const Statement& st, bool want_rows) {
	connect();
	std::size_t n = 0;
	const std::string sql = to_postgres_placeholders(st.sql, &n);
	if (n != st.params.size())
		throw RepositoryError("parameter count mismatch: statement expects " + std::to_string(n) + ", got " +
		                      std::to_string(st.params.size()));

	// Text format with the type left to the server (Oid 0), so one value works for any column
	// type; Blob goes as binary bytea.
	std::vector<std::string> storage(n);
	std::vector<const char*> values(n, nullptr);
	std::vector<int> lengths(n, 0);
	std::vector<int> formats(n, 0);
	std::vector<Oid> types(n, 0);
	for (std::size_t i = 0; i < n; ++i) {
		std::visit(
		    [&](const auto& v) {
			    using T = std::decay_t<decltype(v)>;
			    if constexpr (std::is_same_v<T, std::monostate>) {
				    values[i] = nullptr;
			    } else if constexpr (std::is_same_v<T, std::int64_t> || std::is_same_v<T, std::uint64_t>) {
				    storage[i] = std::to_string(v);
				    values[i] = storage[i].c_str();
			    } else if constexpr (std::is_same_v<T, Blob>) {
				    values[i] = v.bytes.data();
				    lengths[i] = static_cast<int>(v.bytes.size());
				    formats[i] = 1;
				    types[i] = kByteaOid;
			    } else {
				    if (v.find('\0') != std::string::npos)
					    throw RepositoryError("text value contains a NUL byte (not storable on PostgreSQL)");
				    values[i] = v.c_str();
			    }
		    },
		    st.params[i]);
	}

	PGresult* raw = PQexecParams(impl_->conn, sql.c_str(), static_cast<int>(n), types.data(), values.data(),
	                             lengths.data(), formats.data(), 0);
	if (raw == nullptr) throw RepositoryError("execute failed: " + trimmed(PQerrorMessage(impl_->conn)));
	const ResultGuard g(raw);
	if (!ok(raw)) throw RepositoryError("execute failed: " + trimmed(PQresultErrorMessage(raw)));

	const char* tuples = PQcmdTuples(raw);
	affected_rows_ = (tuples != nullptr && *tuples != '\0') ? std::strtoull(tuples, nullptr, 10) : 0;

	std::vector<Row> rows;
	if (!want_rows || PQresultStatus(raw) != PGRES_TUPLES_OK) return rows;
	const int nrows = PQntuples(raw);
	const int ncols = PQnfields(raw);
	rows.reserve(static_cast<std::size_t>(nrows));
	for (int r = 0; r < nrows; ++r) {
		Row row;
		for (int c = 0; c < ncols; ++c) {
			SqlValue v;
			if (!PQgetisnull(raw, r, c)) {
				const char* text = PQgetvalue(raw, r, c);
				const auto len = static_cast<std::size_t>(PQgetlength(raw, r, c));
				switch (PQftype(raw, c)) {
				case kInt2Oid:
				case kInt4Oid:
				case kInt8Oid: v = static_cast<std::int64_t>(std::strtoll(text, nullptr, 10)); break;
				case kBoolOid: v = std::int64_t{text[0] == 't' ? 1 : 0}; break;
				case kByteaOid: {
					std::size_t out_len = 0;
					unsigned char* bytes = PQunescapeBytea(reinterpret_cast<const unsigned char*>(text), &out_len);
					if (bytes == nullptr) throw RepositoryError("bytea decode failed");
					v = Blob{std::string(reinterpret_cast<const char*>(bytes), out_len)};
					PQfreemem(bytes);
					break;
				}
				default: v = std::string(text, len);
				}
			}
			row.emplace(PQfname(raw, c), std::move(v));
		}
		rows.push_back(std::move(row));
	}
	return rows;
}

void PostgresConnection::execute(const Statement& st) { (void)run(st, false); }

std::vector<Row> PostgresConnection::query(const Statement& st) { return run(st, true); }

void PostgresConnection::transaction(const std::function<void()>& body) {
	execute({"BEGIN", {}});
	try {
		body();
		execute({"COMMIT", {}});
	} catch (...) {
		PQclear(PQexec(impl_->conn, "ROLLBACK"));
		throw;
	}
}

bool PostgresConnection::execute_in_transaction(const std::vector<Statement>& statements) {
	try {
		transaction([&] {
			for (const auto& st : statements) execute(st);
		});
	} catch (const RepositoryError&) {
		return false;
	}
	return true;
}

void PostgresConnection::execute_script(std::string_view sql) {
	connect();
	const std::string script(sql);
	execute({"BEGIN", {}});
	PGresult* raw = PQexec(impl_->conn, script.c_str());
	if (raw == nullptr || !ok(raw)) {
		const std::string err = raw == nullptr ? trimmed(PQerrorMessage(impl_->conn)) : trimmed(PQresultErrorMessage(raw));
		PQclear(raw);
		PQclear(PQexec(impl_->conn, "ROLLBACK"));
		throw RepositoryError("script failed: " + err);
	}
	PQclear(raw);
	execute({"COMMIT", {}});
}

} // namespace rathena::persistence
