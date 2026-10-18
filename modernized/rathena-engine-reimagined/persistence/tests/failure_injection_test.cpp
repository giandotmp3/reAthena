// Failure-injection suite: faults at every layer must end in "rolled back, typed error, or
// recovered", never in partial state, duplicates or silent loss.
#include <atomic>
#include <random>
#include <thread>

#include "harness.hpp"

using namespace rathena::persist;
using namespace rathena::persist::testing_support;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

namespace {
WriteBehindOptions fast() {
	WriteBehindOptions o;
	o.retry_interval = 3ms;
	return o;
}
DirtyBatch mixed_batch(std::uint64_t rev) {
	DirtyBatch b;
	b.epoch = rev;
	for (std::uint64_t o = 1; o <= 3; ++o) {
		b.records.push_back(make_record(o, rev, "v" + std::to_string(rev)));
		b.kv.push_back(make_kv(o, "m", "k", static_cast<std::int64_t>(rev), rev));
	}
	return b;
}
} // namespace

TEST(PERSIST_FAULT_001, FailureHalfWayThroughABatchRollsEverythingBackThenTheWalReplaysItWhole) {
	Stack s;
	Wal wal({scratch_dir("fault001"), 1u << 20});
	DirtyTracker t;
	WriteBehindFlusher f(s.db, t, wal);
	s.db.fail_next(1, Fault::Outage, "INSERT INTO kv_entries");  // records already written inside the tx
	const auto res = f.persist(mixed_batch(1));
	EXPECT_TRUE(res.spilled_to_wal);
	EXPECT_EQ(scalar(s.raw_view(), "SELECT COUNT(*) FROM persist_records"), 0);  // no partial batch
	EXPECT_EQ(scalar(s.raw_view(), "SELECT COUNT(*) FROM kv_entries"), 0);
	EXPECT_TRUE(f.backend_down());
	EXPECT_EQ(f.drain_wal(), 1u);
	EXPECT_FALSE(f.backend_down());
	EXPECT_EQ(scalar(s.raw_view(), "SELECT COUNT(*) FROM persist_records"), 3);
	EXPECT_EQ(scalar(s.raw_view(), "SELECT COUNT(*) FROM kv_entries"), 3);
}

TEST(PERSIST_FAULT_002, FailureWhileReplayingKeepsTheRemainingSegmentsAndNeverAppliesOneTwice) {
	Stack s;
	Wal wal({scratch_dir("fault002"), 1u << 20});
	ensure_schema(s.db);
	s.db.execute({"CREATE TABLE audit (n INTEGER PRIMARY KEY AUTOINCREMENT, rev INTEGER)", {}});
	s.db.execute({"CREATE TRIGGER a_i AFTER INSERT ON persist_records BEGIN INSERT INTO audit (rev) VALUES (new.revision); END", {}});
	s.db.execute({"CREATE TRIGGER a_u AFTER UPDATE ON persist_records BEGIN INSERT INTO audit (rev) VALUES (new.revision); END", {}});
	DirtyTracker t;
	WriteBehindFlusher f(s.db, t, wal);
	for (std::uint64_t i = 1; i <= 5; ++i) wal.append(mixed_batch(i));
	// fail the progress marker of the 3rd batch: its transaction (records + marker) rolls back
	int markers = 0;
	f.set_hook([&](const char* p, std::uint64_t) {
		if (std::string(p) == "replay:after_apply" && ++markers == 2) s.db.fail_next(1, Fault::Outage, "persist_wal_progress");
	});
	EXPECT_EQ(f.drain_wal(), 2u);  // batches 1 and 2 were consumed; the fault hit batch 3's marker
	EXPECT_TRUE(f.backend_down());
	EXPECT_EQ(wal.segments(), 3u);
	EXPECT_EQ(scalar(s.raw_view(), "SELECT COUNT(*) FROM audit"), 6);  // batch 3 rolled back as a whole
	f.set_hook(nullptr);
	EXPECT_EQ(f.drain_wal(), 3u);
	EXPECT_TRUE(wal.empty());
	EXPECT_EQ(scalar(s.raw_view(), "SELECT COUNT(*) FROM audit"), 15);  // 5 batches x 3 owners, none repeated
	EXPECT_EQ(scalar(s.raw_view(), "SELECT revision FROM persist_records WHERE owner_id = 2"), 5);
}

TEST(PERSIST_FAULT_003, KvQuotaLedgerIsRestoredWhenTheWriteFails) {
	Stack s;
	Quarantine q(s.db);
	KvStore kv(s.db, q, KvPolicy{100, false});
	kv.set({character(1), "m", "a"}, Value{std::string(30, 'x')});
	const auto before = kv.bytes_used("m");
	s.db.fail_next(1, Fault::Outage, "INSERT INTO kv_entries");
	EXPECT_THROW(kv.set({character(1), "m", "b"}, Value{std::string(30, 'x')}), RepositoryError);
	EXPECT_EQ(kv.bytes_used("m"), before);  // not charged for a write that did not happen
	s.db.fail_next(1, Fault::Outage, "INSERT INTO kv_entries");
	EXPECT_THROW(kv.set({character(1), "m", "a"}, Value{std::string(60, 'x')}), RepositoryError);  // overwrite fails too
	EXPECT_EQ(kv.bytes_used("m"), before);
	EXPECT_NO_THROW(kv.set({character(1), "m", "b"}, Value{std::string(30, 'x')}));
	EXPECT_EQ(std::get<std::string>(kv.get({character(1), "m", "a"})->data), std::string(30, 'x'));
}

TEST(PERSIST_FAULT_004, FailedMigrationLeavesNoTraceAndTheNextRunAppliesItIdempotently) {
	Stack s;
	Migration m1, m2;
	m1.version = 1; m1.name = "a"; m1.checksum = "1";
	m1.up_sqlite = {{"CREATE TABLE a (id INTEGER)", {}}};
	m2.version = 2; m2.name = "b"; m2.checksum = "2";
	m2.up_sqlite = {{"CREATE TABLE b (id INTEGER)", {}}, {"INSERT INTO b (id) VALUES (1)", {}}};
	MigrationRunner runner(s.db, {m1, m2});
	s.db.fail_next(1, Fault::Outage, "INSERT INTO b");
	EXPECT_THROW(runner.apply_pending(), MigrationError);
	EXPECT_EQ(runner.current_version(), 1u);
	EXPECT_TRUE(s.raw_view().query({"SELECT name FROM sqlite_master WHERE name = 'b'", {}}).empty());
	EXPECT_EQ(runner.apply_pending(), (std::vector<std::uint32_t>{2}));
	EXPECT_TRUE(runner.apply_pending().empty());
	EXPECT_EQ(scalar(s.raw_view(), "SELECT COUNT(*) FROM b"), 1);
}

TEST(PERSIST_FAULT_005, FailedIdAssignmentConsumesNoNumber) {
	Stack s;
	IdMap map(s.db, {{"item", {10, 20}}});
	s.db.fail_next(1, Fault::Outage, "INSERT INTO persist_id_map");
	EXPECT_THROW(map.assign("item", "m:item/a"), RepositoryError);
	EXPECT_EQ(map.assign("item", "m:item/a"), 10u);
	EXPECT_EQ(map.assign("item", "m:item/b"), 11u);
}

TEST(PERSIST_FAULT_006, PoisonKvEntryIsQuarantinedOthersInTheSameBatchAreWritten) {
	Stack s;
	Wal wal({scratch_dir("fault006"), 1u << 20});
	DirtyTracker t;
	WriteBehindFlusher f(s.db, t, wal);
	s.db.poison_param("INSERT INTO kv_entries", "badkey");
	DirtyBatch b;
	b.kv.push_back(make_kv(1, "m", "good", 1));
	b.kv.push_back(make_kv(1, "m", "badkey", 2));
	b.kv.push_back(make_kv(1, "m", "good2", 3));
	const auto r = f.persist(b);
	EXPECT_EQ(r.kv_written, 2u);
	EXPECT_EQ(r.quarantined, 1u);
	EXPECT_FALSE(r.spilled_to_wal);
	Quarantine q(s.raw_view());
	EXPECT_TRUE(q.contains("kv_entries", "kv/m/badkey"));
}

TEST(PERSIST_FAULT_007, PoisonRowInAWalBatchDoesNotBlockReplayOfTheBatchesBehindIt) {
	Stack s;
	Wal wal({scratch_dir("fault007"), 1u << 20});
	DirtyTracker t;
	WriteBehindFlusher f(s.db, t, wal);
	DirtyBatch bad;
	bad.records.push_back(make_record(1, 1, "x", "mod:poison"));
	bad.records.push_back(make_record(2, 1, "fine"));
	wal.append(bad);
	wal.append(mixed_batch(2));
	s.db.poison_param("INSERT INTO persist_records", "mod:poison");
	EXPECT_EQ(f.drain_wal(), 2u);
	EXPECT_TRUE(wal.empty());
	EXPECT_EQ(record_payload(s.raw_view(), 2), "v2");
	Quarantine q(s.raw_view());
	EXPECT_EQ(q.list().size(), 1u);
}

TEST(PERSIST_FAULT_008, WalIoFailureWhileSpillingKeepsTheBatchQueuedAndItIsWrittenOnceTheDiskWorksAgain) {
	Stack s;
	const auto dir = scratch_dir("fault008");
	Wal wal({dir, 1u << 20});
	s.db.set_outage(true);
	WriteBehindService svc(s.db, wal, fast());
	fs::rename(dir, dir.string() + "_gone");  // the WAL disk disappears
	svc.mark(make_record(1, 1, "precious"));
	(void)svc.handoff();
	std::this_thread::sleep_for(50ms);
	EXPECT_EQ(svc.durable_epoch(), 0u);  // not durable anywhere: the writer is holding it
	EXPECT_EQ(svc.stats().queued, 1u);
	fs::rename(dir.string() + "_gone", dir);
	EXPECT_TRUE(svc.wait_durable(1, 5s));  // spilled to the WAL after the disk came back
	s.db.set_outage(false);
	EXPECT_TRUE(eventually([&] { return wal.empty(); }));
	EXPECT_EQ(record_payload(s.raw_view(), 1), "precious");
}

TEST(PERSIST_FAULT_009, FlappingBackendWithConcurrentWritesLosesNothingAndKeepsEveryOwnersOrder) {
	Stack s;
	Wal wal({scratch_dir("fault009"), 1u << 24});
	std::atomic<bool> stop{false};
	std::thread chaos([&] {
		std::mt19937 rng(7);
		while (!stop.load()) {
			s.db.set_outage(rng() % 2 == 0);
			if (rng() % 7 == 0) s.db.fail_next(1, rng() % 2 ? Fault::Outage : Fault::RowRejected, "INSERT INTO persist_records");
			std::this_thread::sleep_for(std::chrono::microseconds(200 + rng() % 1500));
		}
		s.db.set_outage(false);
	});
	std::int64_t expect_quarantined_max = 0;
	{
		WriteBehindService svc(s.db, wal, fast());
		for (std::uint64_t rev = 1; rev <= 400; ++rev) {
			for (std::uint64_t o = 1; o <= 6; ++o) {
				try {
					svc.mark(make_record(o, rev, "r" + std::to_string(rev)));
				} catch (const WalFullError&) { FAIL() << "WAL is large enough in this test"; }
			}
			try { (void)svc.handoff(); } catch (const WriteBacklogError&) { std::this_thread::sleep_for(1ms); }
			if (rev % 100 == 0) std::this_thread::sleep_for(5ms);
		}
		stop.store(true);
		chaos.join();
		s.db.clear_faults();
		const auto rep = svc.shutdown();
		EXPECT_EQ(rep.unpersisted_batches, 0u);
		EXPECT_TRUE(rep.wal_empty);
		expect_quarantined_max = scalar(s.raw_view(), "SELECT COUNT(*) FROM persist_quarantine");
	}
	// An injected RowRejected may have quarantined a row of one batch (the data is preserved there);
	// every other owner/revision is in the database. Final state per owner: the last revision, or
	// (if that exact write was the quarantined one) an earlier one, but never a missing owner.
	for (std::uint64_t o = 1; o <= 6; ++o) {
		const auto rev = scalar(s.raw_view(), ("SELECT revision FROM persist_records WHERE owner_id = " + std::to_string(o)).c_str());
		if (expect_quarantined_max == 0) EXPECT_EQ(rev, 400) << "owner " << o;
		else EXPECT_GE(rev, 1) << "owner " << o;
	}
	EXPECT_TRUE(wal.empty());
}

TEST(PERSIST_FAULT_010, ConnectionThatNeverComesUpFailsServiceStartupCleanlyAndLeavesTheWalAlone) {
	Stack s;
	const auto dir = scratch_dir("fault010");
	{
		Wal wal({dir, 1u << 20});
		wal.append(mixed_batch(1));
	}
	s.db.set_outage(true);
	Wal wal({dir, 1u << 20});
	{
		WriteBehindService svc(s.db, wal, fast());  // startup replay fails: it must not throw or drop the segment
		EXPECT_TRUE(svc.stats().backend_down);
		EXPECT_EQ(wal.segments(), 1u);
		s.db.set_outage(false);
		EXPECT_TRUE(eventually([&] { return wal.empty(); }));  // the idle writer notices the recovery
	}
	EXPECT_EQ(scalar(s.raw_view(), "SELECT COUNT(*) FROM persist_records"), 3);
}
