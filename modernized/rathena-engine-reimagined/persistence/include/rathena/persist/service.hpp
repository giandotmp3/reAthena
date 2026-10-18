#pragma once
// Interface contracts of the persistence service (architecture 3, 4, 5 "Storage", 6.2 id_map).
// Everything here is generic: opaque records, namespaced KV, write-behind, WAL, migrations,
// id_map, quarantine. No gameplay repositories.
//
// Threading: stores (RecordStore, KvStore, IdMap, Quarantine) are not internally synchronized
// against each other; they share one SqlConnection and the caller serializes access, either by
// confining them to one thread or by giving them a GuardedConnection (connection_wrappers.hpp).
// Wal, DirtyTracker and OnlineLeases are internally synchronized.
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
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
	/// ensure_schema = false: the caller guarantees the schema exists (or creates it itself later).
	explicit Quarantine(SqlConnection& db, bool ensure_schema = true);
	/// Allocates a never-reused id, stores the row (raw bytes included) and returns the id.
	std::uint64_t put(QuarantinedRow row);
	[[nodiscard]] std::vector<QuarantinedRow> list() const;
	/// True when any owner has a quarantined row with this table and row_ref.
	[[nodiscard]] bool contains(const std::string& table, const std::string& row_ref) const;
	/// Exact form: the row of one owner.
	[[nodiscard]] bool contains_for(const std::string& table, const std::string& row_ref,
	                                const std::string& owner) const;
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
	mutable std::mutex mu_;
	std::set<OwnerRef> held_;
};

struct SaveRequest {
	Record record;
	bool final_save{false};            ///< logout / shutdown flush
	bool restore{false};               ///< restore from backup / import
	std::uint64_t declared_size{0};    ///< payload size the sender claims
};
/// RejectedStaleRevision: a stored row with a higher revision exists (the save is superseded).
enum class SaveOutcome : std::uint8_t { Accepted, RejectedNotOnline, RejectedSizeMismatch, RejectedStaleRevision };

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
	/// Throws QuarantinedRowError when the row is (or just became) quarantined.
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
	/// Quota in bytes per mod, summed over every owner and scope of that mod; enforced here.
	std::uint64_t mod_quota_bytes{1u << 20};
	/// Compatibility switch used by the `legacy` mod: storing 0 / "" deletes the entry.
	bool delete_on_zero_or_empty{false};
};

/// kv row version migration: `storage.migrate(from_version, row)` (architecture 5, Storage upgrades).
class KvMigrationRegistry {
public:
	using Fn = std::function<Value(const Value&)>;
	/// Step that upgrades a row stored at `from_version` to `from_version + 1`.
	void register_step(const std::string& mod_id, std::uint32_t from_version, Fn fn);
	[[nodiscard]] bool has_path(const std::string& mod_id, std::uint32_t stored, std::uint32_t current) const;
	/// Throws MigrationError when there is no path.
	[[nodiscard]] Value upgrade(const std::string& mod_id, std::uint32_t stored, std::uint32_t current, Value v) const;
private:
	std::map<std::pair<std::string, std::uint32_t>, Fn> steps_;
};

/// Stored form of a KV row for codecs other than the built-in Value codec.
struct RawKv {
	std::uint32_t row_version{1};
	std::uint64_t revision{0};
	std::uint64_t cost{0};  ///< bytes charged to the mod's quota
	Blob bytes;             ///< codec byte + body
};
struct RawKvMeta {
	std::uint64_t revision{0};
	std::uint64_t cost{0};
};

class KvStore {
public:
	KvStore(SqlConnection& db, Quarantine& quarantine, KvPolicy policy = {});
	void set_migrations(const KvMigrationRegistry* registry);
	/// Declares the mod's current row version (default 1). Older rows are upgraded on read.
	void set_mod_version(const std::string& mod_id, std::uint32_t version);
	void set_mod_quota(const std::string& mod_id, std::uint64_t bytes);
	[[nodiscard]] std::uint64_t quota(const std::string& mod_id) const;
	/// Loads the quota ledger of every mod with one query, so that later quota checks never need
	/// the database (write-behind keeps accepting writes while the backend is down). Throws
	/// RepositoryError if the backend is unreachable now.
	void warm_up();

	/// Throws QuarantinedRowError (typed) when that row is quarantined, MigrationError when the
	/// stored version has no migration path.
	[[nodiscard]] std::optional<Value> get(const KvKey& key);
	/// Throws QuotaExceededError when the mod's quota would be exceeded (nothing is written).
	void set(const KvKey& key, Value value);
	bool erase(const KvKey& key);
	[[nodiscard]] std::uint64_t bytes_used(const std::string& mod_id) const;

	// Raw layer (script-host adapter, write-behind flush).
	[[nodiscard]] std::optional<RawKv> get_raw(const KvKey& key);
	[[nodiscard]] std::optional<RawKvMeta> meta(const KvKey& key);
	void set_raw(const KvKey& key, RawKv row);
	/// Quota ledger for writes that reach the database later (write-behind). admit() throws
	/// QuotaExceededError and changes nothing when used - old_cost + new_cost > quota.
	void admit(const std::string& mod_id, std::uint64_t new_cost, std::uint64_t old_cost);
	void release(const std::string& mod_id, std::uint64_t cost);
	/// Moves a corrupt row out of the live table (raw bytes preserved) and marks it quarantined.
	void quarantine_row(const KvKey& key, const std::string& reason, Blob raw);
	[[nodiscard]] bool is_quarantined(const KvKey& key) const;
	/// Live keys (database state) of one owner and mod, sorted.
	[[nodiscard]] std::vector<std::string> keys(OwnerScope scope, const std::string& owner_ref,
	                                            const std::string& mod_id) const;
private:
	[[nodiscard]] std::uint64_t used_locked(const std::string& mod_id) const;
	SqlConnection& db_;
	Quarantine& quarantine_;
	KvPolicy policy_;
	const KvMigrationRegistry* migrations_{nullptr};
	std::map<std::string, std::uint32_t> versions_;
	std::map<std::string, std::uint64_t> quotas_;
	mutable std::mutex ledger_mu_;
	mutable std::map<std::string, std::uint64_t> used_;
	mutable bool ledger_loaded_{false};
};

// ---- Write-behind, WAL --------------------------------------------------------------------
class DirtyTracker {
public:
	DirtyTracker();
	~DirtyTracker();
	DirtyTracker(const DirtyTracker&) = delete;
	DirtyTracker& operator=(const DirtyTracker&) = delete;
	/// Both return the epoch of the batch the entry will be part of.
	std::uint64_t mark(Record rec);
	std::uint64_t mark(KvEntry entry);
	/// Atomically swaps the pending set out and returns it immutable (copy-on-write at the tick
	/// boundary). Later mark() calls never mutate a batch already returned. Repeated marks of the
	/// same key between swaps coalesce to the latest. O(1): the pending buffers are moved, not
	/// copied, and the retired index is freed by whoever drops the batch.
	[[nodiscard]] DirtyBatchPtr swap_out();
	[[nodiscard]] std::size_t pending() const;
	/// Puts a batch that could not be persisted back; keys marked since the swap are newer and win.
	void restore(const DirtyBatch& batch);
	/// Epoch the next non-empty swap_out() will carry.
	[[nodiscard]] std::uint64_t next_epoch() const;
private:
	struct Buffer;
	mutable std::mutex mu_;
	std::unique_ptr<Buffer> active_;
	std::uint64_t epoch_{0};
};

enum class WalFsync : std::uint8_t {
	Never,       ///< rely on the OS (fast; a power cut may lose the newest segments)
	File,        ///< fsync the segment before it becomes visible
	FileAndDir,  ///< also fsync the directory (rename and unlink are durable). Default.
};

struct WalConfig {
	std::filesystem::path dir;    ///< from env ${RATHENA_WAL_DIR}; no baked-in path
	std::uint64_t max_bytes{64u << 20};
	WalFsync fsync{WalFsync::FileAndDir};
};

/// Reads RATHENA_WAL_DIR (env only). Throws DbConfigError when unset or empty.
WalConfig load_wal_config_from_env(const char* var_name = "RATHENA_WAL_DIR");

/// Append-only segments, one file per batch (`<seq>.wal`, written to a temp name, fsynced and
/// renamed, so a torn write never appears under a final name). Every segment carries a CRC.
class Wal {
public:
	explicit Wal(WalConfig cfg);
	~Wal();
	Wal(const Wal&) = delete;
	Wal& operator=(const Wal&) = delete;
	/// Throws WalFullError when the append would exceed max_bytes (the batch is NOT written),
	/// WalIoError on I/O failure. Returns the segment sequence number.
	std::uint64_t append(const DirtyBatch& batch);
	/// Feeds batches to `sink` in append order; a batch is removed only after `sink` returns.
	/// Re-running after a crash may re-deliver a batch, so sinks must be idempotent (see
	/// WriteBehindFlusher::drain_wal, which dedups on a database marker).
	std::size_t replay(const std::function<void(const DirtyBatch&)>& sink);
	std::size_t replay_seq(const std::function<void(std::uint64_t seq, const DirtyBatch&)>& sink);
	[[nodiscard]] std::uint64_t bytes_used() const;
	[[nodiscard]] bool accepting_writes() const;
	[[nodiscard]] bool empty() const;
	[[nodiscard]] std::size_t segments() const;
	/// Identifier of this WAL generation; the database marker is keyed by it.
	[[nodiscard]] const std::string& id() const;
	/// Segments that failed their CRC/structure check; they were renamed `*.corrupt` and skipped.
	[[nodiscard]] std::vector<std::string> corrupt_segments() const;
private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

struct FlushResult {
	std::size_t records_written{0};
	std::size_t kv_written{0};
	bool spilled_to_wal{false};
	std::size_t quarantined{0};  ///< entries the backend rejected and that were quarantined
};

/// Test seam: called at named points of the flush/replay path ("replay:before_apply",
/// "replay:after_apply", "replay:after_trim"). Production code leaves it empty.
using FlushHook = std::function<void(const char* point, std::uint64_t seq)>;

class WriteBehindFlusher {
public:
	WriteBehindFlusher(SqlConnection& db, DirtyTracker& tracker, Wal& wal);
	/// Runs on a persistence worker, never on the sim tick. On database failure the batch is
	/// appended to the WAL; WalFullError propagates when the WAL is full (writes refused).
	FlushResult flush_once();
	/// Persists one batch with the ordering rules: older WAL batches first, then this one; if the
	/// backend is down it goes to the WAL. Throws WalFullError (batch not persisted anywhere).
	FlushResult persist(const DirtyBatch& batch);
	/// Replays the WAL into the database after an outage or a restart, exactly once per batch.
	/// Returns the number of batches consumed. Stops (and keeps the rest) at the first backend
	/// failure; the failure is not thrown, see backend_down().
	std::size_t drain_wal();
	[[nodiscard]] bool backend_down() const noexcept { return backend_down_; }
	void set_hook(FlushHook hook) { hook_ = std::move(hook); }
	/// Called after a batch has been applied to the database (directly or from the WAL).
	void set_applied_callback(std::function<void(const DirtyBatch&)> cb) { applied_cb_ = std::move(cb); }
	/// While the backend is down, WAL replay is attempted at most this often (0 = every call).
	void set_retry_interval(std::chrono::milliseconds d) { retry_interval_ = d; }
private:
	struct Applied { std::size_t records{0}; std::size_t kv{0}; std::size_t quarantined{0}; };
	Applied apply(const DirtyBatch& batch, std::optional<std::pair<std::string, std::uint64_t>> marker);
	void quarantine_entry(const DirtyBatch& batch, std::size_t index, bool is_kv, const std::string& reason);
	void ensure_schema_once();
	SqlConnection& db_;
	DirtyTracker& tracker_;
	Wal& wal_;
	Quarantine quarantine_;
	bool backend_down_{false};
	bool schema_ok_{false};
	FlushHook hook_;
	std::function<void(const DirtyBatch&)> applied_cb_;
	std::chrono::milliseconds retry_interval_{0};
	std::chrono::steady_clock::time_point last_drain_attempt_{};
};

// ---- Schema migrations ----------------------------------------------------------------------
struct Migration {
	std::uint32_t version{0};
	std::string name;
	std::string checksum;               ///< detects edits of an already applied migration
	std::vector<Statement> up_sqlite;
	std::vector<Statement> up_postgres;
};

struct MigrationOptions {
	/// The runner owns versions in [managed_from, managed_below); rows outside that range belong
	/// to another runner sharing the table (the built-in schema owns [1, 1000), an application
	/// uses managed_from = 1000). managed_below 0 = unbounded. The list must be contiguous from
	/// managed_from.
	std::uint32_t managed_from{1};
	std::uint32_t managed_below{0};
};

class MigrationRunner {
public:
	MigrationRunner(SqlConnection& db, std::vector<Migration> migrations, MigrationOptions options = {});
	[[nodiscard]] std::uint32_t current_version();
	/// Applies pending migrations in order, each in one transaction. Throws MigrationError for
	/// gaps, checksum drift of an applied migration, a failing statement, or a database newer
	/// than this build.
	std::vector<std::uint32_t> apply_pending();
private:
	SqlConnection& db_;
	std::vector<Migration> migrations_;
	MigrationOptions options_;
};

/// The built-in schema (versions 1..N, below kForeignMigrationBase).
inline constexpr std::uint32_t kForeignMigrationBase = 1000;
[[nodiscard]] std::vector<Migration> builtin_migrations();
/// Applies the built-in schema if needed. Idempotent; stores call it from their constructors.
void ensure_schema(SqlConnection& db);

// ---- id_map with tombstones -----------------------------------------------------------------
struct IdRange { std::uint32_t first{0}; std::uint32_t last{0}; };

class IdMap {
public:
	IdMap(SqlConnection& db, std::map<std::string, IdRange> allocation_ranges);
	/// Explicit `client_id` keeps a migrated legacy number; otherwise allocates from the kind's
	/// range. Idempotent for the same (kind, namespaced); a retired string id that returns gets
	/// its old number back. Throws IdCollisionError naming both ids, IdRangeExhaustedError.
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
