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
