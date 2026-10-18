#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace nd {

// BR-RULESET-116: scripts and stages never mutate the world during calculation.
// They append effects; native host code applies them after the result is final.
struct Effect {
  std::string kind;          // opaque namespaced id, e.g. "ruleset.x:effect/reflect"
  std::uint64_t target = 0;  // entity id
  std::int64_t arg0 = 0;
  std::int64_t arg1 = 0;
};

struct EffectQueue {
  std::vector<Effect> items;
  bool applying = false;  // re-entrancy guard, set by the host while draining

  void push(Effect e);          // throws std::logic_error if called while applying
  void clear() noexcept { items.clear(); }
};

// Mutable damage context for one hit. Damage values are int64 (BR-RULESET-115).
struct DamageCtx {
  std::int64_t damage = 0;
  int div = 1;                         // hit count (BR-RULESET-036)

  int attack_element = 0;
  int target_element = 0;
  int target_element_level = 1;
  int weapon_type = 0;
  int target_size = 0;

  std::int64_t target_def = 0;
  std::int64_t target_soft_def = 0;
  std::int64_t target_res = 0;
  std::int64_t ignore_res_pct = 0;
  std::int64_t status_element_pct = 0; // e.g. Deluge val3 in E-1

  bool is_crit = false;
  bool missed = false;

  EffectQueue effects;
};

// RNG seam. Draw counts are part of the contract (BR-RULESET-115): a draw is
// consumed even when the resulting chance is <= 0.
class IRng {
 public:
  virtual ~IRng() = default;
  virtual std::uint32_t next() = 0;           // one draw
  virtual std::uint64_t draws() const = 0;    // total draws so far
  std::uint32_t rnd(std::uint32_t bound) { return bound == 0 ? (void(next()), 0u) : next() % bound; }
};

}  // namespace nd
