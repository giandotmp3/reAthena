#include <unordered_map>

#include "rathena/persist/service.hpp"

namespace rathena::persist {
namespace {
struct RecordKeyHash {
	std::size_t operator()(const RecordKey& k) const noexcept {
		return std::hash<std::string>{}(k.kind) ^ (std::hash<std::uint64_t>{}(k.owner.id) * 0x9E3779B97F4A7C15ull) ^
		       static_cast<std::size_t>(k.owner.scope);
	}
};
struct KvKeyHash {
	std::size_t operator()(const KvKey& k) const noexcept {
		std::size_t h = std::hash<std::string>{}(k.key);
		h ^= std::hash<std::string>{}(k.mod_id) + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
		h ^= std::hash<std::uint64_t>{}(k.owner.id) * 0x9E3779B97F4A7C15ull;
		if (!k.owner_text.empty()) h ^= std::hash<std::string>{}(k.owner_text);
		return h ^ static_cast<std::size_t>(k.owner.scope);
	}
};
} // namespace

struct DirtyTracker::Buffer {
	std::vector<Record> records;
	std::vector<KvEntry> kv;
	std::unordered_map<RecordKey, std::size_t, RecordKeyHash> record_index;
	std::unordered_map<KvKey, std::size_t, KvKeyHash> kv_index;
};

DirtyTracker::DirtyTracker() : active_(std::make_unique<Buffer>()) {}
DirtyTracker::~DirtyTracker() = default;

std::uint64_t DirtyTracker::mark(Record rec) {
	std::scoped_lock l(mu_);
	auto [it, inserted] = active_->record_index.try_emplace(rec.key, active_->records.size());
	if (inserted) active_->records.push_back(std::move(rec));
	else active_->records[it->second] = std::move(rec);  // coalesce to the latest
	return epoch_ + 1;
}

std::uint64_t DirtyTracker::mark(KvEntry entry) {
	std::scoped_lock l(mu_);
	auto [it, inserted] = active_->kv_index.try_emplace(entry.key, active_->kv.size());
	if (inserted) active_->kv.push_back(std::move(entry));
	else active_->kv[it->second] = std::move(entry);
	return epoch_ + 1;
}

DirtyBatchPtr DirtyTracker::swap_out() {
	std::unique_ptr<Buffer> old;
	std::uint64_t epoch = 0;
	{
		std::scoped_lock l(mu_);
		if (active_->records.empty() && active_->kv.empty()) return std::make_shared<const DirtyBatch>();
		old = std::exchange(active_, std::make_unique<Buffer>());
		epoch = ++epoch_;
	}
	// Everything below touches only the retired buffer: no lock, no per-entry work. The retired
	// index maps ride along in the batch and are destroyed wherever the last reference drops
	// (the writer thread), not here on the producer.
	auto batch = std::make_shared<DirtyBatch>();
	batch->epoch = epoch;
	batch->records = std::move(old->records);
	batch->kv = std::move(old->kv);
	batch->retired = std::shared_ptr<void>(old.release(), [](void* p) { delete static_cast<Buffer*>(p); });
	return batch;
}

void DirtyTracker::restore(const DirtyBatch& batch) {
	std::scoped_lock l(mu_);
	for (const auto& r : batch.records)
		if (active_->record_index.try_emplace(r.key, active_->records.size()).second) active_->records.push_back(r);
	for (const auto& e : batch.kv)
		if (active_->kv_index.try_emplace(e.key, active_->kv.size()).second) active_->kv.push_back(e);
}

std::size_t DirtyTracker::pending() const {
	std::scoped_lock l(mu_);
	return active_->records.size() + active_->kv.size();
}

std::uint64_t DirtyTracker::next_epoch() const {
	std::scoped_lock l(mu_);
	return epoch_ + 1;
}

} // namespace rathena::persist
