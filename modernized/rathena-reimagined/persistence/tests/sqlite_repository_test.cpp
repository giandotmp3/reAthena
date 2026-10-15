// SQLite backend specifics (DATABASE_DECISION.md), run on every platform with no database server:
// connection settings, file databases shared by two connections, URL selection and the target
// schema DDL. The repository contract itself is in repository_contract_test.cpp.
#include <gtest/gtest.h>

#include <filesystem>
#include <random>

#include "rathena/persistence/db_config.hpp"
#include "rathena/persistence/errors.hpp"
#include "rathena/persistence/postgres.hpp"
#include "rathena/persistence/sql_repositories.hpp"
#include "rathena/persistence/sqlite.hpp"
#include "rathena/persistence/target_schema.hpp"

using namespace rathena::persistence;

namespace {

Account sample(std::string userid) {
	Account a;
	a.userid = std::move(userid);
	a.pass_hash = "$argon2id$v=19$m=65536,t=2,p=1$ZmFrZXNhbHQ$ZmFrZWhhc2g"; // fake, same shape
	a.pass_scheme = PassScheme::Argon2id;
	a.sex = Sex::Female;
	a.email = "player@example.invalid";
	a.group_id = 5;
	a.logincount = 3;
	a.character_slots = 9;
	return a;
}

std::filesystem::path temp_db_path() {
	std::random_device rd;
	return std::filesystem::temp_directory_path() / ("rathena_sqlite_test_" + std::to_string(rd()) + ".db");
}

} // namespace

TEST(SqliteConnection, DoubleQuotedStringLiteralsAreDisabled) {
	SqliteConnection conn(":memory:");
	conn.execute({"CREATE TABLE \"t\" (\"a\" TEXT) STRICT", {}});
	// With SQLITE_DQS=0 "oops" is an identifier, so a typo cannot silently become a string value.
	EXPECT_THROW(conn.execute({"INSERT INTO \"t\" (\"a\") VALUES (\"oops\")", {}}), RepositoryError);
}

TEST(SqliteConnection, RejectsMoreThanOneStatementPerCall) {
	SqliteConnection conn(":memory:");
	EXPECT_THROW(conn.execute({"CREATE TABLE \"t\" (\"a\" INTEGER); DROP TABLE \"t\"", {}}), RepositoryError);
}

TEST(SqliteConnection, TwoConnectionsShareAFileDatabaseInWalMode) {
	const auto path = temp_db_path();
	{
		SqliteConnection writer(path.string());
		SqliteConnection reader(path.string());
		ASSERT_TRUE(create_target_schema(writer));
		SqlAccountRepository w(writer, TableNames{});
		SqlAccountRepository r(reader, TableNames{});
		Account a = sample("shared01");
		ASSERT_TRUE(w.create(a).ok());
		ASSERT_TRUE(r.find(a.id).has_value()) << "committed write is visible to another connection";
		const auto mode = reader.query({"PRAGMA journal_mode", {}});
		ASSERT_EQ(mode.size(), 1u);
		EXPECT_EQ(std::get<std::string>(mode[0].begin()->second), "wal");
	}
	std::error_code ec;
	for (const char* suffix : {"", "-wal", "-shm"}) std::filesystem::remove(path.string() + suffix, ec);
}

// ---------------------------------------------------------------- configuration

TEST(OpenConnection, PicksTheBackendFromTheUrl) {
	EXPECT_EQ(open_connection("sqlite::memory:")->dialect(), SqlDialect::Sqlite);
	EXPECT_EQ(open_connection("sqlite:dev/rathena.db")->dialect(), SqlDialect::Sqlite);
	EXPECT_EQ(open_connection("sqlite:///var/lib/rathena/rathena.db")->dialect(), SqlDialect::Sqlite);
	EXPECT_THROW((void)open_connection("sqlite://relative.db"), DbConfigError);
	EXPECT_THROW((void)open_connection("redis://x"), DbConfigError);
	// Opening does not connect, so a fake endpoint is enough to pick the backend.
	for (const char* url : {"postgres://rathena:fake-db-pass-0000@db.invalid/x",
	                        "postgresql://rathena:fake-db-pass-0000@db.invalid:5432/x"}) {
		if (postgres_client_available()) {
			EXPECT_EQ(open_connection(url)->dialect(), SqlDialect::Postgres);
		} else {
			EXPECT_THROW((void)open_connection(url), DbConfigError) << "built without libpq";
		}
	}
}

TEST(OpenConnection, MysqlUrlsPointToTheImporter) {
	try {
		(void)open_connection("mysql://rathena:fake-db-pass-0000@db.invalid:3306/rathena_main");
		ADD_FAILURE() << "mysql:// must not open a connection";
	} catch (const DbConfigError& e) {
		const std::string what = e.what();
		EXPECT_NE(what.find("no longer supported"), std::string::npos) << what;
		EXPECT_NE(what.find("rathena-migrate import"), std::string::npos) << what;
		EXPECT_EQ(what.find("fake-db-pass-0000"), std::string::npos) << "password leaked";
	}
}

TEST(OpenConnection, SqliteUrlFromEnvironmentNeedsNoServer) {
	MapEnvironment env;
	env.set("DATABASE_URL", "sqlite::memory:");
	const auto cfg = DbConnectionConfig::from_environment(env, ServerProcess::Login);
	EXPECT_EQ(cfg.main_url, "sqlite::memory:");
	EXPECT_EQ(cfg.log_url, "sqlite::memory:");
	auto conn = open_connection(cfg.main_url);
	ASSERT_TRUE(create_target_schema(*conn));
	SqlAccountRepository repo(*conn, TableNames{});
	Account a = sample("fromenv");
	EXPECT_TRUE(repo.create(a).ok());
}

TEST(TargetSchema, PostgresDdlHasNoSqliteOrMysqlSyntax) {
	const auto pg = target_schema_statements(SqlDialect::Postgres);
	ASSERT_FALSE(pg.empty());
	for (const auto& st : pg) {
		EXPECT_EQ(st.sql.find('`'), std::string::npos) << "PostgreSQL DDL must not use backticks: " << st.sql;
		EXPECT_EQ(st.sql.find("STRICT"), std::string::npos) << "STRICT is SQLite-only: " << st.sql;
	}
}
