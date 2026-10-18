// WAL durability and crash recovery: CRC, torn writes, limits, fsync policy, exactly-once replay
// after a process is killed in the middle of replay.
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdlib>
#include <fstream>

#include "harness.hpp"
#include "rathena/persist/codec.hpp"

using namespace rathena::persist;
using namespace rathena::persist::testing_support;
namespace fs = std::filesystem;

namespace {
DirtyBatch batch(std::uint64_t rev) {
	DirtyBatch b;
	b.epoch = rev;
	b.records.push_back(make_record(1, rev, "v" + std::to_string(rev)));
	b.kv.push_back(make_kv(1, "m", "n", static_cast<std::int64_t>(rev), rev));
	return b;
}

// Audit trigger: every write that reaches persist_records leaves a row, so a duplicate
// application of a batch is visible even though the upsert itself is idempotent.
void install_audit(SqlConnection& db) {
	ensure_schema(db);
	db.execute({"CREATE TABLE IF NOT EXISTS audit (n INTEGER PRIMARY KEY AUTOINCREMENT, rev INTEGER)", {}});
	db.execute({"CREATE TRIGGER IF NOT EXISTS audit_i AFTER INSERT ON persist_records BEGIN INSERT INTO audit (rev) VALUES (new.revision); END", {}});
	db.execute({"CREATE TRIGGER IF NOT EXISTS audit_u AFTER UPDATE ON persist_records BEGIN INSERT INTO audit (rev) VALUES (new.revision); END", {}});
}

std::vector<std::int64_t> audit(SqlConnection& db) {
	std::vector<std::int64_t> out;
	for (const auto& r : db.query({"SELECT rev FROM audit ORDER BY n", {}})) out.push_back(std::get<std::int64_t>(r.at("rev")));
	return out;
}

// Runs `fn` in a forked child that is expected to die; returns the wait status.
template <class Fn>
int run_in_child(Fn fn) {
	std::fflush(nullptr);
	const pid_t pid = ::fork();
	if (pid == 0) {
		fn();
		_exit(0);
	}
	int status = 0;
	::waitpid(pid, &status, 0);
	return status;
}
} // namespace

TEST(PERSIST_WAL_006, SegmentsCarryACrcAndACorruptOneIsSetAsideReportedAndSkipped) {
	const auto dir = scratch_dir("wal006");
	{
		Wal wal({dir, 1u << 20});
		for (std::uint64_t i = 1; i <= 3; ++i) wal.append(batch(i));
	}
	// flip one byte in the middle segment
	fs::path victim;
	for (const auto& e : fs::directory_iterator(dir))
		if (e.path().filename().string() == "00000000000000000002.wal") victim = e.path();
	ASSERT_FALSE(victim.empty());
	{
		std::fstream f(victim, std::ios::in | std::ios::out | std::ios::binary);
		f.seekp(40);
		f.put('\x5a');
	}
	Wal wal({dir, 1u << 20});
	std::vector<std::uint64_t> seen;
	wal.replay([&](const DirtyBatch& b) { seen.push_back(b.epoch); });
	EXPECT_EQ(seen, (std::vector<std::uint64_t>{1, 3}));
	ASSERT_EQ(wal.corrupt_segments().size(), 1u);  // never silent
	EXPECT_TRUE(fs::exists(victim.string() + ".corrupt"));  // kept for the operator
	EXPECT_EQ(wal.bytes_used(), 0u);
}

TEST(PERSIST_WAL_007, TornTempFilesFromACrashedAppendAreInvisibleAndRemoved) {
	const auto dir = scratch_dir("wal007");
	{
		Wal wal({dir, 1u << 20});
		wal.append(batch(1));
	}
	{ std::ofstream(dir / "00000000000000000002.wal.tmp") << "half a segment"; }
	Wal wal({dir, 1u << 20});
	EXPECT_EQ(wal.segments(), 1u);
	EXPECT_FALSE(fs::exists(dir / "00000000000000000002.wal.tmp"));
	EXPECT_EQ(wal.replay([](const DirtyBatch&) {}), 1u);
}

TEST(PERSIST_WAL_008, TheByteLimitIncludesFramingHoldsAcrossRestartAndIsFreedByReplay) {
	const auto dir = scratch_dir("wal008");
	std::uint64_t one = 0;
	{
		Wal probe({dir, 1u << 20});
		probe.append(batch(1));
		one = probe.bytes_used();
		probe.replay([](const DirtyBatch&) {});
	}
	{
		Wal wal({dir, one * 2 + one / 2});  // room for two
		wal.append(batch(1));
		wal.append(batch(2));
		EXPECT_THROW(wal.append(batch(3)), WalFullError);
	}
	Wal wal({dir, one * 2 + one / 2});
	EXPECT_EQ(wal.segments(), 2u);
	EXPECT_THROW(wal.append(batch(3)), WalFullError);  // the limit survives a restart
	wal.replay([](const DirtyBatch&) {});
	EXPECT_NO_THROW(wal.append(batch(3)));
}

TEST(PERSIST_WAL_009, EveryFsyncPolicyWorksAndSequencesContinueAfterReopen) {
	for (auto policy : {WalFsync::Never, WalFsync::File, WalFsync::FileAndDir}) {
		const auto dir = scratch_dir("wal009_" + std::to_string(static_cast<int>(policy)));
		WalConfig cfg{dir, 1u << 20, policy};
		std::uint64_t s1 = 0, s2 = 0;
		{
			Wal wal(cfg);
			s1 = wal.append(batch(1));
		}
		Wal wal(cfg);
		s2 = wal.append(batch(2));
		EXPECT_EQ(s2, s1 + 1);
		std::vector<std::uint64_t> seen;
		wal.replay([&](const DirtyBatch& b) { seen.push_back(b.epoch); });
		EXPECT_EQ(seen, (std::vector<std::uint64_t>{1, 2}));
	}
}

TEST(PERSIST_WAL_010, OneInstancePerDirectoryAndTheDirectoryComesFromTheEnvironment) {
	const auto dir = scratch_dir("wal010");
	Wal first({dir, 1u << 20});
	EXPECT_THROW(Wal({dir, 1u << 20}), WalIoError);
	::unsetenv("RATHENA_TEST_WAL_DIR");
	EXPECT_THROW(load_wal_config_from_env("RATHENA_TEST_WAL_DIR"), DbConfigError);
	::setenv("RATHENA_TEST_WAL_DIR", dir.c_str(), 1);
	EXPECT_EQ(load_wal_config_from_env("RATHENA_TEST_WAL_DIR").dir, dir);
	::unsetenv("RATHENA_TEST_WAL_DIR");
}

TEST(PERSIST_WAL_011, AnAppendThatCannotBeWrittenReportsATypedIoErrorAndLeavesNoPartialFile) {
	const auto dir = scratch_dir("wal011");
	Wal wal({dir, 1u << 20});
	wal.append(batch(1));
	const auto before = wal.bytes_used();
	fs::remove_all(dir / "nothing");
	// make the directory unusable underneath the open WAL
	fs::rename(dir, dir.string() + "_moved");
	EXPECT_THROW(wal.append(batch(2)), WalIoError);
	EXPECT_EQ(wal.bytes_used(), before);
	fs::rename(dir.string() + "_moved", dir);
	EXPECT_NO_THROW(wal.append(batch(2)));  // recovers once the directory is back
}

TEST(PERSIST_WAL_012, BatchEncodingRoundTripsEveryFieldIncludingErasuresAndPreEncodedValues) {
	DirtyBatch b = batch(9);
	KvEntry gone;
	gone.key = {account(4), "m", "old", "acct-text"};
	gone.erased = true;
	gone.revision = 7;
	KvEntry raw;
	raw.key = {OwnerRef{OwnerScope::World, 0}, "m", "w", "world"};
	raw.encoded = Blob{std::string("S\x01\x02", 3)};
	raw.cost = 77;
	raw.row_version = 3;
	b.kv.push_back(gone);
	b.kv.push_back(raw);
	const auto back = decode_batch(encode_batch(b));
	EXPECT_EQ(back.epoch, 9u);
	ASSERT_EQ(back.kv.size(), 3u);
	EXPECT_TRUE(back.kv[1].erased);
	EXPECT_EQ(back.kv[1].key.owner_text, "acct-text");
	EXPECT_EQ(back.kv[2].encoded->bytes, std::string("S\x01\x02", 3));
	EXPECT_EQ(back.kv[2].cost, 77u);
	EXPECT_EQ(back.kv[2].row_version, 3u);
	EXPECT_EQ(back.records[0].payload.bytes, "v9");
}

TEST(PERSIST_WAL_013, CrashDuringReplayAfterCommitBeforeTrimLosesNothingAndDuplicatesNothing) {
	const auto dir = scratch_dir("wal013");
	const std::string dbfile = (dir / "db.sqlite").string();
	const fs::path waldir = dir / "wal";
	{
		Stack s(dbfile);
		install_audit(s.db);
		Wal wal({waldir, 1u << 20});
		for (std::uint64_t i = 1; i <= 6; ++i) wal.append(batch(i));
	}
	const int st = run_in_child([&] {
		Stack s(dbfile);
		Wal wal({waldir, 1u << 20});
		DirtyTracker t;
		WriteBehindFlusher f(s.db, t, wal);
		f.set_hook([](const char* point, std::uint64_t seq) {
			// batch 3 is committed (with its progress marker) but the segment is not removed yet
			if (std::string(point) == "replay:after_apply" && seq == 3) ::kill(::getpid(), SIGKILL);
		});
		f.drain_wal();
	});
	ASSERT_TRUE(WIFSIGNALED(st));
	ASSERT_EQ(WTERMSIG(st), SIGKILL);

	Stack s(dbfile);
	{
		EXPECT_EQ(audit(s.db), (std::vector<std::int64_t>{1, 2, 3}));  // what survived the kill
		Wal wal({waldir, 1u << 20});
		EXPECT_EQ(wal.segments(), 4u);  // 3 (applied, untrimmed) + 4,5,6
		DirtyTracker t;
		WriteBehindFlusher f(s.db, t, wal);
		EXPECT_EQ(f.drain_wal(), 4u);  // 3 is recognised as applied (skipped), 4..6 applied
		EXPECT_TRUE(wal.empty());
	}
	EXPECT_EQ(audit(s.db), (std::vector<std::int64_t>{1, 2, 3, 4, 5, 6}));  // each exactly once, in order
	EXPECT_EQ(record_payload(s.db, 1), "v6");
	EXPECT_EQ(scalar(s.db, "SELECT COUNT(*) FROM kv_entries"), 1);
}

TEST(PERSIST_WAL_014, CrashBeforeApplyAndRepeatedCrashesStillConvergeToExactlyOnce) {
	const auto dir = scratch_dir("wal014");
	const std::string dbfile = (dir / "db.sqlite").string();
	const fs::path waldir = dir / "wal";
	{
		Stack s(dbfile);
		install_audit(s.db);
		Wal wal({waldir, 1u << 20});
		for (std::uint64_t i = 1; i <= 8; ++i) wal.append(batch(i));
	}
	// crash at a different point on every restart: before apply of 2, after apply of 4, before apply of 7
	const std::pair<const char*, std::uint64_t> plan[] = {{"replay:before_apply", 2}, {"replay:after_apply", 4}, {"replay:before_apply", 7}};
	for (const auto& [point, seq] : plan) {
		const int st = run_in_child([&] {
			Stack s(dbfile);
			Wal wal({waldir, 1u << 20});
			DirtyTracker t;
			WriteBehindFlusher f(s.db, t, wal);
			f.set_hook([&](const char* p, std::uint64_t q) {
				if (std::string(p) == point && q == seq) ::kill(::getpid(), SIGKILL);
			});
			f.drain_wal();
		});
		ASSERT_TRUE(WIFSIGNALED(st)) << point << " " << seq;
	}
	Stack s(dbfile);
	Wal wal({waldir, 1u << 20});
	DirtyTracker t;
	WriteBehindFlusher f(s.db, t, wal);
	f.drain_wal();
	EXPECT_TRUE(wal.empty());
	EXPECT_EQ(audit(s.db), (std::vector<std::int64_t>{1, 2, 3, 4, 5, 6, 7, 8}));
	EXPECT_EQ(record_payload(s.db, 1), "v8");
	EXPECT_EQ(scalar(s.db, "SELECT COUNT(*) FROM persist_wal_progress"), 1);  // markers of old generations pruned
}

TEST(PERSIST_WAL_015, AWipedWalDirectoryWithAnOldDatabaseDoesNotSkipNewBatchesBecauseGenerationsDiffer) {
	const auto dir = scratch_dir("wal015");
	const std::string dbfile = (dir / "db.sqlite").string();
	Stack s(dbfile);
	install_audit(s.db);
	{
		Wal wal({dir / "wal", 1u << 20});
		DirtyTracker t;
		WriteBehindFlusher f(s.db, t, wal);
		wal.append(batch(1));
		wal.append(batch(2));
		f.drain_wal();
	}
	fs::remove_all(dir / "wal");  // operator wipes the WAL directory; sequence numbers restart at 1
	Wal wal({dir / "wal", 1u << 20});
	DirtyTracker t;
	WriteBehindFlusher f(s.db, t, wal);
	wal.append(batch(3));
	f.drain_wal();
	EXPECT_EQ(audit(s.db), (std::vector<std::int64_t>{1, 2, 3}));
}
