#pragma once
// One sandboxed Luau lua_State per mod (M0(c) decision), each with its own
// limiting allocator, interrupt-callback budget, read-only shared globals,
// and no os/io/debug/loadstring/getfenv/setfenv/require. table.sort and
// string.rep are replaced by bounded versions.
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "script_host/budget.hpp"
#include "script_host/dialogue.hpp"
#include "script_host/hook.hpp"
#include "script_host/storage.hpp"

namespace script_host {

class RngStreams;

enum class Permission { StorageAccount, StorageReadOther, HttpRoute, UnboundedLoop, Atcommand, NetRaw };

struct ModEnvironmentSpec {
  std::string mod_id;
  std::set<Permission> granted;
  std::uint64_t memory_cap_bytes = 32ull << 20;
  BudgetLimits budget{};                 // UnboundedLoop in `granted` lifts the step limit
  std::uint32_t max_sort_elements = 65536;  // bounded table.sort
};

enum class RunStatus { Ok, Error, BudgetExceeded, MemoryExceeded, PermissionDenied };

struct RunResult {
  RunStatus status = RunStatus::Ok;
  std::string error;  // includes mod id, hook id, trace
};

// Per-mod allocator accounting. Exceeding the cap disables the mod.
class MemoryAccountant {
 public:
  struct Slot {
    std::uint64_t cap = ~0ull, used = 0, peak = 0, failed_allocs = 0;
    bool disabled = false;
  };
  void set_cap(const std::string& mod, std::uint64_t bytes);
  // Returns false (and marks the mod disabled) if the charge would exceed the cap.
  bool charge(const std::string& mod, std::uint64_t bytes);
  void release(const std::string& mod, std::uint64_t bytes);
  std::uint64_t used(const std::string& mod) const;
  bool disabled(const std::string& mod) const;
  void reenable(const std::string& mod);
  // Stable pointer for allocators (hot path avoids the string lookup).
  Slot& slot(const std::string& mod) { return slots_[mod]; }

 private:
  std::unordered_map<std::string, Slot> slots_;
};

// A hook the mod chunk registered with events.on() while loading.
struct RegisteredHook {
  HookSpec spec;
  int ref = 0;  // Luau registry reference to the function
};
struct RegisteredMigrate {
  std::uint32_t current_version = 1;
  int ref = 0;
};

// Result of running or resuming a dialogue coroutine.
struct CoStep {
  enum class Kind { Yield, Done, Failed } kind = Kind::Failed;
  std::uint64_t id = 0;
  DialogueRequest request;
  RunResult result;
};

class ModSandbox {
 public:
  static constexpr bool kLuauAvailable =
#ifdef SCRIPT_HOST_WITH_LUAU
      true;
#else
      false;
#endif

  explicit ModSandbox(ModEnvironmentSpec spec, MemoryAccountant* shared = nullptr);
  ~ModSandbox();
  ModSandbox(const ModSandbox&) = delete;
  ModSandbox& operator=(const ModSandbox&) = delete;

  // Runs source in the mod's environment (globals persist between runs; the
  // shared libraries are read-only). events.on / storage.migrate /
  // definitions.declare are only valid while a run() is in progress.
  // The step/wall budget is armed per call; a budget trip is a typed
  // RunStatus::BudgetExceeded and the sandbox stays usable afterwards.
  // A memory-cap failure disables the mod until reenable().
  RunResult run(const std::string& chunk_name, const std::string& source);
  const ModEnvironmentSpec& spec() const { return spec_; }

  // ---- host wiring ------------------------------------------------------
  struct Host {
    std::function<void(std::uint64_t ms, int ref)> add_timer;  // timers.after
    std::function<void(const std::string&)> log;               // print()
  };
  void set_host(Host h) { host_ = std::move(h); }
  void set_storage(StorageApi* s) { storage_ = s; }
  void set_rng(RngStreams* r) { rng_ = r; }

  // Registrations collected by the last run().
  std::vector<RegisteredHook> take_hooks();
  std::vector<RegisteredMigrate> take_migrates();
  std::vector<std::string> take_shapes();  // definitions.declare() lines, in order

  // ---- invoking mod functions -------------------------------------------
  // Throws HookFailure (Kind::Budget / Memory / Permission / Error).
  void call_hook(int ref, HookContext& ctx);
  RunResult call_ref(int ref, const std::string& label);  // timers
  RunResult call_migrate(int ref, std::uint32_t from, const Row& in, Row& out);
  void release_ref(int ref);

  // ---- coroutine dialogue -----------------------------------------------
  CoStep co_start(const std::string& chunk_name, const std::string& source);
  CoStep co_resume(std::uint64_t id, const DialogueResponse& r);
  void co_cancel(std::uint64_t id);
  std::size_t co_count() const;

  // ---- introspection ----------------------------------------------------
  BudgetMeter& meter();
  std::uint64_t memory_used() const;
  std::uint64_t memory_peak() const;
  bool disabled() const;
  void reenable();
  void begin_tick();

  struct Impl;

 private:
  ModEnvironmentSpec spec_;
  Host host_;
  StorageApi* storage_ = nullptr;
  RngStreams* rng_ = nullptr;
  std::unique_ptr<Impl> impl_;
};

using ModState = ModSandbox;  // the architecture's name for the same object

}  // namespace script_host
