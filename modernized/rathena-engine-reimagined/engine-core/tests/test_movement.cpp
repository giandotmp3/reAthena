#include <vector>

#include "engine/movement/movement.hpp"
#include "harness.hpp"

using namespace engine;

namespace {
struct World {
  MapGrid grid{40, 40};
  SpatialIndex spatial{grid};
  Registry reg;
  VirtualClock clock;
  TimerQueue timers{clock};
  World() {
    for (int y = 0; y < 40; ++y)
      for (int x = 0; x < 40; ++x) grid.set_terrain(x, y, cell::Walkable | cell::Shootable);
  }
  EntityId spawn(Point p, int step_ms = 150) {
    EntityId e = reg.create();
    reg.emplace<Position>(e, Position{p.x, p.y});
    reg.emplace<Mover>(e, Mover{step_ms, false});
    spatial.insert(e, p, BucketClass::Primary);
    return e;
  }
};
}  // namespace

PENDING_TEST("EC-MOVE-001", walk_advances_one_cell_per_step_timer) {
  World w;
  MovementSystem mv(w.reg, w.grid, w.spatial, w.timers);
  EntityId e = w.spawn({2, 2});
  CHECK(mv.request_walk(e, {5, 2}).accepted);
  CHECK(w.reg.get<Position>(e)->x == 2);  // no movement until the first timer
  w.clock.advance(150); w.timers.run_due();
  CHECK(w.reg.get<Position>(e)->x == 3);
  w.clock.advance(300); w.timers.run_due();
  CHECK(w.reg.get<Position>(e)->x == 5);
}

PENDING_TEST("EC-MOVE-002", hooks_fire_per_step_and_veto_stops_walk) {
  World w;
  std::vector<Point> moved;
  MoveHooks h;
  h.before_move = [](EntityId, Point, Point to) {
    return to.x >= 4 ? MoveVerdict::Veto : MoveVerdict::Allow;
  };
  h.moved = [&](EntityId, Point, Point to) { moved.push_back(to); };
  MovementSystem mv(w.reg, w.grid, w.spatial, w.timers, h);
  EntityId e = w.spawn({2, 2});
  mv.request_walk(e, {8, 2});
  w.clock.advance(150 * 6); w.timers.run_due();
  CHECK(w.reg.get<Position>(e)->x == 3);
  CHECK((moved == std::vector<Point>{{3, 2}}));
}

PENDING_TEST("EC-MOVE-003", steps_update_spatial_index) {
  World w;
  MovementSystem mv(w.reg, w.grid, w.spatial, w.timers);
  EntityId e = w.spawn({2, 2});
  mv.request_walk(e, {4, 2});
  w.clock.advance(300); w.timers.run_due();
  CHECK(w.spatial.position_of(e) == Point({4, 2}));
}

PENDING_TEST("EC-MOVE-004", walk_to_unreachable_cell_is_rejected) {
  World w;
  w.grid.set_terrain(5, 2, 0);
  MovementSystem mv(w.reg, w.grid, w.spatial, w.timers);
  EntityId e = w.spawn({2, 2});
  CHECK(!mv.request_walk(e, {5, 2}).accepted);
}
