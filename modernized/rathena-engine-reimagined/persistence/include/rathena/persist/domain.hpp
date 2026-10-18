#pragma once
// Domain model. Persistence is generic: it stores opaque typed records keyed by (owner, kind) and
// namespaced KV entries. It has no gameplay vocabulary (no item, skill, status, job...).
#include <compare>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "rathena/persist/row.hpp"

namespace rathena::persist {

/// Storage scopes. GlobalAccount is the login-wide scope (legacy `##`), Account the per-account
/// scope (legacy `#`); the sigil-to-scope mapping itself lives in the `legacy` mod, not here.
enum class OwnerScope : std::uint8_t { Character = 0, Account = 1, GlobalAccount = 2, World = 3 };

struct OwnerRef {
	OwnerScope scope{OwnerScope::World};
	std::uint64_t id{0}; ///< 0 for GlobalAccount and World
	friend auto operator<=>(const OwnerRef&, const OwnerRef&) = default;
};

/// Typed KV value: int64, double, string, bool or nested table.
struct ValueTable;
struct Value {
	std::variant<std::int64_t, double, std::string, bool, std::shared_ptr<const ValueTable>> data{std::int64_t{0}};
	Value() = default;
	Value(std::int64_t v) : data(v) {}
	Value(double v) : data(v) {}
	Value(std::string v) : data(std::move(v)) {}
	Value(const char* v) : data(std::string(v)) {}
	Value(bool v) : data(v) {}
	Value(std::shared_ptr<const ValueTable> v) : data(std::move(v)) {}
};
struct ValueTable {
	std::vector<std::pair<std::string, Value>> entries;
};

/// True for int64 0, double 0.0, bool false and "" (the legacy registry "empty value" set).
bool is_zero_or_empty(const Value& v) noexcept;
/// Approximate serialized size, used for per-mod quota accounting.
std::size_t encoded_size(const Value& v) noexcept;

/// Generic record: opaque payload, schema-versioned, optimistic revision counter.
struct RecordKey {
	OwnerRef owner;
	std::string kind; ///< namespaced, e.g. "mymod:inventory"
	friend auto operator<=>(const RecordKey&, const RecordKey&) = default;
};
struct Record {
	RecordKey key;
	std::uint32_t schema_version{1};
	Blob payload;
	std::uint64_t revision{0};
};

struct KvKey {
	OwnerRef owner;
	std::string mod_id; ///< namespacing is automatic: one mod cannot address another's rows
	std::string key;
	/// Optional textual owner id (script-host owners are strings such as "77" or "world"). When
	/// non-empty it replaces the decimal form of owner.id as the stored owner reference.
	std::string owner_text{};
	friend auto operator<=>(const KvKey&, const KvKey&) = default;
};
/// The owner reference as stored in the database (decimal id unless owner_text is set).
std::string owner_ref_text(const KvKey& k);

struct KvEntry {
	KvKey key;
	Value value;
	std::uint32_t row_version{1}; ///< versioned rows, see KvMigrationRegistry
	std::uint64_t revision{0};
	bool erased{false};           ///< delete marker: the row is removed when the batch is applied
	/// Pre-encoded stored bytes (codec byte + body) used instead of `value` when set; the
	/// script-host adapter stores its own value model this way.
	std::optional<Blob> encoded{};
	std::uint64_t cost{0};        ///< quota cost; 0 = key bytes + encoded_size(value)
};

/// Immutable snapshot of dirty state handed to persistence workers at the tick boundary.
/// Built copy-on-write: the sim thread swaps a pointer, it does not copy per character.
struct DirtyBatch {
	std::uint64_t epoch{0};
	std::vector<Record> records;
	std::vector<KvEntry> kv;
	/// Keeps the tracker's retired index buffer alive so that it is freed on the thread that drops
	/// the batch (the writer), not on the producer at hand-off time.
	std::shared_ptr<void> retired{};
	[[nodiscard]] bool empty() const noexcept { return records.empty() && kv.empty(); }
};
using DirtyBatchPtr = std::shared_ptr<const DirtyBatch>;

/// A row that failed validation on read and was moved out of the live tables.
struct QuarantinedRow {
	std::uint64_t quarantine_id{0};
	std::string table;
	std::string row_ref; ///< key text such as "kv/<mod>/<key>"; never the payload
	std::string owner;   ///< stored owner reference of the row ("" when not owner-scoped)
	std::string reason;
	Blob raw;
};

/// id_map entry: client-visible number <-> namespaced string id. Saves store the string only.
enum class IdState : std::uint8_t { Active, Tombstoned };
struct IdMapEntry {
	std::string kind;      ///< "skill", "status", "item", ...  (opaque to persistence)
	std::string namespaced; ///< "mod:kind/name"
	std::uint32_t client_id{0};
	IdState state{IdState::Active};
};

} // namespace rathena::persist
