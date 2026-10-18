#pragma once
// storage.character(ch):mod("id"), storage.account(...), storage.world()
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <stdexcept>
#include <variant>

namespace script_host {

struct ValueTable;
using Value = std::variant<std::monostate, std::int64_t, double, bool, std::string, std::shared_ptr<ValueTable>>;
struct ValueTable { std::map<std::string, Value> entries; };

enum class Scope { Character, Account, World };

struct StorageError : std::runtime_error {
  enum class Code { QuotaExceeded, PermissionDenied, NoMigrationPath, Corrupt } code;
  StorageError(Code c, const std::string& m) : std::runtime_error(m), code(c) {}
};

struct Row {
  std::uint32_t version = 1;
  Value value;
};

using MigrateFn = std::function<Row(std::uint32_t from_version, const Row& row)>;

class StorageApi {
 public:
  // Keys are namespaced by mod id automatically; cross-mod access needs
  // the storage.read:<mod> permission (modelled by `reader_mod != owner_mod`).
  std::optional<Value> get(Scope s, const std::string& owner, const std::string& owner_mod,
                           const std::string& reader_mod, const std::string& key);
  void set(Scope s, const std::string& owner, const std::string& mod, const std::string& key, Value v);
  void set_quota(const std::string& mod, std::uint64_t bytes);
  std::uint64_t usage(const std::string& mod) const;
  void declare_migrate(const std::string& mod, std::uint32_t current_version, MigrateFn fn);
  // Loader check: refuses a mod whose stored version has no migration path.
  void check_loadable(const std::string& mod, std::uint32_t stored_version) const;
  void grant_read(const std::string& reader_mod, const std::string& owner_mod);
};

}  // namespace script_host
