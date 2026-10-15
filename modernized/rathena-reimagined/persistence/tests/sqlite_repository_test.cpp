// SQLite backend (DATABASE_DECISION.md): the account-repository contract on the clean target
// schema, run on every platform with no database server. Uses :memory: databases and, for the
// two-connection test, a temporary file.
#include <gtest/gtest.h>

#include <filesystem>
#include <random>

#include "rathena/persistence/db_config.hpp"
#include "rathena/persistence/errors.hpp"
#include "rathena/persistence/mysql.hpp"
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

class SqliteAccounts : public ::testing::Test {
protected:
	void SetUp() override { ASSERT_TRUE(create_target_schema(conn)); }
	SqliteConnection conn{":memory:"};
	SqlAccountRepository repo{conn, TableNames{}};
};

std::filesystem::path temp_db_path() {
	std::random_device rd;
	return std::filesystem::temp_directory_path() / ("rathena_sqlite_test_" + std::to_string(rd()) + ".db");
}

} // namespace

// ---------------------------------------------------------------- repository contract

TEST_F(SqliteAccounts, BR_LOGIN_008_FirstAccountIsTwoMillionThenMaxPlusOne) {
	Account a = sample("alpha01");
	Account b = sample("bravo02");
	ASSERT_TRUE(repo.create(a).ok());
	ASSERT_TRUE(repo.create(b).ok());
	EXPECT_EQ(a.id.value, 2'000'000u);
	EXPECT_EQ(b.id.value, 2'000'001u);
}

TEST_F(SqliteAccounts, RoundTripPreservesEveryField) {
	Account a = sample("roundtrip");
	a.unban_time = 1'900'000'000;
	a.vip_time = 1'800'000'000;
	a.old_group = 1;
	ASSERT_TRUE(repo.create(a).ok());
	EXPECT_EQ(repo.find(a.id), a);
	a.state = 5;
	a.logincount = 4;
	repo.save(a);
	EXPECT_EQ(repo.find(a.id), a);
	EXPECT_FALSE(repo.find(AccountId{2'999'999}).has_value());
	Account ghost = sample("ghost");
	ghost.id = AccountId{2'999'999};
	EXPECT_THROW(repo.save(ghost), RepositoryError);
}

TEST_F(SqliteAccounts, ValuesAreBoundNotSplicedIntoSql) {
	Account a = sample("o'r\"1=1;--");
	ASSERT_TRUE(repo.create(a).ok());
	ASSERT_TRUE(repo.find_by_userid("o'r\"1=1;--", true).has_value());
	EXPECT_FALSE(repo.find_by_userid("' OR '1'='1", false).has_value());
}

TEST_F(SqliteAccounts, BR_LOGIN_038_CaseSensitivityAndAmbiguity) {
	Account lower = sample("hero");
	Account upper = sample("HERO");
	ASSERT_TRUE(repo.create(lower).ok());
	ASSERT_TRUE(repo.create(upper).ok());
	ASSERT_TRUE(repo.find_by_userid("HERO", true).has_value());
	EXPECT_EQ(repo.find_by_userid("HERO", true)->id, upper.id);
	EXPECT_FALSE(repo.find_by_userid("Hero", true).has_value());
	EXPECT_FALSE(repo.find_by_userid("Hero", false).has_value()) << "two case-insensitive matches -> deny";
	Account solo = sample("Solo");
	ASSERT_TRUE(repo.create(solo).ok());
	EXPECT_EQ(repo.find_by_userid("sOLO", false)->id, solo.id);
}

TEST_F(SqliteAccounts, GlobalRegistryUpsertAndRead) {
	Account a = sample("regs01");
	ASSERT_TRUE(repo.create(a).ok());
	repo.set_global_reg(a.id, GlobalRegNum{"##cash", 0, 100});
	repo.set_global_reg(a.id, GlobalRegNum{"##cash", 0, 250}); // ON CONFLICT update, not duplicate
	repo.set_global_reg(a.id, GlobalRegStr{"##title$", 1, "Champion"});
	ASSERT_EQ(repo.global_regs_num(a.id).size(), 1u);
	EXPECT_EQ(repo.global_regs_num(a.id)[0].value, 250);
	EXPECT_EQ(repo.global_regs_str(a.id), (std::vector<GlobalRegStr>{{"##title$", 1, "Champion"}}));
}

TEST_F(SqliteAccounts, BR_LOGIN_037_RemoveDeletesAccountAndBothRegistries) {
	Account a = sample("delete01");
	ASSERT_TRUE(repo.create(a).ok());
	repo.set_global_reg(a.id, GlobalRegNum{"##n", 0, 1});
	repo.set_global_reg(a.id, GlobalRegStr{"##s$", 0, "x"});
	EXPECT_TRUE(repo.remove(a.id));
	EXPECT_FALSE(repo.find(a.id).has_value());
	EXPECT_TRUE(repo.global_regs_num(a.id).empty());
	EXPECT_TRUE(repo.global_regs_str(a.id).empty());
	EXPECT_FALSE(repo.remove(a.id));
}

TEST_F(SqliteAccounts, BR_LOGIN_037_RemoveRollsBackOnFailure) {
	Account a = sample("rollback1");
	ASSERT_TRUE(repo.create(a).ok());
	repo.set_global_reg(a.id, GlobalRegNum{"##n", 0, 7});
	TableNames broken;
	broken.global_acc_reg_str = "missing_table_for_rollback_test"; // the 2nd of 3 deletes fails
	SqlAccountRepository failing(conn, broken);
	EXPECT_FALSE(failing.remove(a.id));
	EXPECT_TRUE(repo.find(a.id).has_value());
	EXPECT_EQ(repo.global_regs_num(a.id).size(), 1u) << "the first delete must have been rolled back";
}

// ---------------------------------------------------------------- target schema constraints

TEST_F(SqliteAccounts, SchemaIsIdempotentAndRecordsItsVersion) {
	EXPECT_TRUE(create_target_schema(conn));
	const auto rows = conn.query({"SELECT \"version\" FROM \"schema_migrations\"", {}});
	ASSERT_EQ(rows.size(), 1u);
	EXPECT_EQ(std::get<std::string>(rows[0].at("version")), kTargetSchemaVersion);
}

TEST_F(SqliteAccounts, RegistryRowsCannotOutliveTheirAccount) {
	EXPECT_THROW(repo.set_global_reg(AccountId{2'500'000}, GlobalRegNum{"##n", 0, 1}), RepositoryError) << "FK to login";
	Account a = sample("cascade1");
	ASSERT_TRUE(repo.create(a).ok());
	repo.set_global_reg(a.id, GlobalRegNum{"##n", 0, 1});
	conn.execute({"DELETE FROM \"login\" WHERE \"account_id\" = ?", {std::uint64_t{a.id.value}}});
	EXPECT_TRUE(repo.global_regs_num(a.id).empty()) << "ON DELETE CASCADE";
}

TEST_F(SqliteAccounts, ChecksRejectValuesTheLegacySchemaAccepted) {
	Account bad_sex = sample("badsex");
	ASSERT_TRUE(repo.create(bad_sex).ok());
	EXPECT_THROW(conn.execute({"UPDATE \"login\" SET \"sex\" = 'X' WHERE \"account_id\" = ?", {std::uint64_t{bad_sex.id.value}}}),
	             RepositoryError);
	Account too_long = sample("this_userid_is_longer_than_23");
	EXPECT_THROW((void)repo.create(too_long), RepositoryError);
	EXPECT_THROW(conn.execute({"UPDATE \"login\" SET \"logincount\" = 'many'", {}}), RepositoryError) << "STRICT table";
}

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
	EXPECT_EQ(open_connection("mysql://rathena:fake-db-pass-0000@db.invalid:3306/rathena_main")->dialect(), SqlDialect::MySql);
	EXPECT_THROW((void)open_connection("sqlite://relative.db"), DbConfigError);
	EXPECT_THROW((void)open_connection("postgres://u:p@db.invalid/x"), DbConfigError) << "until increment 2";
	EXPECT_THROW((void)open_connection("redis://x"), DbConfigError);
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

TEST(TargetSchema, NotGeneratedForTheMysqlTransitionBackend) {
	EXPECT_THROW((void)target_schema_statements(SqlDialect::MySql), std::invalid_argument);
	const auto pg = target_schema_statements(SqlDialect::Postgres);
	ASSERT_FALSE(pg.empty());
	for (const auto& st : pg) {
		EXPECT_EQ(st.sql.find('`'), std::string::npos) << "PostgreSQL DDL must not use backticks: " << st.sql;
		EXPECT_EQ(st.sql.find("STRICT"), std::string::npos) << "STRICT is SQLite-only: " << st.sql;
	}
}
