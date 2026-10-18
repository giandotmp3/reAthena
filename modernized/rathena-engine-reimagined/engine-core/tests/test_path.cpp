#include "engine/path/path.hpp"
#include "harness.hpp"

using namespace engine;

namespace {
MapGrid open_grid(uint16_t w, uint16_t h) {
  MapGrid g(w, h);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) g.set_terrain(x, y, cell::Walkable | cell::Shootable);
  return g;
}
}  // namespace

PENDING_TEST("EC-PATH-001", straight_orthogonal_path_costs_10_per_step) {
  MapGrid g = open_grid(20, 20);
  PathResult r = path_search(g, {2, 2}, {6, 2});
  CHECK(r.found);
  CHECK(r.steps.size() == 4);
  CHECK(r.steps.back() == Point({6, 2}));
  CHECK(r.cost == 40);
}

PENDING_TEST("EC-PATH-002", diagonal_step_costs_14) {
  MapGrid g = open_grid(20, 20);
  PathResult r = path_search(g, {2, 2}, {5, 5});
  CHECK(r.found);
  CHECK(r.steps.size() == 3);
  CHECK(r.cost == 3 * kCostDiagonal);
}

PENDING_TEST("EC-PATH-003", no_corner_cutting) {
  MapGrid g = open_grid(20, 20);
  g.set_terrain(3, 2, 0);  // wall east of start
  PathResult r = path_search(g, {2, 2}, {4, 4});
  CHECK(r.found);
  Point prev{2, 2};
  for (Point p : r.steps) {
    if (p.x != prev.x && p.y != prev.y) {  // diagonal move
      CHECK(g.walkable(p.x, prev.y));
      CHECK(g.walkable(prev.x, p.y));
    }
    prev = p;
  }
}

PENDING_TEST("EC-PATH-004", max_walkpath_limit_is_engine_config) {
  MapGrid g = open_grid(100, 20);
  PathResult shortcfg = path_search(g, {2, 2}, {42, 2});  // 40 steps > default 32
  CHECK(!shortcfg.found);
  PathConfig cfg;
  cfg.max_walkpath = 64;
  PathResult longcfg = path_search(g, {2, 2}, {42, 2}, PathMode::Standard, cfg);
  CHECK(longcfg.found && longcfg.steps.size() == 40);
}

PENDING_TEST("EC-PATH-005", unreachable_destination_fails) {
  MapGrid g = open_grid(20, 20);
  for (int y = 0; y < 20; ++y) g.set_terrain(10, y, 0);  // full wall
  CHECK(!path_search(g, {2, 2}, {15, 2}).found);
}

PENDING_TEST("EC-PATH-006", path_search_long_needs_clear_shootable_line) {
  MapGrid g = open_grid(30, 30);
  CHECK(path_search_long(g, {2, 2}, {12, 2}));
  g.set_terrain(7, 2, cell::Walkable);  // walkable, not shootable
  CHECK(!path_search_long(g, {2, 2}, {12, 2}));
}

PENDING_TEST("EC-PATH-007", official_walkpath_over_14_also_needs_long_search) {
  MapGrid g = open_grid(40, 20);
  g.set_terrain(10, 2, cell::Walkable);  // blocks the shoot line, walkable around
  // 16 cells away: standard path exists, official rule rejects (len > 14).
  CHECK(path_search(g, {2, 2}, {18, 2}, PathMode::Standard).found);
  CHECK(!path_search(g, {2, 2}, {18, 2}, PathMode::OfficialWalkpath).found);
  // 10 cells away (len <= 14): official rule does not require the long search.
  CHECK(path_search(g, {2, 2}, {12, 2}, PathMode::OfficialWalkpath).found);
}

PENDING_TEST("EC-PATH-008", search_is_deterministic) {
  MapGrid g = open_grid(30, 30);
  g.set_terrain(8, 5, 0);
  PathResult a = path_search(g, {3, 3}, {14, 9});
  PathResult b = path_search(g, {3, 3}, {14, 9});
  CHECK(a.found && b.found);
  CHECK(a.steps == b.steps);
  CHECK(a.cost == b.cost);
}
