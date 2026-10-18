#pragma once
// Interface contracts of the persistence service (architecture 3, 4, 5 "Storage", 6.2 id_map).
// Everything here is generic: opaque records, namespaced KV, write-behind, WAL, migrations,
// id_map, quarantine. No gameplay repositories.
// SCAFFOLD: method bodies throw NotImplementedError (src/stubs.cpp); acceptance tests are in tests/.
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "rathena/persist/domain.hpp"
#include "rathena/persist/errors.hpp"
#include "rathena/persist/sql_connection.hpp"

namespace rathena::persist {

// ---- Quarantine ---------------------------------------------------------------------------
class Quarantine {
public:
	explicit Quarantine(SqlConnection& db);
	std::uint64_t put(QuarantinedRow row);
	[[nodiscard]] std::vector<QuarantinedRow> list() const;
	[[nodiscard]] bool contains(const std::string& table, const std::string& row_ref) const;
	/// Operator action: returns the raw row and removes it from quarantine.
	std::optional<QuarantinedRow> release(std::uint64_t quarantine_id);
private:
	SqlConnection& db_;
};

// ---- Save admission (BR-CHAR-032) ---------------------------------------------------------
/// Which owners are currently online on this process (the authority allowed to save them).
class OnlineLeases {
public:
	void acquire(const OwnerRef& owner);
	void release(const OwnerRef& owner);
	[[nodiscard]] bool held(const OwnerRef& owner) const;
private:
	std::set<OwnerRef> held_;
};

struct SaveRequest {
	Record record;
	bool final_save{false};            ///< logout / shutdown flush
	bool restore{false};               ///< restore from backup / import
	std::uint64_t declared_size{0};    ///< payload size the sender claims
};
enum class SaveOutcome : std::uint8_t { Accepted, RejectedNotOnline, RejectedSizeMismatch };

struct CollectionPolicy {
	std::size_t max_items{0}; ///< 0 = unbounded; ruleset config supplies the number (e.g. 20)
	bool restore_once{false}; ///< items are handed back by the first load only
};

// ---- Record store ---------------------------------------------------------------------------
class RecordStore {
public:
	RecordStore(SqlConnection& db, const OnlineLeases& leases, Quarantine& quarantine);
	void set_collection_policy(const std::string& kind, CollectionPolicy policy);
	SaveOutcome save(const SaveRequest& req);
	[[nodiscard]] std::optional<Record> load(const RecordKey& key);
	/// Collections: ordered opaque items per (owner, kind). Throws CollectionCapExceededError
	/// when over the kind's cap (rejected, never silently truncated).
	void save_collection(const RecordKey& key, const std::vector<Blob>& items);
	[[nodiscard]] std::vector<Blob> load_collection(const RecordKey& key);
private:
	SqlConnection& db_;
	const OnlineLeases& leases_;
	Quarantine& quarantine_;
	std::map<std::string, CollectionPolicy> policies_;
};

// ---- Namespaced KV --------------------------------------------------------------------------
struct KvPolicy {
	std::uint64_t mod_quota_bytes{1u << 20}; ///< per (mod, scope) quota, enforced here
	/// Compatibility switch used by the `legacy` mod: storing 0 / "" deletes the entry.
	bool delete_on_zero_or_empty{false};
};

/// kv row version migration: `storage.migrate(from_version, row)` (architecture 5, Storage upgrades).
class KvMigrationRegistry {
public:
	using Fn = std::function<Value(const Value&)>;
	void register_step(const std::string& mod_id, std::uint32_t from_version, Fn fn);
	[[nodiscard]] bool has_path(const std::string& mod_id, std::uint32_t stored, std::uint32_t current) const;
	[[nodiscard]] Value upgrade(const std::string& mod_id, std::uint32_t stored, std::uint32_t current, Value v) const;
private:
	std::map<std::pair<std::string, std::uint32_t>, Fn> steps_;
};

class KvStore {
public:
	KvStore(SqlConnection& db, Quarantine& quarantine, KvPolicy policy = {});
	/// Throws QuarantinedRowError (typed) when that row is quarantined.
	[[nodiscard]] std::optional<Value> get(const KvKey& key);
	/// Throws QuotaExceededError when the mod's quota would be exceeded.
	void set(const KvKey& key, Value value);
	bool erase(const KvKey& key);
	[[nodiscard]] std::uint64_t bytes_used(const std::string& mod_id) const;
private:
	SqlConnection& db_;
	Quarantine& quarantine_;
	KvPolicy policy_;
};

// ---- Write-behind, WAL --------------------------------------------------------------------
class DirtyTracker {
public:
	void mark(Record rec);
	void mark(KvEntry entry);
	/// Atomically swaps the pending set out and returns it immutable (copy-on-write at the tick
	/// boundary). Later mark() calls never mutate a batch already returned. Repeated marks of the
	/// same key between swaps coalesce to the latest.
	[[nodiscard]] DirtyBatchPtr swap_out();
	[[nodiscard]] std::size_t pending() const;
private:
	std::map<RecordKey, Record> records_;
	std::map<KvKey, KvEntry> kv_;
	std::uint64_t epoch_{0};
};

struct WalConfig {
	std::filesystem::path dir;    ///< from env ${RATHENA_WAL_DIR}; no baked-in path
	std::uint64_t max_bytes{64u << 20};
};

class Wal {
public:
	explicit Wal(WalConfig cfg);
	/// Throws WalFullError when the append would exceed max_bytes (the batch is NOT written).
	void append(const DirtyBatch& batch);
	/// Feeds batches to `sink` in append order; a batch is removed only after `sink` returns.
	/// Re-running after a crash may re-deliver a batch, so sinks must be idempotent (revisions).
	std::size_t replay(const std::function<void(const DirtyBatch&)>& sink);
	[[nodiscard]] std::uint64_t bytes_used() const;
	[[nodiscard]] bool accepting_writes() const;
private:
	WalConfig cfg_;
};

struct FlushResult {
	std::size_t records_written{0};
	std::size_t kv_written{0};
	bool spilled_to_wal{false};
};

class WriteBehindFlusher {
public:
	WriteBehindFlusher(SqlConnection& db, DirtyTracker& tracker, Wal& wal);
	/// Runs on a persistence worker, never on the sim tick. On database failure the batch is
	/// appended to the WAL; WalFullError propagates when the WAL is full (writes refused).
	FlushResult flush_once();
	/// Replays the WAL into the database after an outage or a restart.
	std::size_t drain_wal();
private:
	SqlConnection& db_;
	DirtyTracker& tracker_;
	Wal& wal_;
};

// ---- Schema migrations ----------------------------------------------------------------------
struct Migration {
	std::uint32_t version{0};
	std::string name;
	std::string checksum;               ///< detects edits of an already applied migration
	std::vector<Statement> up_sqlite;
	std::vector<Statement> up_postgres;
};

class MigrationRunner {
public:
	MigrationRunner(SqlConnection& db, std::vector<Migration> migrations);
	[[nodiscard]] std::uint32_t current_version();
	/// Applies pending migrations in order, each in one transaction. Throws MigrationError for
	/// gaps, checksum drift of an applied migration, or a database newer than this build.
	std::vector<std::uint32_t> apply_pending();
private:
	SqlConnection& db_;
	std::vector<Migration> migrations_;
};

// ---- id_map with tombstones -----------------------------------------------------------------
struct IdRange { std::uint32_t first{0}; std::uint32_t last{0}; };

class IdMap {
public:
	IdMap(SqlConnection& db, std::map<std::string, IdRange> allocation_ranges);
	/// Explicit `client_id` keeps a migrated legacy number; otherwise allocates from the kind's
	/// range. Idempotent for the same (kind, namespaced). Throws IdCollisionError naming both ids.
	std::uint32_t assign(const std::string& kind, const std::string& namespaced,
	                     std::optional<std::uint32_t> explicit_client_id = std::nullopt);
	[[nodiscard]] std::optional<std::uint32_t> client_id(const std::string& kind, const std::string& namespaced) const;
	[[nodiscard]] std::optional<std::string> namespaced_id(const std::string& kind, std::uint32_t client_id) const;
	/// Mod removed: the number is tombstoned forever and never reassigned.
	void retire(const std::string& kind, const std::string& namespaced);
	[[nodiscard]] bool is_tombstoned(const std::string& kind, std::uint32_t client_id) const;
private:
	SqlConnection& db_;
	std::map<std::string, IdRange> ranges_;
};

} // namespace rathena::persist
