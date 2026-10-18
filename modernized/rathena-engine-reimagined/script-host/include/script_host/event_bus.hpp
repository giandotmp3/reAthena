#pragma once
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "script_host/budget.hpp"
#include "script_host/hook.hpp"
#include "script_host/not_implemented.hpp"

namespace script_host {

struct HookError {
  std::string hook;      // qualified id
  std::string kind;      // "error" | "budget" | "memory" | "permission"
  std::string message;
};

struct DispatchResult {
  bool cancelled = false;
  std::string cancel_reason;
  std::string cancelled_by;              // qualified id of the cancelling hook
  std::vector<std::string> ran;          // qualified hook ids in execution order
  std::vector<Effect> committed_effects; // empty when cancelled
  std::vector<HookError> errors;         // failed hooks (the dispatch continues)
  std::vector<std::string> diagnostics;  // ignored cancels, observe mutations, ...
  std::vector<std::string> auto_disabled;// hooks disabled by strikes during this dispatch
  std::string replaced_by;               // winning Replace hook (caller skips its default stage)
};

class EventBus {
 public:
  explicit EventBus(BudgetLimits strike_limits = {});

  // Registers a hook. Does not validate ordering until validate().
  void add(HookSpec spec, HookFn fn);

  // Load-time conflict detection, all reported as LoadError naming the hooks:
  //  - a before/after constraint that contradicts (phase, priority) order,
  //  - constraint cycles among hooks of equal (phase, priority),
  //  - duplicate qualified hook ids on one event,
  //  - two or more Replace hooks on one (event, phase) unless exactly one
  //    declares `precedence`,
  //  - Replace on a protected event without permit_protected,
  //  - draws_main_rng hooks while strict_parity is on.
  // Constraints naming a hook that is not registered are ignored (optional
  // ordering against a mod that may be absent).
  void validate() const;

  // Resolved order of enabled hooks: phase (pre, on, post), then priority, then
  // topological constraints, then load order, then registration sequence.
  std::vector<std::string> order(const std::string& event) const;

  // Runs pre/on/post. cancel() in Pre discards the effects queued during this
  // dispatch and skips every later hook. cancel() outside Pre, or from a
  // protected-stage hook without permission, is ignored with a diagnostic.
  // A failing hook (exception) is rolled back (its effects, its ctx changes),
  // recorded, and the dispatch continues; budget failures add a strike.
  DispatchResult dispatch(const std::string& event, HookContext& ctx);

  void mark_protected(const std::string& event);
  bool is_protected(const std::string& event) const { return protected_.count(event) != 0; }
  void disable_hook(const std::string& qualified_id);  // operator / strike disable
  void enable_hook(const std::string& qualified_id);
  bool is_disabled(const std::string& qualified_id) const { return disabled_.count(qualified_id) != 0; }

  // Hot reload support: hooks are owned by (mod, generation).
  std::size_t remove_generation(const std::string& mod, std::uint64_t generation);
  std::size_t count(const std::string& mod, std::uint64_t generation) const;
  std::size_t size() const { return entries_.size(); }

  void set_tick(std::uint64_t t) { tick_ = t; }
  void set_strict_parity(bool v) { strict_parity_ = v; cache_.clear(); }
  void set_mod_load_order(const std::string& mod, int order) { load_order_[mod] = order; cache_.clear(); }

 private:
  struct Entry { HookSpec spec; HookFn fn; std::uint64_t seq; };
  using EntryPtr = std::shared_ptr<const Entry>;
  std::vector<EntryPtr> plan(const std::string& event) const;

  std::vector<EntryPtr> entries_;  // immutable entries, shared between copies of the bus
  std::set<std::string> disabled_, protected_;
  std::map<std::string, int> load_order_;
  std::uint64_t next_seq_ = 0, tick_ = 0;
  bool strict_parity_ = false;
  BudgetMeter strikes_;
  mutable std::map<std::string, std::vector<EntryPtr>> cache_;
};

}  // namespace script_host
