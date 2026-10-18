#pragma once
// script-host StorageBackend on top of the persistence mod KV store (architecture 5 "Storage").
//
// Write-through (default): every store()/erase() commits to the database before it returns.
// Write-behind: pass a WriteBehindService; store()/erase() charge the quota ledger, update an
// in-memory overlay (read-your-writes) and mark the row dirty; the service persists it later and
// the entry turns into a clean cache entry once the row is in the database. Reads in this mode go
// through that cache (bounded; absent rows are cached too), so the hot path does not touch SQLite
// and keeps working while the database is down for every row that was read or written before.
// A row never seen before cannot be read during an outage: the read throws RepositoryError
// (never a silent "absent"). The cache assumes this process is the only writer of the database.
// Per-mod quotas are enforced here (KvStore ledger), in addition to StorageApi's own check.
//
// Lifetime: destroy the WriteBehindService (or shut it down) before the adapter.
#include <map>
#include <mutex>

#include "rathena/persist/service.hpp"
#include "rathena/persist/writer.hpp"
#include "script_host/storage.hpp"

namespace rathena::persist {

/// Cost charged to the quota, identical to script_host::MemoryStorageBackend so both backends
/// behave the same under StorageApi.
std::uint64_t script_host_row_cost(const script_host::StorageKey& k, const script_host::Row& r);

class KvStorageBackend final : public script_host::StorageBackend {
public:
	explicit KvStorageBackend(KvStore& kv, WriteBehindService* write_behind = nullptr,
	                          std::size_t max_cached_rows = 65536);

	std::optional<script_host::Row> load(const script_host::StorageKey& k) const override;
	void store(const script_host::StorageKey& k, const script_host::Row& r) override;
	bool erase(const script_host::StorageKey& k) override;
	std::uint64_t usage(const std::string& mod) const override;
	std::vector<std::string> keys(script_host::Scope s, const std::string& owner, const std::string& mod) const override;

	/// Persistence-layer quota for a mod (should match StorageApi::set_quota).
	void set_mod_quota(const std::string& mod, std::uint64_t bytes) { kv_.set_mod_quota(mod, bytes); }
	/// Number of rows written but not yet in the database (write-behind mode).
	[[nodiscard]] std::size_t overlay_size() const;

private:
	struct Pending {
		bool erased{false};   ///< tombstone: the row is (to be) absent
		bool dirty{false};    ///< not yet in the database; clean entries are cache only
		script_host::Row row;
		std::uint64_t revision{0};
		std::uint64_t cost{0};
	};
	void on_applied(const DirtyBatch& batch);
	void trim_cache() const;  // caller holds mu_
	std::uint64_t next_revision(std::uint64_t old_rev);  // caller holds mu_
	KvStore& kv_;
	WriteBehindService* wb_;
	std::size_t max_cached_;
	mutable std::mutex mu_;
	mutable std::map<KvKey, Pending> overlay_;
	std::uint64_t last_revision_{0};
};

} // namespace rathena::persist
