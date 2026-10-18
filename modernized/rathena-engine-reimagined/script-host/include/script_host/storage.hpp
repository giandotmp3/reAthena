#pragma once
// storage.character(ch):mod("id"), storage.account(...), storage.world()
//
// Namespaced per-mod KV on three scopes. Typed values (int64, double, string,
// bool, nested table), versioned rows, per-mod quota, migrate(from_version)
// hook contract. The backend is an interface; the in-memory backend ships here
// and the persistence unit plugs in a durable one.
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <stdexcept>
#include <variant>
#include <vector>

namespace script_host {

struct ValueTable;
using Value = std::variant<std::monostate, std::int64_t, double, bool, std::string, std::shared_ptr<ValueTable>>;
struct ValueTable {
  std::map<std::string, Value> entries;      // string keys
  std::map<std::int64_t, Value> array;       // integer keys (Lua array part)
};

enum class Scope { Character, Account, World };

struct StorageError : std::runtime_error {
  enum class Code { QuotaExceeded, PermissionDenied, NoMigrationPath, Corrupt, Invalid } code;
  StorageError(Code c, const std::string& m) : std::runtime_error(m), code(c) {}
};

struct Row {
  std::uint32_t version = 1;
  Value value;
};

// Deep copy (tables are shared_ptr; stored values must not alias caller values).
Value deep_copy(const Value& v);
// Bytes charged against the quota; throws StorageError(Invalid) past kMaxDepth.
constexpr int kMaxValueDepth = 16;
std::uint64_t value_size(const Value& v);
bool value_equal(const Value& a, const Value& b);

// One migration step: given the row at `from_version`, return the row at a
// higher version. The host chains steps until the declared current version is
// reached; a step that does not advance the version means "no path".
using MigrateFn = std::function<Row(std::uint32_t from_version, const Row& row)>;

struct StorageKey {
  Scope scope;
  std::string owner;  // character id / account id / "world"
  std::string mod;
  std::string key;
  auto operator<=>(const StorageKey&) const = default;
};

// Backend seam for the persistence unit. All calls are synchronous.
class StorageBackend {
 public:
  virtual ~StorageBackend() = default;
  virtual std::optional<Row> load(const StorageKey& k) const = 0;
  // Stores (inserts or replaces) the row.
  virtual void store(const StorageKey& k, const Row& r) = 0;
  virtual bool erase(const StorageKey& k) = 0;
  // Total encoded bytes currently held for `mod` (quota basis).
  virtual std::uint64_t usage(const std::string& mod) const = 0;
  // All keys of (scope, owner, mod), sorted.
  virtual std::vector<std::string> keys(Scope s, const std::string& owner, const std::string& mod) const = 0;
};

class MemoryStorageBackend final : public StorageBackend {
 public:
  std::optional<Row> load(const StorageKey& k) const override;
  void store(const StorageKey& k, const Row& r) override;
  bool erase(const StorageKey& k) override;
  std::uint64_t usage(const std::string& mod) const override;
  std::vector<std::string> keys(Scope s, const std::string& owner, const std::string& mod) const override;

 private:
  static std::uint64_t cost(const StorageKey& k, const Row& r);
  std::map<StorageKey, Row> rows_;
  std::map<std::string, std::uint64_t> usage_;
};

class StorageApi {
 public:
  static constexpr std::uint64_t kDefaultQuotaBytes = 1u << 20;  // storage.default_quota_bytes

  explicit StorageApi(std::shared_ptr<StorageBackend> backend = nullptr);

  // Keys are namespaced by mod id automatically; cross-mod access needs
  // the storage.read:<mod> permission (modelled by `reader_mod != owner_mod`
  // and grant_read()). Rows below the mod's current version are migrated on
  // read and written back. Throws StorageError (PermissionDenied, NoMigrationPath, Corrupt).
  std::optional<Value> get(Scope s, const std::string& owner, const std::string& owner_mod,
                           const std::string& reader_mod, const std::string& key);
  std::optional<Row> get_row(Scope s, const std::string& owner, const std::string& mod, const std::string& key);
  // Writes at the mod's current version. Throws QuotaExceeded when the mod's
  // total would pass its quota (the write is not applied).
  void set(Scope s, const std::string& owner, const std::string& mod, const std::string& key, Value v);
  bool erase(Scope s, const std::string& owner, const std::string& mod, const std::string& key);
  std::vector<std::string> keys(Scope s, const std::string& owner, const std::string& mod) const;

  void set_quota(const std::string& mod, std::uint64_t bytes);
  std::uint64_t quota(const std::string& mod) const;
  std::uint64_t usage(const std::string& mod) const;

  // Declares the mod's current storage version and its migration step function.
  void declare_migrate(const std::string& mod, std::uint32_t current_version, MigrateFn fn);
  std::uint32_t current_version(const std::string& mod) const;
  // Loader check: refuses a mod whose stored version has no migration path.
  // stored == current is fine; stored > current (downgrade) is refused; for
  // stored < current the migrate function is dry-run on an empty row and must
  // advance the version strictly up to `current` (so migrate functions must be
  // total on empty rows). Throws StorageError(NoMigrationPath).
  void check_loadable(const std::string& mod, std::uint32_t stored_version) const;
  void grant_read(const std::string& reader_mod, const std::string& owner_mod);

  StorageBackend& backend() { return *backend_; }

 private:
  Row migrate_row(const std::string& mod, Row row) const;
  std::shared_ptr<StorageBackend> backend_;
  std::map<std::string, std::uint64_t> quota_;
  struct Migration { std::uint32_t current = 1; MigrateFn fn; };
  std::map<std::string, Migration> migrations_;
  std::set<std::pair<std::string, std::string>> grants_;  // (reader, owner)
};

}  // namespace script_host
