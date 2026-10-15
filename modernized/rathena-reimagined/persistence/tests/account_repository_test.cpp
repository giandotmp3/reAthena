// Acceptance tests: BR-LOGIN-008 (account id allocation), BR-LOGIN-037 (atomic account delete),
// and the repository half of BR-LOGIN-038 (userid lookup; >1 match denies).
// All account data below is fake; no legacy credential is used.
#include <gtest/gtest.h>

#include "rathena/persistence/errors.hpp"
#include "rathena/persistence/in_memory.hpp"
#include "rathena/persistence/sql_repositories.hpp"

using namespace rathena::persistence;

namespace {

Account fake_account(std::string userid) {
	Account a;
	a.userid = std::move(userid);
	a.pass_hash = "$argon2id$v=19$m=65536,t=3,p=1$ZmFrZXNhbHQ$ZmFrZWhhc2g"; // fake PHC-shaped value
	a.pass_scheme = PassScheme::Argon2id;
	a.email = "player@example.invalid";
	return a;
}

} // namespace

// ---------------------------------------------------------------- BR-LOGIN-008

TEST(BR_LOGIN_008_AccountIdAllocation, BR_LOGIN_008_EmptyTableStartsAt2000000) {
	const auto a = allocate_account_id(std::nullopt, std::nullopt);
	ASSERT_TRUE(a.ok());
	EXPECT_EQ(a.id->value, 2'000'000u);
}

TEST(BR_LOGIN_008_AccountIdAllocation, BR_LOGIN_008_NextIsMaxPlusOne) {
	const auto a = allocate_account_id(std::nullopt, 2'000'005u);
	ASSERT_TRUE(a.ok());
	EXPECT_EQ(a.id->value, 2'000'006u);
}

TEST(BR_LOGIN_008_AccountIdAllocation, BR_LOGIN_008_RaisedToAtLeastStartAccountNum) {
	// Only server accounts (ids < MAX_SERVERS) exist: the next player id is still 2,000,000.
	const auto a = allocate_account_id(std::nullopt, 1u);
	ASSERT_TRUE(a.ok());
	EXPECT_EQ(a.id->value, 2'000'000u);
}

TEST(BR_LOGIN_008_AccountIdAllocation, BR_LOGIN_008_AboveEndAccountNumIsRefused) {
	const auto a = allocate_account_id(std::nullopt, 100'000'000u);
	EXPECT_FALSE(a.ok());
	EXPECT_EQ(a.error, AccountIdError::AboveMaximum);
	EXPECT_TRUE(allocate_account_id(std::nullopt, 99'999'999u).ok());
}

TEST(BR_LOGIN_008_AccountIdAllocation, BR_LOGIN_008_MaxUint32DoesNotWrapToZero) {
	const auto a = allocate_account_id(std::nullopt, 0xFFFF'FFFFu);
	EXPECT_FALSE(a.ok());
	EXPECT_EQ(a.error, AccountIdError::AboveMaximum);
}

TEST(BR_LOGIN_008_AccountIdAllocation, BR_LOGIN_008_RequestedZeroIsRefused) {
	const auto a = allocate_account_id(0u, std::nullopt);
	EXPECT_FALSE(a.ok());
	EXPECT_EQ(a.error, AccountIdError::ZeroProhibited);
}

TEST(BR_LOGIN_008_AccountIdAllocation, BR_LOGIN_008_RequestedAboveMaximumIsRefused) {
	EXPECT_EQ(allocate_account_id(100'000'001u, std::nullopt).error, AccountIdError::AboveMaximum);
}

TEST(BR_LOGIN_008_AccountIdAllocation, BR_LOGIN_008_RequestedIdIsUsedAsIs) {
	// Server accounts (BR-LOGIN-025) are created with explicit ids below START_ACCOUNT_NUM.
	const auto a = allocate_account_id(1u, 2'500'000u);
	ASSERT_TRUE(a.ok());
	EXPECT_EQ(a.id->value, 1u);
}

TEST(BR_LOGIN_008_AccountIdAllocation, BR_LOGIN_008_RepositoryCreateAllocatesSequentially) {
	InMemoryDatabase db;
	InMemoryAccountRepository repo(db);
	Account a = fake_account("alice01");
	Account b = fake_account("bob0001");
	ASSERT_TRUE(repo.create(a).ok());
	ASSERT_TRUE(repo.create(b).ok());
	EXPECT_EQ(a.id.value, 2'000'000u);
	EXPECT_EQ(b.id.value, 2'000'001u);
	EXPECT_EQ(repo.find(b.id)->userid, "bob0001");
}

TEST(BR_LOGIN_008_AccountIdAllocation, BR_LOGIN_008_RepositoryRefusesWhenRangeExhausted) {
	InMemoryDatabase db;
	InMemoryAccountRepository repo(db);
	Account last = fake_account("last0001");
	ASSERT_TRUE(repo.create(last, 100'000'000u).ok());
	Account next = fake_account("next0001");
	const auto r = repo.create(next);
	EXPECT_FALSE(r.ok());
	EXPECT_EQ(r.error, AccountIdError::AboveMaximum);
	EXPECT_EQ(db.accounts.size(), 1u);
}

TEST(BR_LOGIN_008_AccountIdAllocation, BR_LOGIN_008_MaxQueryIsParameterFree) {
	const auto st = sql::select_max_account_id(TableNames{}, SqlDialect::Postgres);
	EXPECT_EQ(st.sql, "SELECT MAX(\"account_id\") FROM \"login\"");
	EXPECT_TRUE(st.params.empty());
}

// ---------------------------------------------------------------- BR-LOGIN-037

class BR_LOGIN_037_AtomicAccountDelete : public ::testing::Test {
protected:
	void SetUp() override {
		Account a = fake_account("victim01");
		ASSERT_TRUE(repo.create(a).ok());
		id = a.id;
		repo.set_global_reg(id, GlobalRegNum{"##CASHPOINTS", 0, 500});
		repo.set_global_reg(id, GlobalRegStr{"##NOTE$", 0, "hello"});
		Account other = fake_account("other001");
		ASSERT_TRUE(repo.create(other).ok());
		other_id = other.id;
		repo.set_global_reg(other_id, GlobalRegNum{"##CASHPOINTS", 0, 7});
	}
	InMemoryDatabase db;
	InMemoryAccountRepository repo{db};
	AccountId id{};
	AccountId other_id{};
};

TEST_F(BR_LOGIN_037_AtomicAccountDelete, BR_LOGIN_037_RemovesAccountAndAllGlobalRegs) {
	ASSERT_TRUE(repo.remove(id));
	EXPECT_FALSE(repo.find(id).has_value());
	EXPECT_TRUE(repo.global_regs_num(id).empty());
	EXPECT_TRUE(repo.global_regs_str(id).empty());
	// Other accounts are untouched.
	EXPECT_TRUE(repo.find(other_id).has_value());
	EXPECT_EQ(repo.global_regs_num(other_id).size(), 1u);
}

TEST_F(BR_LOGIN_037_AtomicAccountDelete, BR_LOGIN_037_FailureOnLastStatementRollsBackEverything) {
	db.faults.fail_on("delete:global_acc_reg_str");
	EXPECT_FALSE(repo.remove(id));
	EXPECT_TRUE(repo.find(id).has_value());
	EXPECT_EQ(repo.global_regs_num(id).size(), 1u);
	EXPECT_EQ(repo.global_regs_str(id).size(), 1u);
}

TEST_F(BR_LOGIN_037_AtomicAccountDelete, BR_LOGIN_037_FailureOnMiddleStatementRollsBackEverything) {
	db.faults.fail_on("delete:global_acc_reg_num");
	EXPECT_FALSE(repo.remove(id));
	EXPECT_TRUE(repo.find(id).has_value());
	EXPECT_EQ(repo.global_regs_num(id).size(), 1u);
	EXPECT_EQ(repo.global_regs_str(id).size(), 1u);
}

TEST_F(BR_LOGIN_037_AtomicAccountDelete, BR_LOGIN_037_RetryAfterFailureSucceeds) {
	db.faults.fail_on("delete:login");
	EXPECT_FALSE(repo.remove(id));
	EXPECT_TRUE(repo.remove(id));
	EXPECT_FALSE(repo.find(id).has_value());
}

TEST_F(BR_LOGIN_037_AtomicAccountDelete, BR_LOGIN_037_UnknownAccountIsNotRemoved) {
	EXPECT_FALSE(repo.remove(AccountId{99'999'999}));
}

TEST(BR_LOGIN_037_AtomicAccountDeleteSql, BR_LOGIN_037_StatementsCoverRowAndBothRegTables) {
	const auto st = sql::remove_account(TableNames{}, AccountId{2'000'123}, SqlDialect::Sqlite);
	ASSERT_EQ(st.size(), 3u);
	// Children before the parent, so the delete also works with foreign keys.
	EXPECT_EQ(st[0].sql, "DELETE FROM \"global_acc_reg_num\" WHERE \"account_id\" = ?");
	EXPECT_EQ(st[1].sql, "DELETE FROM \"global_acc_reg_str\" WHERE \"account_id\" = ?");
	EXPECT_EQ(st[2].sql, "DELETE FROM \"login\" WHERE \"account_id\" = ?");
	EXPECT_EQ(sql::remove_account(TableNames{}, AccountId{2'000'123}, SqlDialect::Postgres), st);
	for (const auto& s : st) EXPECT_EQ(std::get<std::uint64_t>(s.params.at(0)), 2'000'123u);
}

// The live ROLLBACK proof is BR_LOGIN_037_RemoveRollsBackOnFailure in
// repository_contract_test.cpp, on SQLite and (with RATHENA_TEST_PG_URL) on PostgreSQL.

// ---------------------------------------------------------------- BR-LOGIN-038 (repository half)

TEST(BR_LOGIN_038_UseridLookup, BR_LOGIN_038_CaseSensitiveLookupIsExact) {
	InMemoryDatabase db;
	InMemoryAccountRepository repo(db);
	Account a = fake_account("Hero01");
	ASSERT_TRUE(repo.create(a).ok());
	EXPECT_TRUE(repo.find_by_userid("Hero01", true).has_value());
	EXPECT_FALSE(repo.find_by_userid("hero01", true).has_value());
	EXPECT_TRUE(repo.find_by_userid("hero01", false).has_value());
}

TEST(BR_LOGIN_038_UseridLookup, BR_LOGIN_038_AmbiguousMatchIsDenied) {
	InMemoryDatabase db;
	InMemoryAccountRepository repo(db);
	Account a = fake_account("Hero01");
	Account b = fake_account("hero01");
	ASSERT_TRUE(repo.create(a).ok());
	ASSERT_TRUE(repo.create(b).ok());
	EXPECT_FALSE(repo.find_by_userid("HERO01", false).has_value());
	EXPECT_TRUE(repo.find_by_userid("hero01", true).has_value());
}
