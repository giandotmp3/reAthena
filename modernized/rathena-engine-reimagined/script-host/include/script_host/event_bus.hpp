#pragma once
#include <map>
#include <string>
#include <vector>

#include "script_host/hook.hpp"
#include "script_host/not_implemented.hpp"

namespace script_host {

struct DispatchResult {
  bool cancelled = false;
  std::string cancel_reason;
  std::vector<std::string> ran;          // qualified hook ids in execution order
  std::vector<Effect> committed_effects; // empty when cancelled
};

class EventBus {
 public:
  // Registers a hook. Does not validate ordering until validate().
  void add(HookSpec spec, HookFn fn);

  // Load-time conflict detection: priority vs before/after contradiction,
  // cycles, and two Replace hooks on one event without declared precedence.
  // Throws LoadError naming both hooks.
  void validate() const;

  // Resolved order: priority, then topological constraints, then load order,
  // then registration sequence.
  std::vector<std::string> order(const std::string& event) const;

  // Runs pre/on/post. cancel() in Pre discards queued effects and skips On/Post.
  // Stages marked protected cannot be cancelled without permission.
  DispatchResult dispatch(const std::string& event, HookContext& ctx);

  void mark_protected(const std::string& event);
  void disable_hook(const std::string& qualified_id);  // operator / strike disable

 private:
  struct Entry { HookSpec spec; HookFn fn; std::uint64_t seq; };
  std::vector<Entry> entries_;
};

}  // namespace script_host
