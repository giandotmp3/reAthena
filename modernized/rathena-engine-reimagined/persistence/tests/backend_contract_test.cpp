// Both backends (SQLite, PostgreSQL 16+) must pass the same contract. The PostgreSQL leg runs only
// when RATHENA_TEST_PG_URL names a DISPOSABLE database (never a production URL).
#include <cstdlib>

#include "support.hpp"

using namespace rathena::persist;
using namespace rathena::persist::testing_support;

namespace {
void contract(SqlConnection& db) {
	db.connect();
	db.execute({"CREATE TABLE contract_t (id INTEGER PRIMARY KEY, v TEXT)", {}});
	EXPECT_TRUE(db.execute_in_transaction({{"INSERT INTO contract_t (id, v) VALUES (?, ?)", {SqlValue{std::int64_t{1}}, SqlValue{std::string("a")}}}}));
	// a failing statement rolls the whole transaction back
	EXPECT_FALSE(db.execute_in_transaction({
	    {"INSERT INTO contract_t (id, v) VALUES (?, ?)", {SqlValue{std::int64_t{2}}, SqlValue{std::string("b")}}},
	    {"INSERT INTO contract_t (id, v) VALUES (?, ?)", {SqlValue{std::int64_t{1}}, SqlValue{std::string("dup")}}}}));
	EXPECT_EQ(db.query({"SELECT id FROM contract_t", {}}).size(), 1u);
	// parameters are bound, never spliced
	const std::string hostile = "x'); DROP TABLE contract_t;--";
	db.execute({"INSERT INTO contract_t (id, v) VALUES (?, ?)", {SqlValue{std::int64_t{3}}, SqlValue{hostile}}});
	EXPECT_EQ(db.query({"SELECT v FROM contract_t WHERE id = ?", {SqlValue{std::int64_t{3}}}}).size(), 1u);
}
} // namespace

TEST(PERSIST_BACKEND_001, SqliteBackendPassesTheConnectionContract) {
	PENDING_RULE("PERSIST-BACKEND-001");
	auto db = memory_db();
	contract(*db);
}

TEST(PERSIST_BACKEND_002, PostgresBackendPassesTheSameContractWhenADisposableUrlIsProvided) {
	PENDING_RULE("PERSIST-BACKEND-002");
	const char* url = std::getenv("RATHENA_TEST_PG_URL");
	if (url == nullptr || *url == '\0') GTEST_SKIP() << "RATHENA_TEST_PG_URL not set (disposable PostgreSQL required)";
	auto db = open_connection(url);
	contract(*db);
}
