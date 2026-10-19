#include "mods/host_runtime.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>

#include "mods/common.hpp"

namespace mods {
namespace fs = std::filesystem;
using script_host::DispatchResult;
using script_host::HookContext;
using script_host::MemoryStorageBackend;
using script_host::ModEnvironmentSpec;
using script_host::ModSandbox;
using script_host::ReloadManager;
using script_host::ReloadResult;
using script_host::RunResult;
using script_host::RunStatus;
using script_host::StorageApi;
using HostPermission = script_host::Permission;

namespace {

std::string slurp(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  if (!in) throw LoadError("package.io", "cannot read " + p.string());
  std::ostringstream o;
  o << in.rdbuf();
  return o.str();
}

std::string lua_quote(const std::string& s) {
  std::string o = "\"";
  for (unsigned char c : s) {
    if (c == '"' || c == '\\') { o += '\\'; o += static_cast<char>(c); }
    else if (c < 0x20 || c == 0x7F) { char b[8]; std::snprintf(b, sizeof b, "\\%03d", c); o += b; }
    else o += static_cast<char>(c);
  }
  return o + "\"";
}

const char* kPrelude = R"LUA(--!nonstrict
local __defs, __loaded, __stems = {}, {}, {}
local function __norm(p)
  local out = {}
  for seg in string.gmatch(p, "[^/]+") do
    if seg == ".." then table.remove(out) elseif seg ~= "." then table.insert(out, seg) end
  end
  return table.concat(out, "/")
end
local function __find(path, from)
  path = string.gsub(path, "%.luau$", "")
  if string.sub(path, 1, 1) == "." then
    local n = __norm(from .. "/" .. path)
    if __defs[n] then return n end
  else
    local n = __norm(path)
    if __defs[n] then return n end
    n = __norm(from .. "/" .. path)
    if __defs[n] then return n end
  end
  local stem = string.match(path, "([^/]+)$")
  local list = stem and __stems[stem]
  if list and #list == 1 then return list[1] end
  return nil
end
local __load
local function __mk(dir)
  return function(path)
    local n = __find(path, dir)
    if not n then error("module not found: " .. tostring(path) .. " (from '" .. dir .. "')", 2) end
    return __load(n)
  end
end
function __load(name)
  local m = __loaded[name]
  if m ~= nil then
    if m == false then error("cyclic require of " .. name, 2) end
    return m
  end
  __loaded[name] = false
  local dir = string.match(name, "^(.*)/[^/]*$") or ""
  local f = __mk(dir)
  m = __defs[name](f, f)
  if m == nil then m = true end
  __loaded[name] = m
  return m
end
)LUA";

}  // namespace

std::string bundle_package(const fs::path& root, const BundleOptions& opt) {
  std::vector<std::pair<std::string, fs::path>> mods;  // module name -> file
  for (const auto& dir : opt.module_dirs) {
    fs::path d = root / dir;
    if (!fs::is_directory(d)) continue;
    for (auto it = fs::recursive_directory_iterator(d); it != fs::recursive_directory_iterator(); ++it) {
      if (it->is_directory()) {
        if (opt.exclude_dirs.count(it->path().filename().string())) it.disable_recursion_pending();
        continue;
      }
      if (!it->is_regular_file() || it->path().extension() != ".luau") continue;
      std::string fn = it->path().filename().string();
      if (fn.size() > 7 && fn.compare(fn.size() - 7, 7, ".d.luau") == 0) continue;
      std::string rel = fs::relative(it->path(), root).generic_string();
      mods.emplace_back(rel.substr(0, rel.size() - 5), it->path());
    }
  }
  std::sort(mods.begin(), mods.end());
  std::string out = kPrelude;
  std::map<std::string, std::vector<std::string>> stems;
  for (const auto& [name, file] : mods) {
    (void)file;
    auto slash = name.rfind('/');
    stems[slash == std::string::npos ? name : name.substr(slash + 1)].push_back(name);
  }
  for (const auto& [name, file] : mods) {
    out += "__defs[" + lua_quote(name) + "] = function(require, import)\n";
    out += slurp(file);
    out += "\nend\n";
  }
  for (const auto& [stem, list] : stems) {
    out += "__stems[" + lua_quote(stem) + "] = {";
    for (const auto& n : list) out += lua_quote(n) + ",";
    out += "}\n";
  }
  std::vector<std::string> entries;
  for (auto e : opt.entry) {
    if (e.size() > 5 && e.compare(e.size() - 5, 5, ".luau") == 0) e.resize(e.size() - 5);
    entries.push_back(e);
  }
  if (entries.empty()) {
    bool has_init = std::any_of(mods.begin(), mods.end(), [](const auto& m) { return m.first == "scripts/init"; });
    if (has_init) entries.push_back("scripts/init");
    else for (const auto& m : mods) entries.push_back(m.first);
  }
  for (const auto& e : entries)
    if (std::none_of(mods.begin(), mods.end(), [&](const auto& m) { return m.first == e; }))
      throw LoadError("package.entry", "entry module '" + e + "' does not exist under " + root.string());
  out += "local __entries = {";
  for (const auto& e : entries) out += lua_quote(e) + ",";
  out += "}\nfor _, n in ipairs(__entries) do\n  local m = __load(n)\n";
  // Opt-in (manifest `[entry] register = true`): without it a module's own `register` is never called by the host
  if (opt.register_entries)
    out += "  if type(m) == 'table' and type(m.register) == 'function' and type(engine) == 'table' then m.register(engine) end\n";
  out += "end\n";
  return out;
}

HostPermission to_host_permission(PermissionKind k) {
  switch (k) {
    case PermissionKind::StorageAccount: return HostPermission::StorageAccount;
    case PermissionKind::StorageRead: return HostPermission::StorageReadOther;
    case PermissionKind::HttpRoute: return HostPermission::HttpRoute;
    case PermissionKind::UnboundedLoop: return HostPermission::UnboundedLoop;
    case PermissionKind::AtCommand: return HostPermission::Atcommand;
    case PermissionKind::NetRaw: return HostPermission::NetRaw;
    case PermissionKind::ProtectedStages: return HostPermission::ProtectedStages;
  }
  return HostPermission::NetRaw;
}

static void fill_config(ModEnvironmentSpec& spec, const Manifest& m) {
  if (!m.ruleset) return;
  const RulesetMeta& r = *m.ruleset;
  spec.config["mode"] = r.mode;
  if (!r.db_set.empty()) spec.config["db_set"] = r.db_set;
  spec.config["renewal"] = r.renewal();
  if (r.legacy_quirks) spec.config["legacy_quirks"] = *r.legacy_quirks;
  if (r.strict_parity) spec.config["strict_parity"] = *r.strict_parity;
  for (const auto& [name, on] : r.flags) spec.config["subflag_" + name] = on;
}

// ---- world ----------------------------------------------------------------------
struct HostRuntime::World {
  std::unique_ptr<StorageApi> storage;
  std::unique_ptr<ReloadManager> rm;
  std::map<std::string, std::string> chunk;              // mod -> bundled source
  std::map<std::string, std::set<HostPermission>> granted;   // mod -> host permissions
  std::vector<std::string> order;
};

class HostSession final : public ActivationSession {
public:
  HostSession(HostRuntime& rt) : rt_(rt), w_(std::make_unique<HostRuntime::World>()) {
    w_->storage = std::make_unique<StorageApi>(rt.cfg_.storage_backend);
    w_->rm = std::make_unique<ReloadManager>();
    w_->rm->set_storage(w_->storage.get());
    w_->rm->set_rng(rt.cfg_.rng);
  }

  void activate(const ActivationInfo& info) override {
    const std::string& id = info.manifest->id;
    prepare(info);
    const std::string& chunk = w_->chunk[id];
    ReloadResult r = w_->rm->reload(id, chunk.empty() ? std::string("return nil\n") : chunk);
    if (!r.ok) throw LoadError("activate.failed", "mod '" + id + "' failed to load: " + r.error);
  }

  void skip(const ActivationInfo& info) override { prepare(info); }

  void commit() override { rt_.live_ = std::move(w_); }

private:
  void prepare(const ActivationInfo& info) {
    const std::string& id = info.manifest->id;
    if (!HostRuntime::vm_available())
      throw LoadError("host.no_vm", "mod '" + id + "': this build has no Luau VM (set RATHENA_LUAU_ROOT / RATHENA_LUAU_LIB_DIR)");
    ModEnvironmentSpec spec;
    spec.mod_id = id;
    for (const auto& p : info.granted) {
      spec.granted.insert(to_host_permission(p.kind));
      if (p.kind == PermissionKind::StorageRead) w_->storage->grant_read(id, p.arg);
    }
    auto cap = rt_.cfg_.memory_cap.find(id);
    spec.memory_cap_bytes = cap != rt_.cfg_.memory_cap.end() ? cap->second : rt_.cfg_.default_memory_cap;
    auto bl = rt_.cfg_.budget_for.find(id);
    spec.budget = bl != rt_.cfg_.budget_for.end() ? bl->second : rt_.cfg_.budget;
    if (auto q = rt_.cfg_.storage_quota.find(id); q != rt_.cfg_.storage_quota.end()) w_->storage->set_quota(id, q->second);
    fill_config(spec, *info.manifest);
    const BundleOptions* override_opts = nullptr;
    if (auto b = rt_.cfg_.bundle_for.find(id); b != rt_.cfg_.bundle_for.end()) override_opts = &b->second;
    // the `engine` global exists exactly when the bundler will call register(engine) on the entry modules
    spec.expose_engine = override_opts ? override_opts->register_entries : info.manifest->entry_register;
    w_->rm->set_environment(spec);
    w_->rm->bus().set_mod_load_order(id, static_cast<int>(w_->order.size()));
    w_->order.push_back(id);
    w_->granted[id] = spec.granted;

    std::string chunk;
    if (!info.package_dir.empty()) {
      BundleOptions bo;
      if (override_opts) bo = *override_opts;
      else { bo.entry = info.manifest->entry; bo.register_entries = info.manifest->entry_register; }
      chunk = bundle_package(info.package_dir, bo);
    }
    w_->chunk[id] = chunk;
  }

  HostRuntime& rt_;
  std::unique_ptr<HostRuntime::World> w_;
};

HostRuntime::HostRuntime(HostConfig cfg) : cfg_(std::move(cfg)) {
  if (!cfg_.storage_backend) cfg_.storage_backend = std::make_shared<MemoryStorageBackend>();
}
HostRuntime::~HostRuntime() = default;

std::unique_ptr<ActivationSession> HostRuntime::begin() { return std::make_unique<HostSession>(*this); }

ReloadManager& HostRuntime::reload_manager() {
  if (!live_) throw LoadError("host.not_loaded", "no generation has been loaded yet");
  return *live_->rm;
}
StorageApi& HostRuntime::storage() {
  if (!live_) throw LoadError("host.not_loaded", "no generation has been loaded yet");
  return *live_->storage;
}
std::shared_ptr<ModSandbox> HostRuntime::sandbox(const std::string& mod_id) const {
  return live_ ? live_->rm->active_sandbox(mod_id) : nullptr;
}
const std::string& HostRuntime::chunk_of(const std::string& mod_id) const {
  static const std::string empty;
  if (!live_) return empty;
  auto it = live_->chunk.find(mod_id);
  return it == live_->chunk.end() ? empty : it->second;
}
std::set<HostPermission> HostRuntime::granted_of(const std::string& mod_id) const {
  if (!live_) return {};
  auto it = live_->granted.find(mod_id);
  return it == live_->granted.end() ? std::set<HostPermission>{} : it->second;
}

RunResult HostRuntime::run(const std::string& mod_id, const std::string& chunk_name, const std::string& source) {
  auto sb = sandbox(mod_id);
  if (!sb) return {RunStatus::Error, "[" + mod_id + "] no active sandbox"};
  return sb->run(chunk_name, source);
}

ReloadResult HostRuntime::reload_mod(const std::string& mod_id, const std::string& source) {
  ReloadResult r = reload_manager().reload(mod_id, source);
  if (r.ok) live_->chunk[mod_id] = source;
  return r;
}

std::string HostRuntime::deactivate(const std::string& mod_id) {
  if (!live_) return {};
  ReloadManager& rm = *live_->rm;
  std::uint64_t gen = rm.active_generation(mod_id);
  if (gen == 0) return {};
  rm.bus().remove_generation(mod_id, gen);  // no hook of this mod runs from now on
  ReloadResult r = rm.reload(mod_id, "return nil\n");  // drops timers and frees the old state
  if (r.ok) return {};
  return "hooks removed; timers and state of '" + mod_id + "' stay until restart (" + r.error + ")";
}

void HostRuntime::reactivate(const std::string& mod_id) {
  if (!live_) return;
  auto it = live_->chunk.find(mod_id);
  if (it == live_->chunk.end()) return;
  ReloadResult r = live_->rm->reload(mod_id, it->second.empty() ? std::string("return nil\n") : it->second);
  if (!r.ok) throw LoadError("activate.failed", "mod '" + mod_id + "' failed to re-enable: " + r.error);
}

ActivatorStats HostRuntime::stats(const std::string& mod_id) const {
  ActivatorStats s;
  if (auto it = hook_stats_.find(mod_id); it != hook_stats_.end()) s = it->second;
  if (auto sb = sandbox(mod_id)) {
    s.memory_used = sb->memory_used();
    s.memory_peak = sb->memory_peak();
  }
  return s;
}

DispatchResult HostRuntime::dispatch(const std::string& event, HookContext& ctx) {
  using Clock = std::chrono::steady_clock;
  const auto t0 = Clock::now();
  DispatchResult res = reload_manager().bus().dispatch(event, ctx);
  const auto total = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - t0);
  std::map<std::string, std::uint64_t> ran;
  for (const auto& q : res.ran) ran[q.substr(0, q.find('/'))]++;
  std::uint64_t n = res.ran.size();
  for (const auto& [mod, c] : ran) {
    auto& st = hook_stats_[mod];
    st.hook_calls += c;
    if (n) st.hook_time += std::chrono::microseconds(total.count() * static_cast<long long>(c) / static_cast<long long>(n));
  }
  for (const auto& e : res.errors) hook_stats_[e.hook.substr(0, e.hook.find('/'))].hook_errors++;
  return res;
}

}  // namespace mods
