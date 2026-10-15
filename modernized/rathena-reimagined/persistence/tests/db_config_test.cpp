// S1: database credentials come from the environment only (${DATABASE_URL}, ${LOG_DB_URL}),
// never from a literal; the password never appears in logs or error text.
// All values are FAKE same-shape placeholders.
#include <gtest/gtest.h>

#include "rathena/persistence/db_config.hpp"

using namespace rathena::persistence;

namespace {
constexpr const char* kFakeMain = "mysql://rathena:fake-db-pass-0000@db.invalid:3306/rathena_main";
constexpr const char* kFakeLog = "mysql://rathena_log:fake-log-pass-1111@logdb.invalid:3307/rathena_log";
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
	EXPECT_EQ(cfg.main.port, 3306);
	EXPECT_EQ(cfg.main.database, "rathena_main");
	EXPECT_EQ(cfg.log.host, "logdb.invalid");
	EXPECT_EQ(cfg.log.port, 3307);
}

TEST(S1_DbConfig, S1_LogDbDefaultsToMain) {
	MapEnvironment env;
	env.set("DATABASE_URL", kFakeMain);
	const auto cfg = DbConnectionConfig::from_environment(env);
	EXPECT_EQ(cfg.log.host, cfg.main.host);
	EXPECT_EQ(cfg.log.database, cfg.main.database);
}

TEST(S1_DbConfig, S1_ProcessOverrideWinsOverDatabaseUrl) {
	MapEnvironment env;
	env.set("DATABASE_URL", kFakeMain);
	env.set("LOGIN_DB_URL", "mysql://login:fake-login-pass-2222@login-db.invalid/login_db");
	const auto cfg = DbConnectionConfig::from_environment(env, ServerProcess::Login);
	EXPECT_EQ(cfg.main.host, "login-db.invalid");
	EXPECT_EQ(cfg.main.port, 3306);
}

TEST(S1_DbConfig, S1_PasswordMayContainSeparators) {
	const auto ep = parse_db_url("mysql://u:p@ss:w0rd@h.invalid:1/db");
	EXPECT_EQ(ep.user, "u");
	EXPECT_EQ(ep.password, "p@ss:w0rd");
	EXPECT_EQ(ep.host, "h.invalid");
}

TEST(S1_DbConfig, S1_RedactedFormNeverContainsPassword) {
	const auto ep = parse_db_url(kFakeMain);
	EXPECT_EQ(ep.redacted(), "mysql://rathena:***@db.invalid:3306/rathena_main");
}

TEST(S1_DbConfig, S1_MalformedUrlErrorDoesNotLeakPassword) {
	for (const char* bad : {"postgres://rathena:fake-db-pass-0000@db.invalid/x",
	                        "mysql://rathena:fake-db-pass-0000@db.invalid:99999/x",
	                        "mysql://rathena:fake-db-pass-0000@db.invalid:3306",
	                        "mysql://rathena:fake-db-pass-0000@:3306/x"}) {
		try {
			(void)parse_db_url(bad);
			ADD_FAILURE() << "accepted malformed URL";
		} catch (const DbConfigError& e) {
			EXPECT_EQ(std::string(e.what()).find("fake-db-pass-0000"), std::string::npos);
		}
	}
}
