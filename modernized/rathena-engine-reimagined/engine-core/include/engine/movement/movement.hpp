#pragma once
#include <functional>

#include "engine/common.hpp"
#include "engine/ecs/registry.hpp"
#include "engine/grid/grid.hpp"
#include "engine/path/path.hpp"
#include "engine/tick/tick.hpp"

namespace engine {

// Per-unit walking state. step_ms is supplied by the ruleset (speed is a
// game number; the engine only schedules).
struct Mover {
  int32_t step_ms = 150;
  bool walking = false;
};

enum class MoveVerdict : uint8_t { Allow, Veto };

struct MoveHooks {
  // Before a single-cell step; Veto stops the walk without moving.
  std::function<MoveVerdict(EntityId, Point from, Point to)> before_move;
  // After the step was applied.
  std::function<void(EntityId, Point from, Point to)> moved;
};

struct WalkResult {
  bool accepted = false;
};

// STUBS (EC-MOVE-*). One timer per cell step; position changes only in the
// timer; spatial index updated with each step.
class MovementSystem {
public:
  MovementSystem(Registry& reg, MapGrid& grid, SpatialIndex& spatial,
                 TimerQueue& timers, MoveHooks hooks = {})
      : reg_(reg), grid_(grid), spatial_(spatial), timers_(timers),
        hooks_(std::move(hooks)) {}
  WalkResult request_walk(EntityId e, Point dest);
  void stop(EntityId e);

private:
  Registry& reg_;
  MapGrid& grid_;
  SpatialIndex& spatial_;
  TimerQueue& timers_;
  MoveHooks hooks_;
};

}  // namespace engine
