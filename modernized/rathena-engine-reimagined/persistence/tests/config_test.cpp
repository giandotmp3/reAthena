// Implemented: connection configuration (env-only, redaction, MySQL refusal). Supports BR-INTER-016
// (secrets come from the environment, never a literal).
#include <cstdlib>
#include <string>

#include "rathena/persist/db_config.hpp"
#include "rathena/persist/errors.hpp"
#include "support.hpp"

using namespace rathena::persist;

namespace {
void set_env(const char* k, const char* v) {
#ifdef _WIN32
	_putenv_s(k, v);
#else
	setenv(k, v, 1);
#endif
}
void unset_env(const char* k) {
#ifdef _WIN32
	_putenv_s(k, "");
#else
	unsetenv(k);
#endif
}
} // namespace

TEST(PERSIST_CONFIG_001, DatabaseUrlComesFromEnvironmentOnlyAndHasNoDefault) {
	unset_env("RATHENA_TEST_DB_URL_A");
	EXPECT_THROW(load_db_config_from_env("RATHENA_TEST_DB_URL_A"), DbConfigError);
	set_env("RATHENA_TEST_DB_URL_A", "");
	EXPECT_THROW(load_db_config_from_env("RATHENA_TEST_DB_URL_A"), DbConfigError);
	set_env("RATHENA_TEST_DB_URL_A", "sqlite::memory:");
	EXPECT_EQ(load_db_config_from_env("RATHENA_TEST_DB_URL_A").backend, Backend::Sqlite);
	unset_env("RATHENA_TEST_DB_URL_A");
}

TEST(PERSIST_CONFIG_002, ErrorsAndRedactedUrlNeverContainThePassword) {
	const std::string url = testing_support::kFakePgUrl;
	EXPECT_EQ(parse_db_url(url).backend, Backend::Postgres);
	const std::string red = redact_url(url);
	EXPECT_EQ(red.find("fake-db-pass-0000"), std::string::npos);
	EXPECT_NE(red.find("rathena:***@db.invalid"), std::string::npos);
	try {
		parse_db_url("mysql://rathena:fake-db-pass-0000@db.invalid/x");
		FAIL() << "mysql must be refused";
	} catch (const DbConfigError& e) {
		EXPECT_EQ(std::string(e.what()).find("fake-db-pass-0000"), std::string::npos);
	}
}

TEST(PERSIST_CONFIG_003, OnlySqliteAndPostgresBackendsAreAccepted) {
	EXPECT_EQ(parse_db_url("sqlite:dev/x.db").backend, Backend::Sqlite);
	EXPECT_EQ(parse_db_url("postgres://u:p@h/db").backend, Backend::Postgres);
	EXPECT_THROW(parse_db_url("mariadb://u:p@h/db"), DbConfigError);
	EXPECT_THROW(parse_db_url("oracle://u:p@h/db"), DbConfigError);
	EXPECT_THROW(parse_db_url(""), DbConfigError);
}

TEST(PERSIST_CONFIG_004, IdentifierQuotingRejectsInjectionShapes) {
	EXPECT_EQ(quote_identifier("kv_entries", SqlDialect::Sqlite), "\"kv_entries\"");
	EXPECT_THROW(quote_identifier("a\"; DROP TABLE x;--", SqlDialect::Postgres), std::invalid_argument);
	EXPECT_THROW(quote_identifier("", SqlDialect::Sqlite), std::invalid_argument);
}
