// Live-database tests for MysqlConnection + MysqlAccountRepository.
//
// They run only when RATHENA_TEST_DATABASE_URL names a DISPOSABLE database
// (mysql://user:pass@host:port/db) and the build links a MySQL client; otherwise they skip.
// Each test recreates the account tables from the legacy sql-files/main.sql DDL, then applies the
// real migrator statements (M6, and M2 where the test needs transactions).
#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>

#include "rathena/persistence/errors.hpp"
#include "rathena/persistence/migrate/migrator.hpp"
#include "rathena/persistence/mysql.hpp"

using namespace rathena::persistence;
namespace mg = rathena::persistence::migrate;

namespace {

constexpr const char* kAccountTables[] = {"login", "global_acc_reg_num", "global_acc_reg_str"};

std::optional<std::string> test_db_url() {
	const char* v = std::getenv("RATHENA_TEST_DATABASE_URL");
	if (v == nullptr || *v == '\0') return std::nullopt;
	return std::string(v);
}

/// The legacy CREATE TABLE statement for `table`, read from sql-files/main.sql.
std::string legacy_create_table(const std::string& table) {
	std::ifstream in(std::filesystem::path(RATHENA_LEGACY_SQL_DIR) / "main.sql");
	std::stringstream ss;
	ss << in.rdbuf();
	const std::string all = ss.str();
	const std::string head = "CREATE TABLE IF NOT EXISTS `" + table + "` (";
	const auto start = all.find(head);
	if (start == std::string::npos) throw std::runtime_error("no DDL for " + table + " in main.sql");
	const auto end = all.find(';', start);
	return all.substr(start, end - start);
}

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

class MysqlAccountIT : public ::testing::Test {
protected:
	void SetUp() override {
		if (!mysql_client_available()) GTEST_SKIP() << "built without a MySQL client";
		const auto url = test_db_url();
		if (!url) GTEST_SKIP() << "set RATHENA_TEST_DATABASE_URL to a disposable database to run";
		conn_ = std::make_unique<MysqlConnection>(parse_db_url(*url));
		recreate_legacy_tables();
		apply(mg::StepId::M6);
	}

	void recreate_legacy_tables() {
		for (const char* t : kAccountTables) conn_->execute({std::string("DROP TABLE IF EXISTS `") + t + "`", {}});
		for (const char* t : kAccountTables) conn_->execute({legacy_create_table(t), {}});
	}

	/// Runs a migrator step's real statements against the live tables.
	void apply(mg::StepId id) {
		mg::SchemaSnapshot schema;
		for (const char* t : kAccountTables) schema.tables.push_back({t, "MyISAM", {}});
		for (const auto& st : mg::Migrator::statements_for(id, mg::MigrationOptions{}, schema)) conn_->execute(st);
	}

	std::unique_ptr<MysqlConnection> conn_;
};

} // namespace

TEST_F(MysqlAccountIT, BR_LOGIN_008_FirstAccountIsTwoMillionThenMaxPlusOne) {
	MysqlAccountRepository repo(*conn_, TableNames{});
	Account a = sample("alpha01");
	Account b = sample("bravo02");
	ASSERT_TRUE(repo.create(a).ok());
	ASSERT_TRUE(repo.create(b).ok());
	EXPECT_EQ(a.id.value, 2'000'000u);
	EXPECT_EQ(b.id.value, 2'000'001u);
}

TEST_F(MysqlAccountIT, RoundTripPreservesEveryField) {
	MysqlAccountRepository repo(*conn_, TableNames{});
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
}

TEST_F(MysqlAccountIT, ValuesAreBoundNotSplicedIntoSql) {
	MysqlAccountRepository repo(*conn_, TableNames{});
	Account a = sample("o'r\"1=1;--");
	ASSERT_TRUE(repo.create(a).ok());
	const auto found = repo.find_by_userid("o'r\"1=1;--", true);
	ASSERT_TRUE(found.has_value());
	EXPECT_EQ(found->userid, "o'r\"1=1;--");
	EXPECT_FALSE(repo.find_by_userid("' OR '1'='1", false).has_value());
}

TEST_F(MysqlAccountIT, BR_LOGIN_038_CaseSensitivityAndAmbiguity) {
	MysqlAccountRepository repo(*conn_, TableNames{});
	Account lower = sample("hero");
	Account upper = sample("HERO");
	ASSERT_TRUE(repo.create(lower).ok());
	ASSERT_TRUE(repo.create(upper).ok());
	ASSERT_TRUE(repo.find_by_userid("HERO", true).has_value());
	EXPECT_EQ(repo.find_by_userid("HERO", true)->id, upper.id);
	EXPECT_FALSE(repo.find_by_userid("Hero", true).has_value());
	// Case-insensitive collation matches both rows -> ambiguous -> deny.
	EXPECT_FALSE(repo.find_by_userid("Hero", false).has_value());
}

TEST_F(MysqlAccountIT, GlobalRegistryUpsertAndRead) {
	MysqlAccountRepository repo(*conn_, TableNames{});
	Account a = sample("regs01");
	ASSERT_TRUE(repo.create(a).ok());
	repo.set_global_reg(a.id, GlobalRegNum{"##cash", 0, 100});
	repo.set_global_reg(a.id, GlobalRegNum{"##cash", 0, 250}); // REPLACE, not duplicate
	repo.set_global_reg(a.id, GlobalRegStr{"##title$", 1, "Champion"});
	ASSERT_EQ(repo.global_regs_num(a.id).size(), 1u);
	EXPECT_EQ(repo.global_regs_num(a.id)[0].value, 250);
	ASSERT_EQ(repo.global_regs_str(a.id).size(), 1u);
	EXPECT_EQ(repo.global_regs_str(a.id)[0], (GlobalRegStr{"##title$", 1, "Champion"}));
}

TEST_F(MysqlAccountIT, BR_LOGIN_037_RemoveDeletesAccountAndBothRegistries) {
	apply(mg::StepId::M2);
	MysqlAccountRepository repo(*conn_, TableNames{});
	Account a = sample("delete01");
	ASSERT_TRUE(repo.create(a).ok());
	repo.set_global_reg(a.id, GlobalRegNum{"##n", 0, 1});
	repo.set_global_reg(a.id, GlobalRegStr{"##s$", 0, "x"});
	EXPECT_TRUE(repo.remove(a.id));
	EXPECT_FALSE(repo.find(a.id).has_value());
	EXPECT_TRUE(repo.global_regs_num(a.id).empty());
	EXPECT_TRUE(repo.global_regs_str(a.id).empty());
	EXPECT_FALSE(repo.remove(a.id)); // unknown account
}

TEST_F(MysqlAccountIT, BR_LOGIN_037_MysqlRemoveRollsBackOnFailure) {
	apply(mg::StepId::M2); // InnoDB: the delete is atomic
	MysqlAccountRepository repo(*conn_, TableNames{});
	Account a = sample("rollback1");
	ASSERT_TRUE(repo.create(a).ok());
	repo.set_global_reg(a.id, GlobalRegNum{"##n", 0, 7});

	// Point the third DELETE at a missing table so it fails after the first two ran.
	TableNames broken;
	broken.global_acc_reg_str = "missing_table_for_rollback_test";
	MysqlAccountRepository failing(*conn_, broken);
	EXPECT_FALSE(failing.remove(a.id));

	EXPECT_TRUE(repo.find(a.id).has_value()) << "login row must survive the rolled-back delete";
	EXPECT_EQ(repo.global_regs_num(a.id).size(), 1u) << "## registry must survive the rolled-back delete";
}

TEST_F(MysqlAccountIT, BR_LOGIN_037_WithoutM2LegacyMyIsamDeleteIsNotAtomic) {
	// Documents why M2 (InnoDB) must run before BR-LOGIN-037 can hold: MyISAM ignores ROLLBACK.
	MysqlAccountRepository repo(*conn_, TableNames{});
	Account a = sample("myisam01");
	ASSERT_TRUE(repo.create(a).ok());
	repo.set_global_reg(a.id, GlobalRegNum{"##n", 0, 7});
	TableNames broken;
	broken.global_acc_reg_str = "missing_table_for_rollback_test"; // the 2nd of 3 deletes fails
	MysqlAccountRepository failing(*conn_, broken);
	EXPECT_FALSE(failing.remove(a.id));
	EXPECT_TRUE(repo.find(a.id).has_value());
	EXPECT_TRUE(repo.global_regs_num(a.id).empty()) << "MyISAM kept the partial delete of the ## registry";
}

TEST(MysqlConnectionIT, ConnectFailureNeverEchoesThePassword) {
	if (!mysql_client_available()) GTEST_SKIP() << "built without a MySQL client";
	DbEndpoint ep;
	ep.user = "rathena";
	ep.password = "fake-db-pass-0000";
	ep.host = "127.0.0.1";
	ep.port = 1; // nothing listens here
	ep.database = "rathena_main";
	MysqlConnection conn(ep);
	try {
		conn.connect();
		FAIL() << "connect to port 1 unexpectedly succeeded";
	} catch (const RepositoryError& e) {
		EXPECT_EQ(std::string(e.what()).find("fake-db-pass-0000"), std::string::npos) << "password leaked";
	}
}
