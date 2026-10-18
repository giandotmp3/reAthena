#pragma once
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "mini_test.hpp"
#include "script_host/event_bus.hpp"

namespace fx {
using namespace script_host;

inline std::string slurp(const std::string& path) {
  std::ifstream f(path);
  if (!f) throw mt::Failure("cannot open " + path);
  std::ostringstream o;
  o << f.rdbuf();
  return o.str();
}

inline HookSpec spec(const std::string& mod, const std::string& id, int prio = 0,
                     Phase ph = Phase::On, Mode m = Mode::Observe,
                     const std::string& ev = "battle:stage") {
  HookSpec s;
  s.mod = mod; s.id = id; s.priority = prio; s.phase = ph; s.mode = m; s.event = ev;
  return s;
}

// Adds a hook that appends its qualified id to `log`.
inline void add_logging(EventBus& bus, HookSpec s, std::vector<std::string>& log) {
  auto q = s.qualified();
  bus.add(std::move(s), [&log, q](HookContext&) { log.push_back(q); });
}
}  // namespace fx

#include "script_host/reload.hpp"
#include "script_host/sandbox.hpp"

namespace fx {
// Loads `source` as mod `mod` through a ReloadManager; fails the test if it does not load.
inline ReloadResult load(ReloadManager& rm, const std::string& mod, const std::string& source) {
  auto r = rm.reload(mod, source);
  if (!r.ok) throw mt::Failure("mod " + mod + " failed to load: " + r.error);
  return r;
}
inline ModEnvironmentSpec env(const std::string& mod, std::uint64_t cap = 8ull << 20) {
  ModEnvironmentSpec s;
  s.mod_id = mod;
  s.memory_cap_bytes = cap;
  // Wall-clock limits are off so slow CI machines cannot make step/memory tests
  // flaky; tests about the wall clock set them explicitly.
  s.budget.wall_us_per_dispatch = 0;
  s.budget.wall_us_per_tick = 0;
  return s;
}
inline bool contains(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }
inline std::string probe(const std::string& name) { return slurp(std::string(PROBE_DIR) + "/" + name); }
}  // namespace fx
