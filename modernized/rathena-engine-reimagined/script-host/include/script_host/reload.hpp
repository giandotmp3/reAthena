#pragma once
// Generational hot reload (CAP-E03). Reload builds generation N+1 in a fresh
// sandboxed lua_State at a tick boundary, swaps the event-bus registrations
// atomically, and lets generation N's coroutines finish until a game-time
// timeout. Timers and registrations are owned by (mod, generation).
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "script_host/event_bus.hpp"
#include "script_host/sandbox.hpp"

namespace script_host {

struct ReloadResult {
  bool ok = false;
  std::uint64_t active_generation = 0;
  std::string error;                 // precise reason when !ok
  bool needs_restart = false;        // definition shape / fields schema changed
};

struct ReloadConfig {
  std::uint64_t drain_timeout_ms = 60000;  // [reload] drain_timeout_game_ms
};

class ReloadManager {
 public:
  explicit ReloadManager(ReloadConfig cfg = {});
  ~ReloadManager();
  ReloadManager(const ReloadManager&) = delete;
  ReloadManager& operator=(const ReloadManager&) = delete;

  // Environment (permissions, memory cap, budgets) for a mod's future generations.
  void set_environment(ModEnvironmentSpec spec);
  void set_storage(StorageApi* s) { storage_ = s; }
  void set_rng(RngStreams* r) { rng_ = r; }
  EventBus& bus() { return bus_; }
  const EventBus& bus() const { return bus_; }

  // Creates generation N+1 at a tick boundary. The new generation is loaded in
  // a fresh sandbox, validated against the bus, and committed atomically; on
  // any failure (syntax/runtime error, ordering conflict, shape change) the
  // previous generation stays active. A change to the declared definition
  // shape (definitions.declare) is refused with needs_restart.
  ReloadResult reload(const std::string& mod, const std::string& new_source);
  std::uint64_t active_generation(const std::string& mod) const;  // 0 = never loaded
  std::shared_ptr<ModSandbox> active_sandbox(const std::string& mod) const;

  // Everything owned by (mod, generation): bus hooks + timers + coroutine leases.
  std::size_t live_registrations(const std::string& mod, std::uint64_t generation) const;

  // Native timers owned by the mod's active generation (cancelled on reload).
  std::uint64_t schedule(const std::string& mod, std::uint64_t delay_ms, std::function<void()> fn);

  // Advance game time by `ms`: due timers fire; coroutines of retired
  // generations are cancelled once they exceed drain_timeout_ms.
  void advance_game_time(std::uint64_t ms);
  std::uint64_t game_time_ms() const { return now_ms_; }

  // Coroutine leases keep an old generation alive until its coroutine ends or
  // the drain timeout cancels it via on_cancel().
  struct Lease {
    std::shared_ptr<ModSandbox> sandbox;
    std::uint64_t generation = 0;
    std::uint64_t id = 0;
  };
  std::optional<Lease> acquire(const std::string& mod, std::function<void()> on_cancel);
  void release(const std::string& mod, const Lease& l);

 private:
  struct Generation;
  struct ModEntry;
  struct Timer { std::uint64_t id, due_ms; std::string mod; std::uint64_t gen; std::function<void()> fn; };

  ReloadConfig cfg_;
  EventBus bus_;
  StorageApi* storage_ = nullptr;
  RngStreams* rng_ = nullptr;
  std::uint64_t now_ms_ = 0, next_timer_ = 1, next_lease_ = 1;
  std::map<std::string, ModEnvironmentSpec> envs_;
  std::map<std::string, std::unique_ptr<ModEntry>> mods_;
  std::vector<Timer> timers_;
};

}  // namespace script_host
