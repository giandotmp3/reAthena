#pragma once
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "script_host/effect_queue.hpp"
#include "script_host/numeric.hpp"

namespace script_host {

enum class Phase { Pre, On, Post };
enum class Mode { Observe, Modify, Replace };

// Hook categories from the minimum hook surface (BR-RULESET-117).
enum class HookCategory { Skill, DamageStage, Status, Item, Formula };

struct HookSpec {
  std::string id;      // unique per mod, e.g. "fire_brand"
  std::string mod;     // owning mod id
  std::string event;   // "namespace:event"
  HookCategory category = HookCategory::DamageStage;
  int priority = 0;    // lower runs first
  std::vector<std::string> after, before;  // hook ids (qualified "mod/id")
  Phase phase = Phase::On;
  Mode mode = Mode::Observe;
  bool precedence = false;       // declared precedence for Replace conflicts
  std::uint64_t generation = 0;  // owner (mod, generation)
  std::string qualified() const { return mod + "/" + id; }
};

// Lazy read-through handle with a generation check (stale handle => error).
struct EntityHandle {
  std::uint64_t id = 0;
  std::uint64_t generation = 0;
};

struct HookContext {
  NumCtx num;                     // mutable int64 ctx for Modify hooks
  EffectQueue* effects = nullptr; // never applied inside the hook
  std::vector<EntityHandle> inputs;
  std::optional<std::string> cancel_reason;
  void cancel(std::string reason) { cancel_reason = std::move(reason); }
};

using HookFn = std::function<void(HookContext&)>;

}  // namespace script_host
