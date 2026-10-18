// PostgreSQL backend over libpq. Compiled only when CMake finds libpq-fe.h (see
// RATHENA_PERSIST_WITH_POSTGRESQL). NOT built or run in the environment this unit was written
// in (no libpq headers, no server): it is checked only by the contract test when
// RATHENA_TEST_PG_URL names a disposable database.
#include <libpq-fe.h>

#include <cstdlib>
#include <cstring>
#include <type_traits>
#include <variant>

#include "rathena/persist/errors.hpp"
#include "rathena/persist/sql_connection.hpp"

namespace rathena::persist {
namespace {

struct ResultGuard {
	PGresult* res;
	explicit ResultGuard(PGresult* r) : res(r) {}
	~ResultGuard() { PQclear(res); }
	ResultGuard(const ResultGuard&) = delete;
	ResultGuard& operator=(const ResultGuard&) = delete;
};

constexpr Oid kByteaOid = 17;
constexpr Oid kInt8Oid = 20;
constexpr Oid kInt2Oid = 21;
constexpr Oid kInt4Oid = 23;

std::string rewrite_placeholders(const std::string& sql) {
	std::string out;
	int n = 0;
	bool in_str = false;
	for (char c : sql) {
		if (c == '\'') in_str = !in_str;
		if (c == '?' && !in_str) out += "$" + std::to_string(++n);
		else out += c;
	}
	return out;
}

class PostgresConnection final : public SqlConnection {
public:
	explicit PostgresConnection(std::string url) : url_(std::move(url)) {}
	~PostgresConnection() override { if (conn_ != nullptr) PQfinish(conn_); }
	[[nodiscard]] SqlDialect dialect() const noexcept override { return SqlDialect::Postgres; }

	void connect() override {
		if (conn_ != nullptr) return;
		conn_ = PQconnectdb(url_.c_str());
		if (conn_ == nullptr || PQstatus(conn_) != CONNECTION_OK) {
			const std::string msg = conn_ != nullptr ? PQerrorMessage(conn_) : "out of memory";
			if (conn_ != nullptr) PQfinish(conn_);
			conn_ = nullptr;
			throw RepositoryError("PostgreSQL connection failed: " + msg);  // libpq does not echo the password
		}
	}

	void execute(const Statement& st) override { (void)run(st); }
	std::vector<Row> query(const Statement& st) override { return run(st); }
	[[nodiscard]] std::uint64_t affected_rows() const noexcept override { return affected_; }

	bool execute_in_transaction(const std::vector<Statement>& statements) override {
		try {
			transaction([&] { for (const auto& st : statements) execute(st); });
		} catch (const RepositoryError&) {
			return false;
		}
		return true;
	}

	void transaction(const std::function<void()>& body) override {
		execute({"BEGIN", {}});
		try {
			body();
			execute({"COMMIT", {}});
		} catch (...) {
			if (conn_ != nullptr) { ResultGuard g(PQexec(conn_, "ROLLBACK")); }
			throw;
		}
	}

	void execute_script(std::string_view sql) override {
		connect();
		const std::string script = "BEGIN;" + std::string(sql) + ";COMMIT;";
		ResultGuard g(PQexec(conn_, script.c_str()));
		if (PQresultStatus(g.res) != PGRES_COMMAND_OK && PQresultStatus(g.res) != PGRES_TUPLES_OK) {
			const std::string msg = PQerrorMessage(conn_);
			ResultGuard rb(PQexec(conn_, "ROLLBACK"));
			throw RepositoryError("script failed: " + msg);
		}
	}

private:
	std::vector<Row> run(const Statement& st) {
		connect();
		const std::string sql = rewrite_placeholders(st.sql);
		const auto n = st.params.size();
		std::vector<std::string> store(n);
		std::vector<const char*> values(n, nullptr);
		std::vector<int> lengths(n, 0), formats(n, 0);
		for (std::size_t i = 0; i < n; ++i) {
			std::visit([&](const auto& v) {
				using T = std::decay_t<decltype(v)>;
				if constexpr (std::is_same_v<T, std::monostate>) {
					values[i] = nullptr;
				} else if constexpr (std::is_same_v<T, std::int64_t> || std::is_same_v<T, std::uint64_t>) {
					store[i] = std::to_string(v);
					values[i] = store[i].c_str();
				} else if constexpr (std::is_same_v<T, Blob>) {
					store[i] = v.bytes;
					values[i] = store[i].data();
					lengths[i] = static_cast<int>(store[i].size());
					formats[i] = 1;
				} else {
					store[i] = v;
					values[i] = store[i].c_str();
				}
			}, st.params[i]);
		}
		ResultGuard g(PQexecParams(conn_, sql.c_str(), static_cast<int>(n), nullptr, values.data(), lengths.data(), formats.data(), 0));
		const auto status = PQresultStatus(g.res);
		if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK) {
			const std::string msg = PQerrorMessage(conn_);
			const char* state = PQresultErrorField(g.res, PG_DIAG_SQLSTATE);
			if (state != nullptr && std::strncmp(state, "23", 2) == 0) throw RowRejectedError("execute failed: " + msg);
			throw RepositoryError("execute failed: " + msg);
		}
		const char* aff = PQcmdTuples(g.res);
		affected_ = (aff != nullptr && *aff != '\0') ? std::strtoull(aff, nullptr, 10) : 0;
		std::vector<Row> rows;
		for (int r = 0; r < PQntuples(g.res); ++r) {
			Row row;
			for (int c = 0; c < PQnfields(g.res); ++c) {
				SqlValue v;
				if (!PQgetisnull(g.res, r, c)) {
					const char* txt = PQgetvalue(g.res, r, c);
					const auto oid = PQftype(g.res, c);
					if (oid == kInt8Oid || oid == kInt4Oid || oid == kInt2Oid) v = static_cast<std::int64_t>(std::strtoll(txt, nullptr, 10));
					else if (oid == kByteaOid) v = Blob{unescape_bytea(txt)};
					else v = std::string(txt, static_cast<std::size_t>(PQgetlength(g.res, r, c)));
				}
				row.emplace(PQfname(g.res, c), std::move(v));
			}
			rows.push_back(std::move(row));
		}
		return rows;
	}

	static std::string unescape_bytea(const char* txt) {
		std::size_t len = 0;
		unsigned char* raw = PQunescapeBytea(reinterpret_cast<const unsigned char*>(txt), &len);
		std::string out(reinterpret_cast<const char*>(raw), len);
		PQfreemem(raw);
		return out;
	}

	std::string url_;
	PGconn* conn_{nullptr};
	std::uint64_t affected_{0};
};

} // namespace

std::unique_ptr<SqlConnection> make_postgres_connection(std::string_view url) {
	auto c = std::make_unique<PostgresConnection>(std::string(url));
	c->connect();
	return c;
}

} // namespace rathena::persist
