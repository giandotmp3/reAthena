#include "script_host/storage.hpp"

#include <cmath>

namespace script_host {

namespace {
std::uint64_t size_rec(const Value& v, int depth) {
  if (depth > kMaxValueDepth) throw StorageError(StorageError::Code::Invalid, "storage value nested deeper than 16 levels");
  struct V {
    int depth;
    std::uint64_t operator()(std::monostate) const { return 1; }
    std::uint64_t operator()(std::int64_t) const { return 9; }
    std::uint64_t operator()(double) const { return 9; }
    std::uint64_t operator()(bool) const { return 2; }
    std::uint64_t operator()(const std::string& s) const { return 5 + s.size(); }
    std::uint64_t operator()(const std::shared_ptr<ValueTable>& t) const {
      std::uint64_t n = 5;
      if (t)
        {
          for (auto& [k, e] : t->entries) n += 5 + k.size() + size_rec(e, depth + 1);
          for (auto& [k, e] : t->array) n += 13 + size_rec(e, depth + 1);
        }
      return n;
    }
  };
  return std::visit(V{depth}, v);
}
Value copy_rec(const Value& v, int depth) {
  if (depth > kMaxValueDepth) throw StorageError(StorageError::Code::Invalid, "storage value nested deeper than 16 levels");
  if (auto* t = std::get_if<std::shared_ptr<ValueTable>>(&v)) {
    auto n = std::make_shared<ValueTable>();
    if (*t)
    {
      for (auto& [k, e] : (*t)->entries) n->entries[k] = copy_rec(e, depth + 1);
      for (auto& [k, e] : (*t)->array) n->array[k] = copy_rec(e, depth + 1);
    }
    return Value{n};
  }
  return v;
}
}  // namespace

Value deep_copy(const Value& v) { return copy_rec(v, 0); }
std::uint64_t value_size(const Value& v) { return size_rec(v, 0); }

bool value_equal(const Value& a, const Value& b) {
  if (a.index() != b.index()) return false;
  if (auto* ta = std::get_if<std::shared_ptr<ValueTable>>(&a)) {
    auto& tb = std::get<std::shared_ptr<ValueTable>>(b);
    if (!*ta || !tb) return !*ta && !tb;
    if ((*ta)->entries.size() != tb->entries.size() || (*ta)->array.size() != tb->array.size()) return false;
    for (auto& [k, v] : (*ta)->entries) {
      auto it = tb->entries.find(k);
      if (it == tb->entries.end() || !value_equal(v, it->second)) return false;
    }
    for (auto& [k, v] : (*ta)->array) {
      auto it = tb->array.find(k);
      if (it == tb->array.end() || !value_equal(v, it->second)) return false;
    }
    return true;
  }
  return a == b;
}

// ---- in-memory backend -----------------------------------------------------
std::uint64_t MemoryStorageBackend::cost(const StorageKey& k, const Row& r) {
  return k.key.size() + k.owner.size() + 4 + value_size(r.value);
}
std::optional<Row> MemoryStorageBackend::load(const StorageKey& k) const {
  auto it = rows_.find(k);
  if (it == rows_.end()) return std::nullopt;
  return Row{it->second.version, deep_copy(it->second.value)};
}
void MemoryStorageBackend::store(const StorageKey& k, const Row& r) {
  Row copy{r.version, deep_copy(r.value)};
  auto it = rows_.find(k);
  if (it != rows_.end()) {
    usage_[k.mod] -= cost(k, it->second);
    it->second = std::move(copy);
    usage_[k.mod] += cost(k, it->second);
  } else {
    usage_[k.mod] += cost(k, copy);
    rows_.emplace(k, std::move(copy));
  }
}
bool MemoryStorageBackend::erase(const StorageKey& k) {
  auto it = rows_.find(k);
  if (it == rows_.end()) return false;
  usage_[k.mod] -= cost(k, it->second);
  rows_.erase(it);
  return true;
}
std::uint64_t MemoryStorageBackend::usage(const std::string& mod) const {
  auto it = usage_.find(mod);
  return it == usage_.end() ? 0 : it->second;
}
std::vector<std::string> MemoryStorageBackend::keys(Scope s, const std::string& owner, const std::string& mod) const {
  std::vector<std::string> out;
  for (auto& [k, r] : rows_)
    if (k.scope == s && k.owner == owner && k.mod == mod) out.push_back(k.key);
  return out;
}

// ---- API -------------------------------------------------------------------
StorageApi::StorageApi(std::shared_ptr<StorageBackend> backend)
    : backend_(backend ? std::move(backend) : std::make_shared<MemoryStorageBackend>()) {}

void StorageApi::set_quota(const std::string& mod, std::uint64_t bytes) { quota_[mod] = bytes; }
std::uint64_t StorageApi::quota(const std::string& mod) const {
  auto it = quota_.find(mod);
  return it == quota_.end() ? kDefaultQuotaBytes : it->second;
}
std::uint64_t StorageApi::usage(const std::string& mod) const { return backend_->usage(mod); }

void StorageApi::declare_migrate(const std::string& mod, std::uint32_t current_version, MigrateFn fn) {
  migrations_[mod] = {current_version, std::move(fn)};
}
std::uint32_t StorageApi::current_version(const std::string& mod) const {
  auto it = migrations_.find(mod);
  return it == migrations_.end() ? 1 : it->second.current;
}

Row StorageApi::migrate_row(const std::string& mod, Row row) const {
  std::uint32_t cur = current_version(mod);
  auto it = migrations_.find(mod);
  while (row.version != cur) {
    if (row.version > cur || it == migrations_.end() || !it->second.fn)
      throw StorageError(StorageError::Code::NoMigrationPath,
                         "mod " + mod + ": stored version " + std::to_string(row.version) + " has no migration path to " + std::to_string(cur));
    std::uint32_t from = row.version;
    Row next = it->second.fn(from, row);
    if (next.version <= from || next.version > cur)
      throw StorageError(StorageError::Code::NoMigrationPath,
                         "mod " + mod + ": migrate(" + std::to_string(from) + ") returned version " + std::to_string(next.version) +
                             " (must advance, at most to " + std::to_string(cur) + ")");
    row = std::move(next);
  }
  return row;
}

void StorageApi::check_loadable(const std::string& mod, std::uint32_t stored_version) const {
  std::uint32_t cur = current_version(mod);
  if (stored_version == cur) return;
  Row probe{stored_version, Value{}};
  try {
    migrate_row(mod, probe);
  } catch (const StorageError&) {
    throw;
  } catch (const std::exception& e) {
    throw StorageError(StorageError::Code::NoMigrationPath,
                       "mod " + mod + ": migrate from version " + std::to_string(stored_version) + " failed on an empty row: " + e.what());
  }
}

void StorageApi::grant_read(const std::string& reader_mod, const std::string& owner_mod) {
  grants_.emplace(reader_mod, owner_mod);
}

std::optional<Row> StorageApi::get_row(Scope s, const std::string& owner, const std::string& mod, const std::string& key) {
  StorageKey k{s, owner, mod, key};
  auto row = backend_->load(k);
  if (!row) return std::nullopt;
  if (row->version != current_version(mod)) {
    try {
      *row = migrate_row(mod, std::move(*row));
    } catch (const StorageError&) {
      throw;
    } catch (const std::exception& e) {
      throw StorageError(StorageError::Code::Corrupt, "mod " + mod + ": migration of key '" + key + "' threw: " + e.what());
    }
    backend_->store(k, *row);  // write back migrated row
  }
  return row;
}

std::optional<Value> StorageApi::get(Scope s, const std::string& owner, const std::string& owner_mod,
                                     const std::string& reader_mod, const std::string& key) {
  if (reader_mod != owner_mod && !grants_.count({reader_mod, owner_mod}))
    throw StorageError(StorageError::Code::PermissionDenied,
                       "mod " + reader_mod + " lacks storage.read:" + owner_mod);
  auto row = get_row(s, owner, owner_mod, key);
  if (!row) return std::nullopt;
  return std::move(row->value);
}

void StorageApi::set(Scope s, const std::string& owner, const std::string& mod, const std::string& key, Value v) {
  if (key.empty() || key.size() > 256) throw StorageError(StorageError::Code::Invalid, "storage key must be 1..256 bytes");
  StorageKey k{s, owner, mod, key};
  Row row{current_version(mod), deep_copy(v)};
  std::uint64_t add = key.size() + owner.size() + 4 + value_size(row.value);
  std::uint64_t old = 0;
  if (auto prev = backend_->load(k)) old = key.size() + owner.size() + 4 + value_size(prev->value);
  std::uint64_t used = backend_->usage(mod);
  if (used - old + add > quota(mod))
    throw StorageError(StorageError::Code::QuotaExceeded,
                       "mod " + mod + ": storage quota " + std::to_string(quota(mod)) + " bytes exceeded (used " +
                           std::to_string(used) + ", write needs " + std::to_string(add) + ")");
  backend_->store(k, row);
}

bool StorageApi::erase(Scope s, const std::string& owner, const std::string& mod, const std::string& key) {
  return backend_->erase(StorageKey{s, owner, mod, key});
}
std::vector<std::string> StorageApi::keys(Scope s, const std::string& owner, const std::string& mod) const {
  return backend_->keys(s, owner, mod);
}

}  // namespace script_host
