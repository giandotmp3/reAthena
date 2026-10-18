#pragma once
// Named stage pipelines (architecture 6.4). A defining mod (a ruleset) declares a pipeline's stage names with
// `pipeline.define(name, {stages})` and registers the BODY of each stage with `pipeline.stage(name, stage, fn, opts)`.
// Other mods edit the effective order through `insert_before / insert_after / replace / cancel / move`. The registry
// never changes the defined order: the effective order is computed from the definition plus the current edits (see
// plan building in pipeline.cpp), so removing a mod or a generation removes exactly its edits and nothing else.
//
// Precedence rules (all conflicts are load-time `LoadError`s naming both mods):
//   * two `replace` edits of one stage: error, unless exactly one declares `precedence` (the other is skipped, recorded);
//   * two `move` edits of one stage: same rule;
//   * `replace` + `cancel` of one stage: error;
//   * `replace`, `cancel` or `move` of a stage registered `protected` needs the protected-stage permission;
//   * inserted stage ids are `<mod id>:<name>` and unique per pipeline;
//   * an edit that names a stage that does not exist is an error unless it says `optional`;
//   * stages attached to the same anchor and side run in (priority, mod load order, registration) order.
//
// Running (`PipelineRegistry::run`, reached from Luau as `pipeline.run(name, ctx, opts)`): stages execute in effective
// order. A stage whose body lives in the defining mod's own state is called with `ctx` (any Luau value the ruleset
// chose). A stage whose body lives in another mod's state runs as a hook: it gets a HookContext whose int64 value is the
// run's "lane" (read before and written back after a modify/replace hook), an optional read-only `view` snapshot, the
// effect queue (transactional) and its own RNG stream. A failing hook is rolled back and recorded; the run goes on.
// `ctx:cancel(reason)` from such a hook aborts the run, discards the effects queued during it, and is refused with a
// diagnostic when a protected stage would be skipped and the hook has no permission.
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "script_host/budget.hpp"
#include "script_host/effect_queue.hpp"
#include "script_host/event_bus.hpp"
#include "script_host/hook.hpp"
#include "script_host/not_implemented.hpp"

namespace script_host {

// Where a stage body lives. `owner` is the identity of the owning sandbox, `ref` a Luau registry reference inside that
// sandbox (used when the body runs in its own state), `call` invokes it as a hook from any other state.
struct PipelineBody {
  std::shared_ptr<void> keep;  // keeps the owning sandbox alive
  const void* owner = nullptr;
  int ref = 0;
  HookFn call;
};
using PipelineBodyPtr = std::shared_ptr<const PipelineBody>;

enum class PipelineOpKind { Define, Stage, InsertBefore, InsertAfter, Replace, Cancel, Move };

// One call a mod chunk made while loading (recorded by the sandbox, applied to the registry by the reload manager).
struct PipelineOp {
  PipelineOpKind kind = PipelineOpKind::Define;
  std::string pipeline;
  std::vector<std::string> stages;  // Define: the ordered stage names
  std::string stage;                // Stage / Replace / Cancel / Move: the target; Insert*: the anchor
  std::string id;                   // Insert* / Replace: id of the new (or replacing) stage
  std::string anchor;               // Move: the anchor
  bool before = false;              // Move: place before (true) or after (false) the anchor
  int priority = 0;
  Mode mode = Mode::Observe;       // Insert*: observe (default) or modify; Replace is always a replace-mode hook
  bool precedence = false;
  bool draws_main_rng = false;
  bool optional = false;           // an edit naming a missing pipeline or stage is ignored instead of refused
  bool is_protected = false;       // Stage: callers need the permission to replace, cancel or move this stage
  bool permit_protected = false;   // the recording mod holds the protected-stage permission
  int ref = 0;                     // Luau registry reference of the body (Stage, Insert*, Replace)
};

struct PipelineEditNote {
  std::string op;      // insert_before | insert_after | replace | replace_skipped | cancel | move
  std::string mod;     // the mod that made the edit
  std::string detail;  // anchor, priority, winner ...
};

struct PipelineStepInfo {
  std::string id;           // stage name, or the inserted id (`mod:name`)
  std::string kind;         // "base" | "inserted"
  std::string source;       // mod that introduced the stage (the defining mod for base stages)
  std::string body_mod;     // mod whose code runs for this step ("" when it has no body)
  std::string body_id;      // hook id of that body ("" for the defining mod's own stage body)
  std::string mode;         // "own" | "observe" | "modify" | "replace" | ""
  bool active = true;       // false: cancelled (kept in the list so operators see it)
  bool has_body = false;
  bool protected_stage = false;
  int priority = 0;
  std::vector<PipelineEditNote> edits;  // every edit that touched this step, with its source mod
};

struct PipelineInfo {
  std::string name, owner_mod;
  std::uint64_t generation = 0;
  std::vector<PipelineStepInfo> steps;  // the effective order, cancelled steps included
  std::vector<std::string> ignored;     // optional edits that named something absent
};

struct PipelineOptions {
  bool strict_parity = false;                           // refuse stage hooks that declare draws_main_rng
  std::function<int(const std::string&)> load_order;    // mod -> load order (smaller first); default: registration order
};

// What the Luau side provides to a run. All callbacks run on the game thread inside the calling state.
class PipelineHost {
 public:
  virtual ~PipelineHost() = default;
  virtual bool skip(const std::string& stage_id) = 0;       // opts.skip[stage_id]
  virtual void run_own(const PipelineBody& body) = 0;       // body(ctx) in the calling state; a Luau error propagates
  virtual bool lane_get(std::int64_t& v) = 0;               // false: the run has no lane
  virtual void lane_set(std::int64_t v) = 0;
  virtual std::shared_ptr<const Value> view() = 0;          // snapshot of opts.view (null: none)
  virtual void missing_body(const std::string& stage_id) = 0;  // raises the NO_STAGE_BODY error (does not return)
};

struct PipelineRunResult {
  bool cancelled = false;
  std::string cancel_reason, cancelled_by;
  std::vector<std::string> ran;           // qualified ids of the hooks that ran in other states, in order
  std::vector<Effect> effects;            // committed; empty when cancelled
  std::vector<HookError> errors;
  std::vector<std::string> diagnostics;
  std::vector<std::string> auto_disabled;
  bool noteworthy() const { return cancelled || !ran.empty() || !errors.empty() || !diagnostics.empty() || !effects.empty(); }
};

class PipelineRegistry {
 public:
  explicit PipelineRegistry(BudgetLimits strike_limits = {});
  PipelineRegistry(const PipelineRegistry&) = default;
  PipelineRegistry& operator=(const PipelineRegistry&) = default;
  PipelineRegistry(PipelineRegistry&&) = default;
  PipelineRegistry& operator=(PipelineRegistry&&) = default;

  // Records one op for (mod, generation). `body` carries the owning sandbox (all ops) and the body (Stage, Insert*,
  // Replace). Argument-level errors (duplicate define, unknown stage in Stage, bad ids) are LoadErrors here;
  // cross-edit conflicts are found by validate().
  void apply(const PipelineOp& op, const std::string& mod, std::uint64_t generation, PipelineBodyPtr body);
  // Drops everything (definitions, bodies, edits) owned by (mod, generation).
  std::size_t remove_generation(const std::string& mod, std::uint64_t generation);
  std::size_t count(const std::string& mod, std::uint64_t generation) const;

  // Builds the effective order of every pipeline; throws LoadError on any conflict. Plans are kept for run/describe.
  void validate(const PipelineOptions& opts = {});

  bool defined(const std::string& name) const { return defs_.count(name) != 0; }
  std::vector<std::string> names() const;
  std::vector<std::string> order(const std::string& name) const;  // active step ids
  PipelineInfo describe(const std::string& name) const;           // LoadError when not defined
  std::string owner_of(const std::string& name) const;

  // `caller` identifies the calling sandbox; only the defining mod's sandbox may run its pipeline.
  void run(const std::string& name, const void* caller, PipelineHost& host, PipelineRunResult& out);

  void disable_stage(const std::string& qualified_hook) { disabled_.insert(qualified_hook); }
  void enable_stage(const std::string& qualified_hook) { disabled_.erase(qualified_hook); }
  bool is_disabled(const std::string& qualified_hook) const { return disabled_.count(qualified_hook) != 0; }
  void set_tick(std::uint64_t t) { tick_ = t; }

 private:
  struct Definition {
    std::string name, owner;
    std::uint64_t generation = 0;
    const void* owner_key = nullptr;
    std::vector<std::string> stages;
    struct Reg { PipelineBodyPtr body; bool is_protected = false; };
    std::map<std::string, Reg> bodies;
  };
  struct Edit {
    PipelineOp op;
    std::string mod;
    std::uint64_t generation = 0;
    PipelineBodyPtr body;
    std::uint64_t seq = 0;
  };
  struct Plan;
  std::shared_ptr<const Plan> build(const std::string& name, const PipelineOptions& o) const;
  std::shared_ptr<const Plan> plan_for(const std::string& name) const;

  std::map<std::string, Definition> defs_;
  std::vector<Edit> edits_;
  std::set<std::string> disabled_;
  BudgetMeter strikes_;
  PipelineOptions opts_;
  std::uint64_t next_seq_ = 0, tick_ = 0;
  mutable std::map<std::string, std::shared_ptr<const Plan>> plans_;
};

}  // namespace script_host
