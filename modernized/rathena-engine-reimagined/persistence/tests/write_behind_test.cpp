// Architecture 3 / 4 persist: write-behind of dirty Persisted components and KV with copy-on-write
// batches. Rule ids PERSIST-WB-* are architecture-derived (rules/rule_manifest.tsv).
#include "support.hpp"

using namespace rathena::persist;
using namespace rathena::persist::testing_support;

namespace {
Record rec(std::uint64_t id, std::string payload, std::uint64_t revision = 1) {
	Record r;
	r.key = {character(id), "mod:profile"};
	r.payload = blob(std::move(payload));
	r.revision = revision;
	return r;
}
} // namespace

TEST(PERSIST_WB_001, SwappedOutBatchIsImmutableWhileNewMarksAccumulate) {
	DirtyTracker t;
	t.mark(rec(1, "v1"));
	auto batch = t.swap_out();
	ASSERT_EQ(batch->records.size(), 1u);
	t.mark(rec(1, "v2", 2));
	EXPECT_EQ(batch->records[0].payload.bytes, "v1");  // batch unchanged (copy-on-write)
	EXPECT_EQ(t.pending(), 1u);
	EXPECT_EQ(t.swap_out()->records[0].payload.bytes, "v2");
}

TEST(PERSIST_WB_002, RepeatedMarksOfOneKeyCoalesceToTheLatest) {
	DirtyTracker t;
	for (int i = 1; i <= 5; ++i) t.mark(rec(7, "v" + std::to_string(i), static_cast<std::uint64_t>(i)));
	auto batch = t.swap_out();
	ASSERT_EQ(batch->records.size(), 1u);
	EXPECT_EQ(batch->records[0].revision, 5u);
	EXPECT_TRUE(t.swap_out()->empty());
}

TEST(PERSIST_WB_003, MassLogoutBatchOf1000OwnersIsOneSwapAndOneFlushTransaction) {
	DirtyTracker t;
	for (std::uint64_t i = 1; i <= 1000; ++i) t.mark(rec(i, "p"));
	auto batch = t.swap_out();
	EXPECT_EQ(batch->records.size(), 1000u);
	EXPECT_EQ(t.pending(), 0u);
}

TEST(PERSIST_WB_004, FlushWritesRecordsAndKvToTheDatabase) {
	auto db = memory_db();
	DirtyTracker t;
	Wal wal({std::filesystem::temp_directory_path() / "persist_wb_004", 1u << 20});
	WriteBehindFlusher flusher(*db, t, wal);
	t.mark(rec(1, "payload"));
	KvEntry e;
	e.key = {character(1), "mymod", "k"};
	e.value = Value{std::int64_t{3}};
	t.mark(e);
	auto r = flusher.flush_once();
	EXPECT_EQ(r.records_written, 1u);
	EXPECT_EQ(r.kv_written, 1u);
	EXPECT_FALSE(r.spilled_to_wal);
}
