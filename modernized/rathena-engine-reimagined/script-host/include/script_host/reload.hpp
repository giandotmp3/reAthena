#pragma once
#include <cstdint>
#include <string>

namespace script_host {

struct ReloadResult {
  bool ok = false;
  std::uint64_t active_generation = 0;
  std::string error;                 // precise reason when !ok
  bool needs_restart = false;        // definition shape / fields schema changed
};

class ReloadManager {
 public:
  // Creates generation N+1 at a tick boundary. On load failure the previous
  // generation stays active.
  ReloadResult reload(const std::string& mod, const std::string& new_source);
  std::uint64_t active_generation(const std::string& mod) const;
  // Timers/registrations are owned by (mod, generation); old ones are cancelled.
  std::size_t live_registrations(const std::string& mod, std::uint64_t generation) const;
  // Advance game time; coroutines in generation N are cancelled past the timeout.
  void advance_game_time(std::uint64_t ms);
};

}  // namespace script_host
