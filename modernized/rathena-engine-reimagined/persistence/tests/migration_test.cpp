// Schema migrations (architecture 4 persist; 5 Storage upgrades: "the loader refuses to start a
// mod whose stored version has no migration path").
#include "support.hpp"

using namespace rathena::persist;
using namespace rathena::persist::testing_support;

namespace {
Migration m(std::uint32_t v, const char* table) {
	Migration mig;
	mig.version = v;
	mig.name = std::string("create_") + table;
	mig.checksum = "sum-" + std::to_string(v);
	Statement s{std::string("CREATE TABLE ") + table + " (id INTEGER PRIMARY KEY)", {}};
	mig.up_sqlite = {s};
	mig.up_postgres = {s};
	return mig;
}
} // namespace

TEST(PERSIST_MIG_001, PendingMigrationsApplyInOrderOnceAndAreIdempotent) {
	auto db = memory_db();
	MigrationRunner runner(*db, {m(1, "t1"), m(2, "t2")});
	EXPECT_EQ(runner.current_version(), 0u);
	EXPECT_EQ(runner.apply_pending(), (std::vector<std::uint32_t>{1, 2}));
	EXPECT_EQ(runner.current_version(), 2u);
	EXPECT_TRUE(runner.apply_pending().empty());
}

TEST(PERSIST_MIG_002, FailingMigrationRollsBackAndLeavesVersionUnchanged) {
	auto db = memory_db();
	Migration bad = m(2, "t2");
	bad.up_sqlite = {{"CREATE TABLE t2 (id INTEGER)", {}}, {"THIS IS NOT SQL", {}}};
	bad.up_postgres = bad.up_sqlite;
	MigrationRunner runner(*db, {m(1, "t1"), bad});
	EXPECT_THROW(runner.apply_pending(), PersistError);
	EXPECT_EQ(runner.current_version(), 1u);
	EXPECT_TRUE(db->query({"SELECT name FROM sqlite_master WHERE name = 't2'", {}}).empty());
}

TEST(PERSIST_MIG_003, DatabaseNewerThanBuildAndChecksumDriftAreRefused) {
	auto db = memory_db();
	MigrationRunner(*db, {m(1, "t1"), m(2, "t2")}).apply_pending();
	EXPECT_THROW(MigrationRunner(*db, {m(1, "t1")}).apply_pending(), MigrationError);  // downgrade
	auto edited = m(1, "t1");
	edited.checksum = "tampered";
	EXPECT_THROW(MigrationRunner(*db, {edited, m(2, "t2")}).apply_pending(), MigrationError);
}

TEST(PERSIST_MIG_004, KvRowVersionWithoutMigrationPathIsRefusedAndWithPathIsUpgraded) {
	KvMigrationRegistry reg;
	reg.register_step("mymod", 1, [](const Value& v) { return Value{std::get<std::int64_t>(v.data) * 2}; });
	EXPECT_TRUE(reg.has_path("mymod", 1, 2));
	EXPECT_FALSE(reg.has_path("mymod", 1, 3));  // gap: no 2 -> 3 step
	EXPECT_FALSE(reg.has_path("othermod", 1, 2));
	EXPECT_EQ(std::get<std::int64_t>(reg.upgrade("mymod", 1, 2, Value{std::int64_t{21}}).data), 42);
}
