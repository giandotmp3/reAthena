#pragma once
#include <functional>
#include <unordered_map>
#include <vector>

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

// Hook contract (on-move hook, replaces rules embedded in legacy map_moveblock):
//  * before_move runs in the step timer, before the position changes, after
//    the cell was found walkable. Returning Veto ends the walk right there: the
//    unit stays where it is, `moved` is not called, the remaining path is
//    dropped, Mover::walking becomes false. There is no retry.
//  * moved runs after Position and the spatial index were updated. It may call
//    stop() or request_walk() for the same entity; the walk then follows that
//    call (the legacy "moveblock altered the object" check).
struct MoveHooks {
  std::function<MoveVerdict(EntityId, Point from, Point to)> before_move;
  std::function<void(EntityId, Point from, Point to)> moved;
};

struct WalkResult {
  bool accepted = false;
};

struct MovementConfig {
  // Legacy compiles OFFICIAL_WALKPATH in, so it is the default.
  PathMode path_mode = PathMode::OfficialWalkpath;
  PathConfig path;
};

// Walk request -> path -> one timer per cell step (legacy unit_walktoxy /
// unit_walktoxy_nextcell / unit_walktoxy_timer):
//  * request_walk only plans (A* from the current cell); nothing moves until
//    the first step timer. Accepted with an empty path (already there) does
//    nothing at all.
//  * step time is Mover::step_ms, times 14/10 (integer) for a diagonal step;
//    the next step is chained to the *scheduled* time of the previous one, so
//    steps do not drift; after a stall over 1 s the chain restarts from now
//    (TimerQueue late rule).
//  * if the next cell turned unwalkable before its step, the path is searched
//    again from the current cell; if that fails the walk ends.
//  * request_walk while walking only replaces the destination; the unit
//    finishes its current cell, then re-plans (legacy change_walk_target).
//  * the destination being the current position is accepted as a no-op.
class MovementSystem {
public:
  MovementSystem(Registry& reg, MapGrid& grid, SpatialIndex& spatial,
                 TimerQueue& timers, MoveHooks hooks = {})
      : reg_(reg), grid_(grid), spatial_(spatial), timers_(timers),
        hooks_(std::move(hooks)) {}
  MovementSystem(const MovementSystem&) = delete;
  MovementSystem& operator=(const MovementSystem&) = delete;
  ~MovementSystem();

  MovementConfig config;
  WalkResult request_walk(EntityId e, Point dest);
  void stop(EntityId e);
  bool walking(EntityId e) const { return walks_.count(key(e)) != 0; }

private:
  struct Walk {
    std::vector<Point> path;
    size_t pos = 0;
    Point dest;
    TimerId timer = kNoTimer;
    bool retarget = false;
  };
  static uint64_t key(EntityId e) { return (uint64_t(e.generation) << 32) | e.index; }
  Tick step_delay(const Position& at, Point to, const Mover& m) const;
  void schedule(EntityId e, Walk& w, Tick delay);
  void on_step(EntityId e, Tick scheduled, Tick now);
  void finish(EntityId e);
  bool replan(EntityId e, Walk& w, Point from);

  Registry& reg_;
  MapGrid& grid_;
  SpatialIndex& spatial_;
  TimerQueue& timers_;
  MoveHooks hooks_;
  std::unordered_map<uint64_t, Walk> walks_;
};

}  // namespace engine
