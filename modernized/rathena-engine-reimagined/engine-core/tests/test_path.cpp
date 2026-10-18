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

TEST("EC-PATH-001", straight_orthogonal_path_costs_10_per_step) {
  MapGrid g = open_grid(20, 20);
  PathResult r = path_search(g, {2, 2}, {6, 2});
  CHECK(r.found);
  CHECK(r.steps.size() == 4);
  CHECK(r.steps.back() == Point({6, 2}));
  CHECK(r.cost == 40);
}

TEST("EC-PATH-002", diagonal_step_costs_14) {
  MapGrid g = open_grid(20, 20);
  PathResult r = path_search(g, {2, 2}, {5, 5});
  CHECK(r.found);
  CHECK(r.steps.size() == 3);
  CHECK(r.cost == 3 * kCostDiagonal);
}

TEST("EC-PATH-003", no_corner_cutting) {
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

TEST("EC-PATH-004", max_walkpath_limit_is_engine_config) {
  MapGrid g = open_grid(100, 20);
  PathResult shortcfg = path_search(g, {2, 2}, {42, 2});  // 40 steps > default 32
  CHECK(!shortcfg.found);
  PathConfig cfg;
  cfg.max_walkpath = 64;
  PathResult longcfg = path_search(g, {2, 2}, {42, 2}, PathMode::Standard, cfg);
  CHECK(longcfg.found && longcfg.steps.size() == 40);
}

TEST("EC-PATH-005", unreachable_destination_fails) {
  MapGrid g = open_grid(20, 20);
  for (int y = 0; y < 20; ++y) g.set_terrain(10, y, 0);  // full wall
  CHECK(!path_search(g, {2, 2}, {15, 2}).found);
}

TEST("EC-PATH-006", path_search_long_needs_clear_shootable_line) {
  MapGrid g = open_grid(30, 30);
  CHECK(path_search_long(g, {2, 2}, {12, 2}));
  g.set_terrain(7, 2, cell::Walkable);  // walkable, not shootable
  CHECK(!path_search_long(g, {2, 2}, {12, 2}));
}

TEST("EC-PATH-007", official_walkpath_over_14_also_needs_long_search) {
  // Legacy unit_walktoxy runs the long search with CELL_CHKNOPASS, i.e. the
  // straight line must be *walkable*. (The scaffold used a walkable but not
  // shootable cell, which cannot block the walkability line and which map data
  // cannot produce; a real wall is used instead. See EC-PATH-011.)
  MapGrid g = open_grid(40, 20);
  g.set_terrain(10, 2, 0);  // wall on the straight line, walkable around it
  // 16 cells away: standard path exists, official rule rejects (len > 14).
  CHECK(path_search(g, {2, 2}, {18, 2}, PathMode::Standard).found);
  CHECK(!path_search(g, {2, 2}, {18, 2}, PathMode::OfficialWalkpath).found);
  // 10 cells away (len <= 14): official rule does not require the long search.
  CHECK(path_search(g, {2, 2}, {12, 2}, PathMode::OfficialWalkpath).found);
}

TEST("EC-PATH-011", official_rule_checks_walkability_not_shootability_like_legacy) {
  MapGrid g = open_grid(40, 20);
  g.set_terrain(10, 2, cell::Shootable);  // cliff: shoot through, cannot walk
  PathResult std_r = path_search(g, {2, 2}, {18, 2}, PathMode::Standard);
  CHECK(std_r.found && std_r.steps.size() == 16);
  // The straight line crosses the cliff: official rule rejects.
  CHECK(!path_search(g, {2, 2}, {18, 2}, PathMode::OfficialWalkpath).found);
  // A walkable-but-not-shootable cell does not stop the official rule.
  MapGrid h = open_grid(40, 20);
  h.set_terrain(10, 2, cell::Walkable);
  CHECK(path_search(h, {2, 2}, {18, 2}, PathMode::OfficialWalkpath).found);
  // ...but it does stop a shot (wall check), as EC-PATH-006 requires.
  CHECK(!path_search_long(h, {2, 2}, {18, 2}, LineBlock::Wall));
  CHECK(path_search_long(h, {2, 2}, {18, 2}, LineBlock::NoPass));
  // Cliff: walkability line blocked, shot line clear (legacy CELL_CHKWALL).
  CHECK(path_search_long(g, {2, 2}, {18, 2}, LineBlock::Wall));
  CHECK(!path_search_long(g, {2, 2}, {18, 2}, LineBlock::NoPass));
}

TEST("EC-PATH-008", search_is_deterministic) {
  MapGrid g = open_grid(30, 30);
  g.set_terrain(8, 5, 0);
  PathResult a = path_search(g, {3, 3}, {14, 9});
  PathResult b = path_search(g, {3, 3}, {14, 9});
  CHECK(a.found && b.found);
  CHECK(a.steps == b.steps);
  CHECK(a.cost == b.cost);
}
