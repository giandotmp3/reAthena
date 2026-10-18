#pragma once
// Threaded write-behind service: the producer (sim) thread marks dirty state and hands off an
// immutable batch pointer at the tick boundary; one writer thread persists batches in order
// (per-owner order = batch FIFO order), spilling to the bounded WAL when the backend fails.
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <map>
#include <thread>

#include "rathena/persist/service.hpp"

namespace rathena::persist {

struct WriteBehindOptions {
	std::chrono::milliseconds retry_interval{100};  ///< backend recovery probe period
	std::size_t max_queued_batches{256};            ///< hand-off refuses (WriteBacklogError) beyond this
	FlushHook hook;                                  ///< test seam, see FlushHook
};

struct ServiceStats {
	std::uint64_t handed_off{0};       ///< batches accepted by handoff()
	std::uint64_t direct{0};           ///< batches written straight to the database
	std::uint64_t spilled{0};          ///< batches appended to the WAL
	std::uint64_t durable_epoch{0};    ///< highest epoch safe in the database or the WAL (contiguous)
	std::size_t queued{0};
	bool backend_down{false};
	bool refusing_writes{false};       ///< WAL full and backend down: mark() throws WalFullError
};

struct ShutdownReport {
	std::size_t unpersisted_batches{0}; ///< neither in the database nor in the WAL (WAL was full)
	bool wal_empty{true};               ///< false: the rest is on disk and replays at next start
};

class WriteBehindService {
public:
	WriteBehindService(SqlConnection& db, Wal& wal, WriteBehindOptions options = {});
	~WriteBehindService();
	WriteBehindService(const WriteBehindService&) = delete;
	WriteBehindService& operator=(const WriteBehindService&) = delete;

	/// Throws WalFullError while writes are refused, ServiceClosedError after shutdown().
	void mark(Record rec);
	void mark(KvEntry entry);

	struct Handoff {
		std::uint64_t epoch{0};   ///< 0 = nothing was pending
		std::size_t records{0};
		std::size_t kv{0};
	};
	/// O(1) pointer hand-off of the pending set to the writer. Throws WriteBacklogError when the
	/// queue is full (the pending set stays in the tracker, nothing is lost).
	Handoff handoff();

	/// Epoch in which the owner's newest outstanding write travels (nullopt: nothing outstanding).
	[[nodiscard]] std::optional<std::uint64_t> pending_epoch(const OwnerRef& owner);
	/// Save->auth barrier: hands off if the owner is still in the pending set, then waits until
	/// everything marked for the owner is durable (database or WAL). False on timeout.
	bool barrier(const OwnerRef& owner, std::chrono::milliseconds timeout);
	bool wait_durable(std::uint64_t epoch, std::chrono::milliseconds timeout);
	[[nodiscard]] std::uint64_t durable_epoch() const { return durable_.load(); }

	/// Called on the writer thread after a batch reached the database (not merely the WAL).
	void set_applied_callback(std::function<void(const DirtyBatch&)> cb);

	/// Final hand-off, drain of the queue and of the WAL if the backend is up, then joins.
	ShutdownReport shutdown();
	[[nodiscard]] ServiceStats stats() const;
	[[nodiscard]] bool refusing_writes() const { return refusing_.load(); }

private:
	void run();
	void note_owner(const OwnerRef& o, std::uint64_t epoch);

	SqlConnection& db_;
	Wal& wal_;
	WriteBehindOptions opt_;
	DirtyTracker tracker_;
	WriteBehindFlusher flusher_;

	mutable std::mutex qmu_;
	std::condition_variable qcv_;
	std::deque<DirtyBatchPtr> queue_;
	bool stop_{false};
	bool closed_{false};

	std::mutex dmu_;
	std::condition_variable dcv_;
	std::atomic<std::uint64_t> durable_{0};

	std::mutex omu_;
	std::map<OwnerRef, std::uint64_t> owner_epoch_;

	std::atomic<bool> refusing_{false};
	std::atomic<bool> down_{false};
	std::atomic<std::uint64_t> handed_{0}, direct_{0}, spilled_{0};
	std::size_t unpersisted_{0};
	bool wal_empty_at_end_{true};
	std::thread worker_;
};

} // namespace rathena::persist
