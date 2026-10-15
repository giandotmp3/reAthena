// S1: database credentials come from the environment only (${DATABASE_URL}, ${LOG_DB_URL}),
// never from a literal; the password never appears in logs or error text.
// All values are FAKE same-shape placeholders.
#include <gtest/gtest.h>

#include "rathena/persistence/db_config.hpp"

using namespace rathena::persistence;

namespace {
constexpr const char* kFakeMain = "postgresql://rathena:fake-db-pass-0000@db.invalid:5432/rathena_main";
constexpr const char* kFakeLog = "postgres://rathena_log:fake-log-pass-1111@logdb.invalid:5433/rathena_log";
constexpr const char* kFakeMysql = "mysql://rathena:fake-db-pass-0000@db.invalid:3306/rathena_main";
} // namespace

TEST(S1_DbConfig, S1_RefusesWhenDatabaseUrlUnset) {
	MapEnvironment env;
	EXPECT_THROW((void)DbConnectionConfig::from_environment(env), DbConfigError);
	env.set("DATABASE_URL", "");
	EXPECT_THROW((void)DbConnectionConfig::from_environment(env), DbConfigError);
}

TEST(S1_DbConfig, S1_ReadsMainAndLogFromEnvironment) {
	MapEnvironment env;
	env.set("DATABASE_URL", kFakeMain);
	env.set("LOG_DB_URL", kFakeLog);
	const auto cfg = DbConnectionConfig::from_environment(env);
	EXPECT_EQ(cfg.main.host, "db.invalid");
	EXPECT_EQ(cfg.main.port, 5432);
	EXPECT_EQ(cfg.main.database, "rathena_main");
	EXPECT_EQ(cfg.log.host, "logdb.invalid");
	EXPECT_EQ(cfg.log.port, 5433);
}

TEST(S1_DbConfig, S1_LogDbDefaultsToMain) {
	MapEnvironment env;
	env.set("DATABASE_URL", kFakeMain);
	const auto cfg = DbConnectionConfig::from_environment(env);
	EXPECT_EQ(cfg.log_url, cfg.main_url);
	EXPECT_EQ(cfg.log.host, cfg.main.host);
	EXPECT_EQ(cfg.log.database, cfg.main.database);
}

TEST(S1_DbConfig, S1_ProcessOverrideWinsOverDatabaseUrl) {
	MapEnvironment env;
	env.set("DATABASE_URL", kFakeMain);
	env.set("LOGIN_DB_URL", "postgresql://login:fake-login-pass-2222@login-db.invalid/login_db");
	const auto cfg = DbConnectionConfig::from_environment(env, ServerProcess::Login);
	EXPECT_EQ(cfg.main.host, "login-db.invalid");
	EXPECT_EQ(cfg.main.port, 5432) << "PostgreSQL default port";
}

TEST(S1_DbConfig, S1_MysqlIsNoLongerARuntimeBackend) {
	MapEnvironment env;
	env.set("DATABASE_URL", kFakeMysql);
	try {
		(void)DbConnectionConfig::from_environment(env);
		ADD_FAILURE() << "accepted a mysql:// DATABASE_URL";
	} catch (const DbConfigError& e) {
		const std::string what = e.what();
		EXPECT_NE(what.find("no longer supported"), std::string::npos) << what;
		EXPECT_NE(what.find("rathena-migrate import"), std::string::npos) << what;
		EXPECT_EQ(what.find("fake-db-pass-0000"), std::string::npos);
	}
	env.set("DATABASE_URL", kFakeMain);
	env.set("LOG_DB_URL", kFakeMysql);
	EXPECT_THROW((void)DbConnectionConfig::from_environment(env), DbConfigError) << "LOG_DB_URL too";
}

TEST(S1_DbConfig, S1_PasswordMayContainSeparators) {
	const auto ep = parse_db_url("postgresql://u:p@ss:w0rd@h.invalid:1/db");
	EXPECT_EQ(ep.user, "u");
	EXPECT_EQ(ep.password, "p@ss:w0rd");
	EXPECT_EQ(ep.host, "h.invalid");
	EXPECT_EQ(ep.port, 1);
}

TEST(S1_DbConfig, S1_PercentEscapesAreDecoded) {
	const auto ep = parse_db_url("postgresql://us%40er:p%2Fw%3Ad%25@h.invalid/my%20db");
	EXPECT_EQ(ep.user, "us@er");
	EXPECT_EQ(ep.password, "p/w:d%");
	EXPECT_EQ(ep.database, "my db");
	EXPECT_EQ(parse_db_url("postgresql://u:100%zz@h.invalid/db").password, "100%zz") << "not an escape: kept";
}

TEST(S1_DbConfig, S1_SchemesAndDefaultPorts) {
	EXPECT_EQ(parse_db_url("postgres://u:p@h.invalid/db").scheme, DbScheme::Postgres);
	EXPECT_EQ(parse_db_url("postgres://u:p@h.invalid/db").port, 5432);
	EXPECT_EQ(parse_db_url("postgresql://u:p@h.invalid/db").port, 5432);
	EXPECT_EQ(parse_db_url("mysql://u:p@h.invalid/db").scheme, DbScheme::MySql) << "importer source only";
	EXPECT_EQ(parse_db_url("mysql://u:p@h.invalid/db").port, 3306);
	EXPECT_TRUE(is_postgres_url("postgres://x"));
	EXPECT_TRUE(is_postgres_url("postgresql://x"));
	EXPECT_FALSE(is_postgres_url("sqlite:x.db"));
	EXPECT_TRUE(is_mysql_url("mysql://x"));
}

TEST(S1_DbConfig, S1_PostgresParametersAreWhitelisted) {
	const auto ep = parse_db_url("postgresql://u:p@h.invalid/db?sslmode=require&application_name=rathena");
	EXPECT_EQ(ep.database, "db");
	EXPECT_EQ(ep.params.at("sslmode"), "require");
	EXPECT_EQ(ep.params.at("application_name"), "rathena");
	EXPECT_THROW((void)parse_db_url("postgresql://u:p@h.invalid/db?host=/tmp"), DbConfigError);
	EXPECT_THROW((void)parse_db_url("mysql://u:p@h.invalid/db?sslmode=require"), DbConfigError);
}

TEST(S1_DbConfig, S1_RedactedFormNeverContainsPassword) {
	EXPECT_EQ(parse_db_url(kFakeMain).redacted(), "postgresql://rathena:***@db.invalid:5432/rathena_main");
	EXPECT_EQ(parse_db_url(kFakeLog).redacted(), "postgresql://rathena_log:***@logdb.invalid:5433/rathena_log");
	EXPECT_EQ(parse_db_url(kFakeMysql).redacted(), "mysql://rathena:***@db.invalid:3306/rathena_main");
}

TEST(S1_DbConfig, S1_MalformedUrlErrorDoesNotLeakPassword) {
	for (const char* bad : {"redis://rathena:fake-db-pass-0000@db.invalid/x",
	                        "postgresql://rathena:fake-db-pass-0000@db.invalid:99999/x",
	                        "postgresql://rathena:fake-db-pass-0000@db.invalid:5432",
	                        "postgresql://rathena:fake-db-pass-0000@:5432/x",
	                        "postgresql://rathena:fake-db-pass-0000@db.invalid/x?password=fake-db-pass-0000",
	                        "mysql://rathena:fake-db-pass-0000@db.invalid:3306"}) {
		try {
			(void)parse_db_url(bad);
			ADD_FAILURE() << "accepted malformed URL";
		} catch (const DbConfigError& e) {
			EXPECT_EQ(std::string(e.what()).find("fake-db-pass-0000"), std::string::npos);
		}
	}
}
