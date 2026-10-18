#pragma once
// Integration of the mod loader with script-host: every mod gets its own
// ModSandbox (one lua_State, own memory cap, own granted permissions) built
// through ReloadManager; the operator's grants in mods.lock become the
// sandbox's `granted` set, so an ungranted API is refused at runtime by the
// sandbox with a precise typed error.
//
// Generation model. ModLoader::load builds a complete new "world" (a fresh
// ReloadManager plus StorageApi) next to the live one and swaps it in only
// after every mod loaded, which is what makes a failed reload leave the old
// generation untouched (script-host has no unload and no cross-mod two-phase
// commit). Per-mod hot reload of a running world is HostRuntime::reload_mod.
#include <chrono>
#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "mods/loader.hpp"
#include "script_host/event_bus.hpp"
#include "script_host/numeric.hpp"
#include "script_host/reload.hpp"
#include "script_host/sandbox.hpp"
#include "script_host/storage.hpp"

namespace mods {

struct BundleOptions {
  // Directories (relative to the package root) whose *.luau files are modules.
  std::vector<std::string> module_dirs{"scripts", "data", "luau"};
  // Directory names never bundled (tests, tooling, generated output).
  std::set<std::string> exclude_dirs{"tests", "test", "build", "conformance", "tools", "types"};
  // Entry modules (path under the root, with or without .luau). Empty: scripts/init
  // when it exists, otherwise every module in path order.
  std::vector<std::string> entry;
};

// One Luau chunk for a package. Modules are functions behind `require` and
// `import` shims (the sandbox has neither): names resolve package-relative
// ("scripts/domain", "data/items"), file-relative ("./x", "../y") or by a
// unique file name. After the modules are defined the entry modules are
// required, and an entry that returns a table with `register` has
// register(engine) called when the host defines a global `engine` table.
std::string bundle_package(const std::filesystem::path& root, const BundleOptions& opt = {});

struct HostConfig {
  std::shared_ptr<script_host::StorageBackend> storage_backend;  // default: in-memory
  std::uint64_t default_memory_cap = 64ull << 20;
  std::map<std::string, std::uint64_t> memory_cap;               // per mod override
  script_host::BudgetLimits budget;                              // default budgets
  std::map<std::string, script_host::BudgetLimits> budget_for;   // per mod override
  std::map<std::string, std::uint64_t> storage_quota;
  std::map<std::string, BundleOptions> bundle_for;               // per mod override
  script_host::RngStreams* rng = nullptr;                        // must outlive the runtime
  HostConfig() {
    // The package chunk is loaded under steps_per_dispatch: large packages need room.
    budget.steps_per_dispatch = 50'000'000;
    budget.steps_per_slice = 50'000'000;
    budget.wall_us_per_dispatch = 0;  // wall clock off by default; operators set it
    budget.wall_us_per_tick = 0;
  }
};

script_host::Permission to_host_permission(PermissionKind k);

class HostRuntime final : public ModActivator {
public:
  explicit HostRuntime(HostConfig cfg = {});
  ~HostRuntime() override;

  static constexpr bool vm_available() { return script_host::ModSandbox::kLuauAvailable; }

  // ModActivator
  std::unique_ptr<ActivationSession> begin() override;
  std::string deactivate(const std::string& mod_id) override;
  void reactivate(const std::string& mod_id) override;
  ActivatorStats stats(const std::string& mod_id) const override;

  // Live world (null/empty before the first successful load).
  bool live() const { return live_ != nullptr; }
  script_host::ReloadManager& reload_manager();
  script_host::StorageApi& storage();
  std::shared_ptr<script_host::ModSandbox> sandbox(const std::string& mod_id) const;
  // Runs a chunk inside the mod's own sandbox (its granted set applies).
  script_host::RunResult run(const std::string& mod_id, const std::string& chunk_name, const std::string& source);
  // Hot reload of one mod in the live world (script-host generation semantics).
  script_host::ReloadResult reload_mod(const std::string& mod_id, const std::string& source);
  const std::string& chunk_of(const std::string& mod_id) const;
  std::set<script_host::Permission> granted_of(const std::string& mod_id) const;
  // Dispatches an event on the live bus and records per-mod hook counters.
  // Time is measured around the whole dispatch and divided between the mods
  // whose hooks ran in proportion to how many hooks each ran (script-host
  // exposes no per-hook timer); a dispatch where one mod ran is exact.
  script_host::DispatchResult dispatch(const std::string& event, script_host::HookContext& ctx);

private:
  struct World;
  friend class HostSession;
  HostConfig cfg_;
  std::unique_ptr<World> live_;
  std::map<std::string, ActivatorStats> hook_stats_;  // lifetime counters
};

}  // namespace mods
