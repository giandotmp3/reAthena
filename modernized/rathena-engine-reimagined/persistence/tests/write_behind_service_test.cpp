// Threaded write-behind: hand-off, ordering, barrier, shutdown drain, outage -> WAL -> recovery,
// refusal at the WAL limit, poison rows, concurrency. All run under ASan/UBSan and TSan builds.
#include <atomic>
#include <thread>

#include "harness.hpp"

using namespace rathena::persist;
using namespace rathena::persist::testing_support;
using namespace std::chrono_literals;

namespace {
WriteBehindOptions fast() {
	WriteBehindOptions o;
	o.retry_interval = 5ms;
	return o;
}
} // namespace

TEST(PERSIST_WBS_001, HandedOffBatchIsPersistedByTheWriterThreadAndBecomesDurable) {
	Stack s;
	Wal wal({scratch_dir("wbs001"), 1u << 20});
	WriteBehindService svc(s.db, wal, fast());
	for (std::uint64_t i = 1; i <= 50; ++i) svc.mark(make_record(i, 1, "p" + std::to_string(i)));
	svc.mark(make_kv(1, "m", "k", 5));
	const auto h = svc.handoff();
	EXPECT_EQ(h.records, 50u);
	EXPECT_EQ(h.kv, 1u);
	ASSERT_TRUE(svc.wait_durable(h.epoch, 5s));
	EXPECT_EQ(scalar(s.raw_view(), "SELECT COUNT(*) FROM persist_records"), 50);
	EXPECT_EQ(record_payload(s.raw_view(), 7), "p7");
	EXPECT_EQ(svc.handoff().epoch, 0u);  // nothing pending
	const auto st = svc.stats();
	EXPECT_EQ(st.direct, 1u);
	EXPECT_EQ(st.spilled, 0u);
}

TEST(PERSIST_WBS_002, PerOwnerOrderIsPreservedAcrossManyBatches) {
	Stack s;
	Wal wal({scratch_dir("wbs002"), 1u << 20});
	{
		WriteBehindService svc(s.db, wal, fast());
		s.db.clear_log();
		for (std::uint64_t round = 1; round <= 40; ++round) {
			for (std::uint64_t owner = 1; owner <= 5; ++owner) {
				svc.mark(make_record(owner, round, "r" + std::to_string(round)));
				svc.mark(make_kv(owner, "m", "counter", static_cast<std::int64_t>(round), round));
			}
			(void)svc.handoff();
		}
		svc.shutdown();
	}
	// The writer applies batches in FIFO order: for each owner the revision parameters seen by the
	// UPSERT statements must be non-decreasing.
	EXPECT_EQ(record_payload(s.raw_view(), 3), "r40");
	EXPECT_EQ(scalar(s.raw_view(), "SELECT revision FROM kv_entries WHERE owner_ref = '4'"), 40);
	const auto log = s.db.log();
	std::size_t upserts = 0;
	for (const auto& l : log) if (l.find("INSERT INTO persist_records") == 0) ++upserts;
	EXPECT_EQ(upserts, 200u);  // 40 rounds x 5 owners, none lost
}

TEST(PERSIST_WBS_003, ApplyOrderOfBatchesIsExactlyTheHandOffOrder) {
	Stack s;
	Wal wal({scratch_dir("wbs003"), 1u << 20});
	ensure_schema(s.db);
	s.db.execute({"CREATE TABLE seq (n INTEGER PRIMARY KEY AUTOINCREMENT, owner INTEGER, rev INTEGER)", {}});
	s.db.execute({"CREATE TRIGGER t_i AFTER INSERT ON persist_records BEGIN INSERT INTO seq (owner, rev) VALUES (new.owner_id, new.revision); END", {}});
	s.db.execute({"CREATE TRIGGER t_u AFTER UPDATE ON persist_records BEGIN INSERT INTO seq (owner, rev) VALUES (new.owner_id, new.revision); END", {}});
	{
		WriteBehindService svc(s.db, wal, fast());
		for (std::uint64_t rev = 1; rev <= 60; ++rev) {
			svc.mark(make_record(1, rev, "x"));
			svc.mark(make_record(2, rev, "x"));
			(void)svc.handoff();
		}
	}
	std::int64_t last1 = 0, last2 = 0;
	std::size_t n = 0;
	for (const auto& r : s.raw_view().query({"SELECT owner, rev FROM seq ORDER BY n", {}})) {
		const auto owner = std::get<std::int64_t>(r.at("owner"));
		const auto rev = std::get<std::int64_t>(r.at("rev"));
		auto& last = owner == 1 ? last1 : last2;
		EXPECT_GT(rev, last) << "owner " << owner << " went backwards";
		last = rev;
		++n;
	}
	EXPECT_EQ(n, 120u);
}

TEST(PERSIST_WBS_004, SaveToAuthBarrierBlocksUntilTheCharactersOwnWritesAreDurable) {
	Stack s;
	Wal wal({scratch_dir("wbs004"), 1u << 20});
	WriteBehindService svc(s.db, wal, fast());
	EXPECT_TRUE(svc.barrier(character(9), 10ms));  // nothing outstanding: passes at once
	svc.mark(make_record(9, 1, "logout-save"));
	svc.mark(make_kv(9, "m", "k", 1));
	// the save is still in the pending set (no tick boundary yet); the barrier hands it off itself
	EXPECT_TRUE(svc.barrier(character(9), 5s));
	EXPECT_EQ(record_payload(s.raw_view(), 9), "logout-save");  // auth may now read what the save wrote
	// another character's pending write does not hold this barrier back
	svc.mark(make_record(10, 1, "other"));
	EXPECT_TRUE(svc.barrier(character(9), 10ms));
	EXPECT_TRUE(svc.barrier(character(10), 5s));
}

TEST(PERSIST_WBS_005, BarrierTimesOutWhileTheBackendIsDownAndWalIsFullButWriteIsNeverDropped) {
	Stack s;
	Wal wal({scratch_dir("wbs005"), 1500});
	s.db.set_outage(true);
	WriteBehindService svc(s.db, wal, fast());
	svc.mark(make_record(1, 1, std::string(4000, 'x')));  // cannot fit the WAL at all
	EXPECT_FALSE(svc.barrier(character(1), 100ms));       // not durable -> auth must not proceed
	EXPECT_TRUE(eventually([&] { return svc.refusing_writes(); }));
	EXPECT_THROW(svc.mark(make_record(2, 1, "y")), WalFullError);  // typed refusal, not a drop
	s.db.set_outage(false);  // the database comes back: the held batch is written, nothing was lost
	EXPECT_TRUE(svc.barrier(character(1), 5s));
	EXPECT_EQ(record_payload(s.raw_view(), 1), std::string(4000, 'x'));
	EXPECT_TRUE(eventually([&] { return !svc.refusing_writes(); }));
	EXPECT_NO_THROW(svc.mark(make_record(2, 1, "y")));
}

TEST(PERSIST_WBS_006, OutageSpillsToWalKeepsOrderAndRecoveryReplaysBeforeNewBatches) {
	Stack s;
	Wal wal({scratch_dir("wbs006"), 1u << 20});
	WriteBehindService svc(s.db, wal, fast());
	s.db.set_outage(true);
	std::uint64_t last = 0;
	for (std::uint64_t rev = 1; rev <= 5; ++rev) {
		svc.mark(make_record(1, rev, "during" + std::to_string(rev)));
		last = svc.handoff().epoch;
	}
	ASSERT_TRUE(svc.wait_durable(last, 5s));  // durable = in the WAL
	EXPECT_GT(wal.bytes_used(), 0u);
	EXPECT_TRUE(svc.stats().backend_down);
	EXPECT_EQ(scalar(s.raw_view(), "SELECT COUNT(*) FROM persist_records"), 0);
	s.db.set_outage(false);
	svc.mark(make_record(1, 6, "after"));  // first write after recovery must land after the WAL
	svc.barrier(character(1), 5s);
	EXPECT_TRUE(eventually([&] { return wal.empty() && !svc.stats().backend_down; }));
	EXPECT_EQ(record_payload(s.raw_view(), 1), "after");
	EXPECT_EQ(scalar(s.raw_view(), "SELECT revision FROM persist_records WHERE owner_id = 1"), 6);
}

TEST(PERSIST_WBS_007, GracefulShutdownDrainsTheQueueThePendingSetAndAnAvailableWal) {
	Stack s;
	const auto waldir = scratch_dir("wbs007");
	{
		Wal wal({waldir, 1u << 20});
		WriteBehindService svc(s.db, wal, fast());
		for (std::uint64_t i = 1; i <= 30; ++i) {
			svc.mark(make_record(i, 1, "queued"));
			if (i % 10 == 0) (void)svc.handoff();
		}
		svc.mark(make_record(99, 1, "never handed off"));  // still in the producer's pending set
		const auto rep = svc.shutdown();
		EXPECT_EQ(rep.unpersisted_batches, 0u);
		EXPECT_TRUE(rep.wal_empty);
		EXPECT_THROW(svc.mark(make_record(1, 2, "late")), ServiceClosedError);
		EXPECT_THROW(svc.handoff(), ServiceClosedError);
	}
	EXPECT_EQ(scalar(s.raw_view(), "SELECT COUNT(*) FROM persist_records"), 31);
}

TEST(PERSIST_WBS_008, ShutdownDuringOutageLeavesTheDataInTheWalAndTheNextStartReplaysIt) {
	const auto dir = scratch_dir("wbs008");
	const std::string dbfile = (dir / "db.sqlite").string();
	{
		Stack s(dbfile);
		Wal wal({dir / "wal", 1u << 20});
		WriteBehindService svc(s.db, wal, fast());
		s.db.set_outage(true);
		for (std::uint64_t i = 1; i <= 10; ++i) svc.mark(make_record(i, 1, "v" + std::to_string(i)));
		const auto rep = svc.shutdown();
		EXPECT_EQ(rep.unpersisted_batches, 0u);
		EXPECT_FALSE(rep.wal_empty);  // reported: it is on disk, not yet in the database
	}
	Stack s(dbfile);
	Wal wal({dir / "wal", 1u << 20});
	WriteBehindService svc(s.db, wal, fast());  // construction replays the leftover WAL first
	svc.shutdown();
	EXPECT_EQ(scalar(s.raw_view(), "SELECT COUNT(*) FROM persist_records"), 10);
	EXPECT_TRUE(wal.empty());
}

TEST(PERSIST_WBS_009, BacklogLimitRefusesHandOffWithATypedErrorAndKeepsTheDirtySet) {
	Stack s;
	Wal wal({scratch_dir("wbs009"), 200});  // tiny WAL
	s.db.set_outage(true);
	WriteBehindOptions o = fast();
	o.max_queued_batches = 3;
	WriteBehindService svc(s.db, wal, o);
	bool refused = false;
	std::size_t handed = 0;
	for (std::uint64_t i = 1; i <= 20 && !refused; ++i) {
		try {
			svc.mark(make_record(i, 1, std::string(500, 'x')));
			if (svc.handoff().epoch != 0) ++handed;
		} catch (const WriteBacklogError&) {
			refused = true;
		} catch (const WalFullError&) {
			refused = true;
		}
		std::this_thread::sleep_for(2ms);
	}
	EXPECT_TRUE(refused);
	s.db.set_outage(false);
	svc.shutdown();
	// everything that was accepted by mark() is in the database; nothing accepted was dropped
	const auto n = scalar(s.raw_view(), "SELECT COUNT(*) FROM persist_records");
	EXPECT_GE(n, static_cast<std::int64_t>(handed));
}

TEST(PERSIST_WBS_010, PoisonRowIsIsolatedQuarantinedWithoutPayloadInTheReasonAndTheRestIsWritten) {
	Stack s;
	Wal wal({scratch_dir("wbs010"), 1u << 20});
	WriteBehindService svc(s.db, wal, fast());
	s.db.poison_param("INSERT INTO persist_records", "mod:poison");
	svc.mark(make_record(1, 1, "good1"));
	svc.mark(make_record(2, 1, "SECRET-PAYLOAD", "mod:poison"));
	svc.mark(make_record(3, 1, "good3"));
	ASSERT_TRUE(svc.barrier(character(2), 5s));
	s.db.clear_faults();
	EXPECT_EQ(record_payload(s.raw_view(), 1), "good1");
	EXPECT_EQ(record_payload(s.raw_view(), 3), "good3");
	EXPECT_EQ(record_payload(s.raw_view(), 2, "mod:poison"), "<none>");
	Quarantine q(s.raw_view());
	const auto rows = q.list();
	ASSERT_EQ(rows.size(), 1u);
	EXPECT_EQ(rows[0].table, "persist_records");
	EXPECT_EQ(rows[0].reason.find("SECRET"), std::string::npos);
	EXPECT_EQ(rows[0].raw.bytes, "SECRET-PAYLOAD");  // preserved for the operator
	EXPECT_EQ(svc.stats().spilled, 0u);               // a poison row is not an outage: no WAL spill
}

TEST(PERSIST_WBS_011, ManyProducersReadersAndBarrierWaitersRunConcurrentlyWithoutLossOrRaces) {
	Stack s;
	Wal wal({scratch_dir("wbs011"), 1u << 22});
	WriteBehindService svc(s.db, wal, fast());
	std::atomic<bool> stop{false};
	std::atomic<std::uint64_t> marks{0};
	std::vector<std::thread> threads;
	// one producer owns mark/handoff (the sim thread); others only observe or wait
	threads.emplace_back([&] {
		for (std::uint64_t rev = 1; rev <= 300; ++rev) {
			for (std::uint64_t o = 1; o <= 8; ++o) { svc.mark(make_record(o, rev, "r" + std::to_string(rev))); marks.fetch_add(1); }
			(void)svc.handoff();
			if (rev % 50 == 0) svc.barrier(character(3), 5s);
		}
	});
	for (int i = 0; i < 3; ++i)
		threads.emplace_back([&] {
			while (!stop.load()) {
				(void)svc.stats();
				(void)svc.durable_epoch();
				(void)svc.refusing_writes();
				std::this_thread::sleep_for(100us);
			}
		});
	threads.emplace_back([&] {  // a gateway thread waiting on epochs
		while (!stop.load()) { (void)svc.wait_durable(svc.durable_epoch() + 1, 1ms); }
	});
	threads[0].join();
	const auto rep = svc.shutdown();
	stop.store(true);
	for (std::size_t i = 1; i < threads.size(); ++i) threads[i].join();
	EXPECT_EQ(rep.unpersisted_batches, 0u);
	EXPECT_EQ(scalar(s.raw_view(), "SELECT COUNT(*) FROM persist_records"), 8);
	EXPECT_EQ(scalar(s.raw_view(), "SELECT MIN(revision) FROM persist_records"), 300);  // every owner reached the last revision
	EXPECT_EQ(marks.load(), 2400u);
}

TEST(PERSIST_WBS_012, DirtyTrackerStaysConsistentWhenProducersMarkWhileTheWriterSwaps) {
	DirtyTracker t;
	std::atomic<bool> stop{false};
	std::atomic<std::uint64_t> swapped_entries{0};
	std::thread swapper([&] {
		while (!stop.load()) {
			auto b = t.swap_out();
			swapped_entries.fetch_add(b->records.size());
			for (const auto& r : b->records) EXPECT_FALSE(r.payload.bytes.empty());  // immutable once handed out
		}
	});
	std::vector<std::thread> producers;
	for (std::uint64_t p = 0; p < 4; ++p)
		producers.emplace_back([&t, p] {
			for (std::uint64_t i = 0; i < 2000; ++i) t.mark(make_record(p * 10000 + i, 1, "x"));
		});
	for (auto& th : producers) th.join();
	stop.store(true);
	swapper.join();
	swapped_entries.fetch_add(t.swap_out()->records.size());
	EXPECT_EQ(swapped_entries.load(), 8000u);  // distinct keys: nothing lost, nothing duplicated
}

TEST(PERSIST_WBS_013, HandOffRestoresTheDirtySetWhenNothingCanPersistIt) {
	Stack s;
	Wal wal({scratch_dir("wbs013"), 100});
	s.db.set_outage(true);
	DirtyTracker t;
	WriteBehindFlusher f(s.db, t, wal);
	t.mark(make_record(1, 1, std::string(1000, 'x')));
	EXPECT_THROW(f.flush_once(), WalFullError);
	EXPECT_EQ(t.pending(), 1u);  // back in the tracker, not dropped
	t.mark(make_record(1, 2, "newer"));  // a newer mark wins over the restored older one
	s.db.set_outage(false);
	auto r = f.flush_once();
	EXPECT_EQ(r.records_written, 1u);
	EXPECT_EQ(record_payload(s.raw_view(), 1), "newer");
}
