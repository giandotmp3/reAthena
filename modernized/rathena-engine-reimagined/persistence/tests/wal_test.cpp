// Architecture 3.1: database down -> sim keeps running, saves queue to a bounded on-disk WAL; at
// the WAL limit new writes are REFUSED, not dropped silently; WAL replays after restart.
#include <filesystem>

#include "support.hpp"

using namespace rathena::persist;
using namespace rathena::persist::testing_support;

namespace {
std::filesystem::path scratch(const char* name) {
	auto p = std::filesystem::temp_directory_path() / "rathena_persist_tests" / name;
	std::filesystem::remove_all(p);
	std::filesystem::create_directories(p);
	return p;
}
DirtyBatch batch_of(std::size_t n, std::size_t payload_bytes) {
	DirtyBatch b;
	b.epoch = 1;
	for (std::size_t i = 0; i < n; ++i) {
		Record r;
		r.key = {character(i + 1), "mod:profile"};
		r.payload = blob(std::string(payload_bytes, 'x'));
		r.revision = 1;
		b.records.push_back(std::move(r));
	}
	return b;
}
} // namespace

TEST(PERSIST_WAL_001, AppendThenReplayDeliversBatchesInOrderAndEmptiesTheWal) {
	PENDING_RULE("PERSIST-WAL-001");
	Wal wal({scratch("wal001"), 1u << 20});
	wal.append(batch_of(2, 8));
	wal.append(batch_of(3, 8));
	std::vector<std::size_t> seen;
	EXPECT_EQ(wal.replay([&](const DirtyBatch& b) { seen.push_back(b.records.size()); }), 2u);
	EXPECT_EQ(seen, (std::vector<std::size_t>{2, 3}));
	EXPECT_EQ(wal.bytes_used(), 0u);
}

TEST(PERSIST_WAL_002, WalSurvivesProcessRestartAndReplaysIntoTheDatabase) {
	PENDING_RULE("PERSIST-WAL-002");
	const auto dir = scratch("wal002");
	{
		Wal wal({dir, 1u << 20});
		wal.append(batch_of(4, 16));
	} // "crash": the Wal object is gone, files remain
	Wal reopened({dir, 1u << 20});
	EXPECT_GT(reopened.bytes_used(), 0u);
	auto db = memory_db();
	DirtyTracker t;
	WriteBehindFlusher flusher(*db, t, reopened);
	EXPECT_EQ(flusher.drain_wal(), 1u);
	EXPECT_EQ(reopened.bytes_used(), 0u);
}

TEST(PERSIST_WAL_003, AtTheByteLimitAppendIsRefusedWithWalFullAndNothingIsWritten) {
	PENDING_RULE("PERSIST-WAL-003");
	Wal wal({scratch("wal003"), 4096});
	wal.append(batch_of(1, 1024));
	const auto before = wal.bytes_used();
	EXPECT_THROW(wal.append(batch_of(8, 1024)), WalFullError);
	EXPECT_EQ(wal.bytes_used(), before);  // the refused batch left no partial bytes
	EXPECT_FALSE(wal.accepting_writes() && before >= 4096);
}

TEST(PERSIST_WAL_004, DatabaseOutageSpillsTheBatchToTheWalInsteadOfLosingIt) {
	PENDING_RULE("PERSIST-WAL-004");
	// Needs a connection whose writes fail; the sqlite file connection is closed/removed mid-test
	// once the backend is lifted. Contract: flush_once() returns spilled_to_wal == true and the
	// tracker no longer holds the batch; WalFullError surfaces only when the WAL is full.
	auto db = memory_db();
	DirtyTracker t;
	Wal wal({scratch("wal004"), 1u << 20});
	WriteBehindFlusher flusher(*db, t, wal);
	Record r;
	r.key = {character(1), "mod:profile"};
	r.payload = blob("p");
	t.mark(r);
	db->execute({"DROP TABLE IF EXISTS persist_records", {}});  // simulate an unusable store
	auto res = flusher.flush_once();
	EXPECT_TRUE(res.spilled_to_wal);
	EXPECT_GT(wal.bytes_used(), 0u);
	EXPECT_EQ(t.pending(), 0u);
}

TEST(PERSIST_WAL_005, WalDirectoryComesFromConfigurationNotFromACompiledInPath) {
	PENDING_RULE("PERSIST-WAL-005");
	WalConfig cfg;
	EXPECT_TRUE(cfg.dir.empty());           // no baked-in default directory
	EXPECT_THROW(Wal{cfg}, PersistError);   // an unset directory is a configuration error
}
