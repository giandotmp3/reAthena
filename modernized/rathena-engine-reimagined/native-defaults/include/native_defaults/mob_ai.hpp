#pragma once
// native.mob_ai.default@1: always native (BR-RULESET-113). Driven by mode bits and
// tunables from the mob definition. Scripts supply only mob-skill conditions,
// idle/kill events and AI-mode tunables.
#include <cstdint>
#include <optional>
#include <vector>

namespace nd::mobai {

inline constexpr int kHardTickMs = 100;   // mob.cpp:7020
inline constexpr int kLazyTickMs = 1000;  // mob.cpp:7021

// Opaque mode bits. Meaning of each bit is assigned by the ruleset's mob definition;
// this module only reads the roles below via ModeMap.
struct ModeMap {
  std::uint32_t can_move = 0;
  std::uint32_t looter = 0;
  std::uint32_t aggressive = 0;
  std::uint32_t assist = 0;
  std::uint32_t change_target = 0;
  std::uint32_t angry = 0;
  std::uint32_t berserk = 0;
};

enum class State { Idle, Walk, Chase, Attack, Loot, Follow, Rush, Dead };

struct Tunables {
  int aggro_range = 0;
  int chase_range = 0;
  int attack_range = 1;
  int idle_walk_interval_ms = 0;
  int target_lost_ms = 0;
};

struct Nearby {
  std::uint64_t id = 0;
  int distance = 0;      // Chebyshev
  bool attackable = true;
};

struct MobView {
  std::uint64_t id = 0;
  State state = State::Idle;
  std::uint32_t modes = 0;
  Tunables tunables;
  std::optional<std::uint64_t> target;
  std::optional<std::uint64_t> master;   // slave logic
  std::int64_t last_attacked_by = 0;
  std::int64_t now_ms = 0;
};

enum class Action { None, WalkRandom, MoveToTarget, AttackTarget, PickLoot, FollowMaster, Despawn };

struct Decision {
  State next_state = State::Idle;
  Action action = Action::None;
  std::optional<std::uint64_t> target;
  int next_tick_ms = kLazyTickMs;   // hard tick while active, lazy when idle
};

// Pure decision function: no world mutation (effects are applied by the host).
Decision tick(const MobView& mob, const ModeMap& map, const std::vector<Nearby>& scan);

}  // namespace nd::mobai
