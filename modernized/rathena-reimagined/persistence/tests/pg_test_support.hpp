#pragma once
// PostgreSQL test support. The live tests run only when RATHENA_TEST_PG_URL names a DISPOSABLE
// database (postgresql://user:pass@host:5432/db, created with ENCODING 'SQL_ASCII' LC_COLLATE 'C'
// LC_CTYPE 'C' TEMPLATE template0, like a real rAthena database). Each test works in a private
// schema that is dropped afterwards, so tests can run in parallel (ctest -j) on one database.
#include <cstdlib>
#include <memory>
#include <optional>
#include <random>
#include <string>

#include "rathena/persistence/db_config.hpp"
#include "rathena/persistence/errors.hpp"
#include "rathena/persistence/postgres.hpp"

namespace rathena::persistence::pgtest {

inline std::optional<std::string> pg_test_url() {
	const char* v = std::getenv("RATHENA_TEST_PG_URL");
	if (v == nullptr || *v == '\0') return std::nullopt;
	return std::string(v);
}

/// Why a PostgreSQL test cannot run here, or nullopt if it can.
inline std::optional<std::string> pg_unavailable() {
	if (!postgres_client_available()) return "built without libpq";
	if (!pg_test_url()) return "set RATHENA_TEST_PG_URL to a disposable PostgreSQL database to run";
	return std::nullopt;
}

/// A connection whose search_path is a fresh schema, dropped (with everything in it) on
/// destruction. citext lives in `public`, where the generated schema files expect to find it.
class PgScratchSchema {
public:
	PgScratchSchema() : conn_(std::make_unique<PostgresConnection>(parse_db_url(*pg_test_url()))) {
		std::random_device rd;
		name_ = "rathena_test_" + std::to_string(rd()) + "_" + std::to_string(rd());
		conn_->connect();
		try {
			conn_->execute({"CREATE EXTENSION IF NOT EXISTS citext SCHEMA public", {}});
		} catch (const RepositoryError&) {
			// another test process created it concurrently
		}
		conn_->execute({"CREATE SCHEMA \"" + name_ + "\"", {}});
		conn_->execute({"SET search_path TO \"" + name_ + "\", public", {}});
	}
	~PgScratchSchema() {
		try {
			conn_->execute({"DROP SCHEMA IF EXISTS \"" + name_ + "\" CASCADE", {}});
		} catch (const RepositoryError&) {
			// best effort; the database is disposable
		}
	}
	PgScratchSchema(const PgScratchSchema&) = delete;
	PgScratchSchema& operator=(const PgScratchSchema&) = delete;

	[[nodiscard]] PostgresConnection& conn() noexcept { return *conn_; }
	[[nodiscard]] const std::string& schema() const noexcept { return name_; }

	/// A second connection on the same schema.
	[[nodiscard]] std::unique_ptr<PostgresConnection> another_connection() const {
		auto c = std::make_unique<PostgresConnection>(parse_db_url(*pg_test_url()));
		c->execute({"SET search_path TO \"" + name_ + "\", public", {}});
		return c;
	}

private:
	std::unique_ptr<PostgresConnection> conn_;
	std::string name_;
};

} // namespace rathena::persistence::pgtest
