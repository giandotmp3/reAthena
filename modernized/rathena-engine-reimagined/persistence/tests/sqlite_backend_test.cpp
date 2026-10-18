// The lifted SQLite connection: file persistence, typed errors, parameter binding, wrappers.
#include <filesystem>
#include <thread>

#include "harness.hpp"
#include "rathena/persist/db_config.hpp"

using namespace rathena::persist;
using namespace rathena::persist::testing_support;

TEST(PERSIST_SQLITE_001, FileDatabaseKeepsRowsAcrossReopenAndRunsInWalMode) {
	const auto dir = scratch_dir("sqlite001");
	const std::string url = "sqlite:" + (dir / "t.db").string();
	{
		auto db = open_connection(url);
		db->execute({"CREATE TABLE t (id INTEGER PRIMARY KEY, v BLOB)", {}});
		db->execute({"INSERT INTO t (id, v) VALUES (?, ?)", {SqlValue{std::int64_t{1}}, SqlValue{Blob{std::string("a\0b", 3)}}}});
	}
	auto db = open_connection(url);
	const auto rows = db->query({"SELECT v FROM t WHERE id = 1", {}});
	ASSERT_EQ(rows.size(), 1u);
	EXPECT_EQ(std::get<Blob>(rows[0].at("v")).bytes, std::string("a\0b", 3));  // blobs are not text
	EXPECT_EQ(std::get<std::string>(db->query({"PRAGMA journal_mode", {}})[0].at("journal_mode")), "wal");
}

TEST(PERSIST_SQLITE_002, RowLevelRefusalsAreTypedAsRowRejectedOthersAsRepositoryError) {
	auto db = memory_db();
	db->execute({"CREATE TABLE t (id INTEGER PRIMARY KEY, v TEXT NOT NULL)", {}});
	db->execute({"INSERT INTO t (id, v) VALUES (1, 'x')", {}});
	EXPECT_THROW(db->execute({"INSERT INTO t (id, v) VALUES (1, 'dup')", {}}), RowRejectedError);   // UNIQUE
	EXPECT_THROW(db->execute({"INSERT INTO t (id, v) VALUES (2, NULL)", {}}), RowRejectedError);     // NOT NULL
	try {
		db->execute({"INSERT INTO missing_table (id) VALUES (1)", {}});
		FAIL();
	} catch (const RowRejectedError&) {
		FAIL() << "a missing table is an outage-class error, not a poison row";
	} catch (const RepositoryError&) {}
	EXPECT_THROW(db->execute({"INSERT INTO t (id, v) VALUES (?, ?)", {SqlValue{std::int64_t{9}}}}), RepositoryError);  // arity
	EXPECT_THROW(db->execute({"SELECT 1; SELECT 2", {}}), RepositoryError);  // one statement per call
}

TEST(PERSIST_SQLITE_003, TransactionHelperCommitsOnReturnAndRollsBackOnThrow) {
	auto db = memory_db();
	db->execute({"CREATE TABLE t (id INTEGER PRIMARY KEY)", {}});
	db->transaction([&] { db->execute({"INSERT INTO t (id) VALUES (1)", {}}); });
	EXPECT_THROW(db->transaction([&] {
		db->execute({"INSERT INTO t (id) VALUES (2)", {}});
		throw std::runtime_error("boom");
	}), std::runtime_error);
	EXPECT_EQ(db->query({"SELECT id FROM t", {}}).size(), 1u);
	db->execute_script("INSERT INTO t (id) VALUES (3); INSERT INTO t (id) VALUES (4);");
	EXPECT_THROW(db->execute_script("INSERT INTO t (id) VALUES (5); INSERT INTO t (id) VALUES (5);"), RepositoryError);
	EXPECT_EQ(db->query({"SELECT id FROM t", {}}).size(), 3u);  // the failed script left nothing behind
}

TEST(PERSIST_SQLITE_004, OpenConnectionRefusesBadUrlsWithoutEchoingSecrets) {
	EXPECT_THROW(open_connection("mysql://u:hunter2pass@h/db"), DbConfigError);
	EXPECT_THROW(open_connection("sqlite:"), DbConfigError);
	try {
		open_connection("postgresql://rathena:fake-db-pass-0000@db.invalid:5432/x");
		FAIL() << "no PostgreSQL support expected in this build, or the host is unreachable";
	} catch (const PersistError& e) {  // BackendUnavailableError (no libpq) or RepositoryError (unreachable)
		EXPECT_EQ(std::string(e.what()).find("fake-db-pass-0000"), std::string::npos);
	}
	EXPECT_THROW(open_connection("sqlite:/nonexistent_dir_for_test/x/y.db"), RepositoryError);
}

TEST(PERSIST_SQLITE_005, PreparedStatementCacheSurvivesSchemaChangesAndHostileInput) {
	auto db = memory_db();
	db->execute({"CREATE TABLE t (id INTEGER PRIMARY KEY, v TEXT)", {}});
	const Statement ins{"INSERT INTO t (id, v) VALUES (?, ?)", {SqlValue{std::int64_t{1}}, SqlValue{std::string("a")}}};
	db->execute(ins);
	db->execute({"ALTER TABLE t ADD COLUMN w TEXT", {}});  // invalidates cached plans
	db->execute({"INSERT INTO t (id, v) VALUES (?, ?)", {SqlValue{std::int64_t{2}}, SqlValue{std::string("x'); DROP TABLE t;--")}}});
	EXPECT_EQ(db->query({"SELECT id FROM t", {}}).size(), 2u);
	EXPECT_THROW(db->execute(ins), RowRejectedError);  // same cached statement, fresh error, no stale state
	db->execute({"DELETE FROM t WHERE id = 1", {}});
	db->execute(ins);
	EXPECT_EQ(db->affected_rows(), 1u);
}

TEST(PERSIST_SQLITE_006, GuardedConnectionSerializesThreadsSharingOneConnection) {
	SqliteConnection raw(":memory:");
	GuardedConnection db(raw);
	db.execute({"CREATE TABLE t (id INTEGER PRIMARY KEY, who INTEGER)", {}});
	std::vector<std::thread> ts;
	for (int w = 0; w < 4; ++w)
		ts.emplace_back([&db, w] {
			for (int i = 0; i < 200; ++i)
				db.transaction([&] {
					db.execute({"INSERT INTO t (id, who) VALUES (?, ?)", {SqlValue{std::int64_t{w * 1000 + i}}, SqlValue{std::int64_t{w}}}});
				});
		});
	for (auto& t : ts) t.join();
	EXPECT_EQ(db.query({"SELECT COUNT(*) AS n FROM t", {}})[0].at("n"), SqlValue{std::int64_t{800}});
}
