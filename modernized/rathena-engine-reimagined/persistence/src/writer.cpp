#include "rathena/persist/writer.hpp"

namespace rathena::persist {

WriteBehindService::WriteBehindService(SqlConnection& db, Wal& wal, WriteBehindOptions options)
    : db_(db), wal_(wal), opt_(std::move(options)), flusher_(db, tracker_, wal) {
	flusher_.set_retry_interval(opt_.retry_interval);
	if (opt_.hook) flusher_.set_hook(opt_.hook);
	// A previous run may have left WAL segments: replay them before anything new is written.
	flusher_.drain_wal();
	down_.store(flusher_.backend_down());
	worker_ = std::thread([this] { run(); });
}

WriteBehindService::~WriteBehindService() {
	try { (void)shutdown(); } catch (...) {}
}

void WriteBehindService::note_owner(const OwnerRef& o, std::uint64_t epoch) {
	std::scoped_lock l(omu_);
	auto& slot = owner_epoch_[o];
	if (epoch > slot) slot = epoch;
	if (owner_epoch_.size() > 8192) {  // forget owners that are already durable
		const auto d = durable_.load();
		for (auto it = owner_epoch_.begin(); it != owner_epoch_.end();) it = it->second <= d ? owner_epoch_.erase(it) : std::next(it);
	}
}

void WriteBehindService::mark(Record rec) {
	if (refusing_.load()) throw WalFullError("writes are refused: the database is down and the WAL is full");
	{
		std::scoped_lock l(qmu_);
		if (closed_) throw ServiceClosedError("write-behind service is shut down");
	}
	const auto owner = rec.key.owner;
	note_owner(owner, tracker_.mark(std::move(rec)));
}

void WriteBehindService::mark(KvEntry entry) {
	if (refusing_.load()) throw WalFullError("writes are refused: the database is down and the WAL is full");
	{
		std::scoped_lock l(qmu_);
		if (closed_) throw ServiceClosedError("write-behind service is shut down");
	}
	const auto owner = entry.key.owner;
	note_owner(owner, tracker_.mark(std::move(entry)));
}

WriteBehindService::Handoff WriteBehindService::handoff() {
	{
		std::scoped_lock l(qmu_);
		if (closed_) throw ServiceClosedError("write-behind service is shut down");
		if (queue_.size() >= opt_.max_queued_batches)
			throw WriteBacklogError("write-behind queue is full (" + std::to_string(queue_.size()) +
			                        " batches); the writer cannot keep up or the backend is down");
	}
	auto batch = tracker_.swap_out();
	if (batch->empty()) return {};
	Handoff h{batch->epoch, batch->records.size(), batch->kv.size()};
	{
		std::scoped_lock l(qmu_);
		queue_.push_back(std::move(batch));
	}
	handed_.fetch_add(1);
	qcv_.notify_one();
	return h;
}

std::optional<std::uint64_t> WriteBehindService::pending_epoch(const OwnerRef& owner) {
	std::scoped_lock l(omu_);
	const auto it = owner_epoch_.find(owner);
	if (it == owner_epoch_.end()) return std::nullopt;
	if (it->second <= durable_.load()) {
		owner_epoch_.erase(it);
		return std::nullopt;
	}
	return it->second;
}

bool WriteBehindService::wait_durable(std::uint64_t epoch, std::chrono::milliseconds timeout) {
	std::unique_lock l(dmu_);
	return dcv_.wait_for(l, timeout, [&] { return durable_.load() >= epoch; });
}

bool WriteBehindService::barrier(const OwnerRef& owner, std::chrono::milliseconds timeout) {
	const auto e = pending_epoch(owner);
	if (!e) return true;
	if (*e >= tracker_.next_epoch()) (void)handoff();  // the owner is still in the pending set
	return wait_durable(*e, timeout);
}

void WriteBehindService::set_applied_callback(std::function<void(const DirtyBatch&)> cb) {
	flusher_.set_applied_callback(std::move(cb));
}

void WriteBehindService::run() {
	std::unique_lock l(qmu_);
	for (;;) {
		const bool recovering = !queue_.empty() ? false : (!wal_.empty() && !stop_);
		if (queue_.empty()) {
			if (stop_) break;
			if (recovering) {
				// idle with a non-empty WAL: probe the backend for recovery
				qcv_.wait_for(l, opt_.retry_interval, [&] { return stop_ || !queue_.empty(); });
				if (queue_.empty() && !stop_) {
					l.unlock();
					try { flusher_.drain_wal(); } catch (...) {}
					down_.store(flusher_.backend_down());
					if (wal_.empty()) refusing_.store(false);
					l.lock();
				}
			} else {
				qcv_.wait(l, [&] { return stop_ || !queue_.empty(); });
			}
			continue;
		}
		DirtyBatchPtr batch = queue_.front();  // stays queued until persisted: nothing is lost on failure
		l.unlock();
		bool done = false;
		try {
			const auto res = flusher_.persist(*batch);
			(res.spilled_to_wal ? spilled_ : direct_).fetch_add(1);
			refusing_.store(false);
			done = true;
		} catch (const WalFullError&) {
			refusing_.store(true);  // DB down and WAL full: refuse new writes, keep this batch, retry
		} catch (...) {
			refusing_.store(false);  // e.g. WAL I/O error: keep the batch and retry
		}
		down_.store(flusher_.backend_down());
		const std::uint64_t epoch = batch->epoch;
		batch.reset();  // large batches are freed outside qmu_: that would stall the producer's hand-off
		if (done) {
			{
				std::scoped_lock dl(dmu_);
				durable_.store(epoch);  // single writer, FIFO: epochs are contiguous and increasing
			}
			dcv_.notify_all();
			l.lock();
			DirtyBatchPtr front = std::move(queue_.front());
			queue_.pop_front();
			l.unlock();
			front.reset();
			l.lock();
		} else {
			l.lock();
			if (stop_) break;  // shutting down with an unpersistable batch: reported, not dropped
			qcv_.wait_for(l, opt_.retry_interval, [&] { return stop_; });
		}
	}
	unpersisted_ = queue_.size();
}

ShutdownReport WriteBehindService::shutdown() {
	bool first = false;
	{
		std::scoped_lock l(qmu_);
		if (!closed_) { closed_ = true; first = true; }
	}
	if (first) {
		// final hand-off of whatever is still marked, bypassing the closed check
		auto batch = tracker_.swap_out();
		if (!batch->empty()) {
			std::scoped_lock l(qmu_);
			queue_.push_back(std::move(batch));
			handed_.fetch_add(1);
		}
		{
			std::scoped_lock l(qmu_);
			stop_ = true;
		}
		qcv_.notify_all();
	}
	if (worker_.joinable()) worker_.join();
	if (first) {
		try { flusher_.drain_wal(); } catch (...) {}
		wal_empty_at_end_ = wal_.empty();
	}
	return ShutdownReport{unpersisted_, wal_empty_at_end_};
}

ServiceStats WriteBehindService::stats() const {
	ServiceStats s;
	s.handed_off = handed_.load();
	s.direct = direct_.load();
	s.spilled = spilled_.load();
	s.durable_epoch = durable_.load();
	{
		std::scoped_lock l(qmu_);
		s.queued = queue_.size();
	}
	s.backend_down = down_.load();
	s.refusing_writes = refusing_.load();
	return s;
}

} // namespace rathena::persist
