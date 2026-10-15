// The account-repository contract on the clean target schema, run on every backend:
// SQLite (:memory:, always) and PostgreSQL (when RATHENA_TEST_PG_URL is set; skipped otherwise).
// Test names carry the backend as the parameter suffix (/sqlite, /postgresql).
#include <gtest/gtest.h>

#include <memory>

#include "pg_test_support.hpp"
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

class AccountRepoContract : public ::testing::TestWithParam<SqlDialect> {
protected:
	void SetUp() override {
		if (GetParam() == SqlDialect::Postgres) {
			if (const auto why = pgtest::pg_unavailable()) GTEST_SKIP() << *why;
			pg_ = std::make_unique<pgtest::PgScratchSchema>();
			conn_ = &pg_->conn();
		} else {
			sqlite_ = std::make_unique<SqliteConnection>(":memory:");
			conn_ = sqlite_.get();
		}
		ASSERT_TRUE(create_target_schema(*conn_));
		repo_ = std::make_unique<SqlAccountRepository>(*conn_, TableNames{});
	}
	SqlConnection& conn() { return *conn_; }
	SqlAccountRepository& repo() { return *repo_; }

private:
	std::unique_ptr<SqliteConnection> sqlite_;
	std::unique_ptr<pgtest::PgScratchSchema> pg_;
	SqlConnection* conn_ = nullptr;
	std::unique_ptr<SqlAccountRepository> repo_;
};

} // namespace

INSTANTIATE_TEST_SUITE_P(Backends, AccountRepoContract, ::testing::Values(SqlDialect::Sqlite, SqlDialect::Postgres),
                         [](const ::testing::TestParamInfo<SqlDialect>& info) { return std::string(to_string(info.param)); });

// ---------------------------------------------------------------- repository contract

TEST_P(AccountRepoContract, BR_LOGIN_008_FirstAccountIsTwoMillionThenMaxPlusOne) {
	Account a = sample("alpha01");
	Account b = sample("bravo02");
	ASSERT_TRUE(repo().create(a).ok());
	ASSERT_TRUE(repo().create(b).ok());
	EXPECT_EQ(a.id.value, 2'000'000u);
	EXPECT_EQ(b.id.value, 2'000'001u);
}

TEST_P(AccountRepoContract, RoundTripPreservesEveryField) {
	Account a = sample("roundtrip");
	a.unban_time = 1'900'000'000;
	a.vip_time = 1'800'000'000;
	a.old_group = 1;
	ASSERT_TRUE(repo().create(a).ok());
	EXPECT_EQ(repo().find(a.id), a);
	a.state = 5;
	a.logincount = 4;
	repo().save(a);
	EXPECT_EQ(repo().find(a.id), a);
	EXPECT_FALSE(repo().find(AccountId{2'999'999}).has_value());
	Account ghost = sample("ghost");
	ghost.id = AccountId{2'999'999};
	EXPECT_THROW(repo().save(ghost), RepositoryError);
}

TEST_P(AccountRepoContract, ValuesAreBoundNotSplicedIntoSql) {
	Account a = sample("o'r\"1=1;--");
	ASSERT_TRUE(repo().create(a).ok());
	ASSERT_TRUE(repo().find_by_userid("o'r\"1=1;--", true).has_value());
	EXPECT_FALSE(repo().find_by_userid("' OR '1'='1", false).has_value());
}

TEST_P(AccountRepoContract, BR_LOGIN_038_CaseSensitivityAndAmbiguity) {
	Account lower = sample("hero");
	Account upper = sample("HERO");
	ASSERT_TRUE(repo().create(lower).ok());
	ASSERT_TRUE(repo().create(upper).ok());
	ASSERT_TRUE(repo().find_by_userid("HERO", true).has_value());
	EXPECT_EQ(repo().find_by_userid("HERO", true)->id, upper.id);
	EXPECT_FALSE(repo().find_by_userid("Hero", true).has_value());
	EXPECT_FALSE(repo().find_by_userid("Hero", false).has_value()) << "two case-insensitive matches -> deny";
	Account solo = sample("Solo");
	ASSERT_TRUE(repo().create(solo).ok());
	EXPECT_EQ(repo().find_by_userid("sOLO", false)->id, solo.id);
}

TEST_P(AccountRepoContract, GlobalRegistryUpsertAndRead) {
	Account a = sample("regs01");
	ASSERT_TRUE(repo().create(a).ok());
	repo().set_global_reg(a.id, GlobalRegNum{"##cash", 0, 100});
	repo().set_global_reg(a.id, GlobalRegNum{"##cash", 0, 250}); // ON CONFLICT update, not duplicate
	repo().set_global_reg(a.id, GlobalRegStr{"##title$", 1, "Champion"});
	ASSERT_EQ(repo().global_regs_num(a.id).size(), 1u);
	EXPECT_EQ(repo().global_regs_num(a.id)[0].value, 250);
	EXPECT_EQ(repo().global_regs_str(a.id), (std::vector<GlobalRegStr>{{"##title$", 1, "Champion"}}));
}

TEST_P(AccountRepoContract, BR_LOGIN_037_RemoveDeletesAccountAndBothRegistries) {
	Account a = sample("delete01");
	ASSERT_TRUE(repo().create(a).ok());
	repo().set_global_reg(a.id, GlobalRegNum{"##n", 0, 1});
	repo().set_global_reg(a.id, GlobalRegStr{"##s$", 0, "x"});
	EXPECT_TRUE(repo().remove(a.id));
	EXPECT_FALSE(repo().find(a.id).has_value());
	EXPECT_TRUE(repo().global_regs_num(a.id).empty());
	EXPECT_TRUE(repo().global_regs_str(a.id).empty());
	EXPECT_FALSE(repo().remove(a.id));
}

TEST_P(AccountRepoContract, BR_LOGIN_037_RemoveRollsBackOnFailure) {
	Account a = sample("rollback1");
	ASSERT_TRUE(repo().create(a).ok());
	repo().set_global_reg(a.id, GlobalRegNum{"##n", 0, 7});
	TableNames broken;
	broken.global_acc_reg_str = "missing_table_for_rollback_test"; // the 2nd of 3 deletes fails
	SqlAccountRepository failing(conn(), broken);
	EXPECT_FALSE(failing.remove(a.id));
	EXPECT_TRUE(repo().find(a.id).has_value());
	EXPECT_EQ(repo().global_regs_num(a.id).size(), 1u) << "the first delete must have been rolled back";
}

// ---------------------------------------------------------------- target schema constraints

TEST_P(AccountRepoContract, SchemaIsIdempotentAndRecordsItsVersion) {
	EXPECT_TRUE(create_target_schema(conn()));
	const auto rows = conn().query({"SELECT \"version\" FROM \"schema_migrations\"", {}});
	ASSERT_EQ(rows.size(), 1u);
	EXPECT_EQ(std::get<std::string>(rows[0].at("version")), kTargetSchemaVersion);
}

TEST_P(AccountRepoContract, RegistryRowsCannotOutliveTheirAccount) {
	EXPECT_THROW(repo().set_global_reg(AccountId{2'500'000}, GlobalRegNum{"##n", 0, 1}), RepositoryError) << "FK to login";
	Account a = sample("cascade1");
	ASSERT_TRUE(repo().create(a).ok());
	repo().set_global_reg(a.id, GlobalRegNum{"##n", 0, 1});
	conn().execute({"DELETE FROM \"login\" WHERE \"account_id\" = ?", {std::uint64_t{a.id.value}}});
	EXPECT_TRUE(repo().global_regs_num(a.id).empty()) << "ON DELETE CASCADE";
}

TEST_P(AccountRepoContract, ChecksRejectValuesTheLegacySchemaAccepted) {
	Account bad_sex = sample("badsex");
	ASSERT_TRUE(repo().create(bad_sex).ok());
	EXPECT_THROW(conn().execute({"UPDATE \"login\" SET \"sex\" = 'X' WHERE \"account_id\" = ?", {std::uint64_t{bad_sex.id.value}}}),
	             RepositoryError);
	Account too_long = sample("this_userid_is_longer_than_23");
	EXPECT_THROW((void)repo().create(too_long), RepositoryError);
	EXPECT_THROW(conn().execute({"UPDATE \"login\" SET \"logincount\" = 'many'", {}}), RepositoryError) << "typed column (STRICT on SQLite)";
}
