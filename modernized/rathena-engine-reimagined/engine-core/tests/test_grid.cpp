#include <vector>

#include "engine/grid/grid.hpp"
#include "harness.hpp"

using namespace engine;

namespace {
void open_all(MapGrid& g) {
  for (int y = 0; y < g.height(); ++y)
    for (int x = 0; x < g.width(); ++x)
      g.set_terrain(x, y, cell::Walkable | cell::Shootable);
}
}  // namespace

TEST("EC-GRID-001", last_row_and_column_count_as_solid) {
  MapGrid g(10, 10);
  open_all(g);
  CHECK(g.walkable(8, 8));
  CHECK(!g.walkable(9, 3));
  CHECK(!g.walkable(3, 9));
  CHECK(!g.shootable(9, 9));
  CHECK(!g.walkable(-1, 0));
  CHECK(!g.walkable(10, 0));
}

TEST("EC-GRID-002", dynamic_flags_are_opaque_registered_bits) {
  MapGrid g(10, 10);
  open_all(g);
  DynamicFlagRegistry reg;
  CellFlags f1 = reg.register_flag("test:flag_one");
  CellFlags f2 = reg.register_flag("test:flag_two");
  CHECK(f1 != f2);
  CHECK((f1 & cell::kTerrainMask) == 0);
  CHECK(reg.register_flag("test:flag_one") == f1);
  g.set_dynamic(2, 2, f1);
  CHECK(g.has(2, 2, f1));
  CHECK(!g.has(2, 2, f2));
  CHECK(g.walkable(2, 2));  // dynamic bits do not alter terrain
  g.clear_dynamic(2, 2, f1);
  CHECK(!g.has(2, 2, f1));
}

TEST("EC-GRID-003", bucket_is_8x8) {
  CHECK(block_of(0) == 0);
  CHECK(block_of(7) == 0);
  CHECK(block_of(8) == 1);
  CHECK(block_of(17) == 2);
}

TEST("EC-DIST-001", chebyshev_distance_for_rules) {
  CHECK(distance_chebyshev({0, 0}, {3, 4}) == 4);
  CHECK(distance_chebyshev({5, 5}, {5, 5}) == 0);
  CHECK(distance_chebyshev({-2, 1}, {2, -1}) == 4);
}

TEST("EC-DIST-002", client_distance_is_floor_of_euclid_minus_tenth) {
  CHECK(distance_client({0, 0}, {3, 4}) == 4);  // sqrt(25)-0.1 = 4.9
  CHECK(distance_client({0, 0}, {1, 0}) == 0);  // 0.9
  CHECK(distance_client({0, 0}, {1, 1}) == 1);  // 1.314
  CHECK(distance_client({0, 0}, {0, 0}) == -1); // -0.1 floors to -1 (legacy formula)
}

PENDING_TEST("EC-GRID-004", area_query_is_chebyshev_square) {
  MapGrid g(32, 32);
  SpatialIndex sp(g);
  EntityAllocator al;
  EntityId in = al.create(), out = al.create(), corner = al.create();
  sp.insert(in, {7, 7}, BucketClass::Primary);
  sp.insert(out, {8, 5}, BucketClass::Primary);
  sp.insert(corner, {7, 3}, BucketClass::Primary);
  std::vector<EntityId> seen;
  sp.for_each_in_area(AreaQuery::around({5, 5}, 2), [&](EntityId e) { seen.push_back(e); });
  CHECK(seen.size() == 2);  // corner (7,3) is inside the square; (8,5) is not
}

PENDING_TEST("EC-GRID-005", area_order_primary_then_secondary_lifo_within_bucket) {
  MapGrid g(32, 32);
  SpatialIndex sp(g);
  EntityAllocator al;
  EntityId a = al.create(), b = al.create(), c = al.create();
  sp.insert(a, {1, 1}, BucketClass::Primary);
  sp.insert(b, {1, 1}, BucketClass::Secondary);
  sp.insert(c, {1, 1}, BucketClass::Primary);
  std::vector<EntityId> seen;
  sp.for_each_in_area(AreaQuery::around({1, 1}, 3), [&](EntityId e) { seen.push_back(e); });
  CHECK(seen.size() == 3);
  CHECK(seen[0] == c);  // last inserted first
  CHECK(seen[1] == a);
  CHECK(seen[2] == b);  // secondary pass last
}

PENDING_TEST("EC-GRID-006", area_is_snapshot_then_call_and_skips_removed) {
  MapGrid g(32, 32);
  SpatialIndex sp(g);
  EntityAllocator al;
  EntityId a = al.create(), b = al.create(), late = al.create();
  sp.insert(a, {1, 1}, BucketClass::Primary);
  sp.insert(b, {2, 2}, BucketClass::Primary);
  std::vector<EntityId> seen;
  sp.for_each_in_area(AreaQuery::around({1, 1}, 3), [&](EntityId e) {
    seen.push_back(e);
    if (seen.size() == 1) {
      EntityId victim = (e == a) ? b : a;
      sp.remove(victim);                              // removed mid-iteration
      sp.insert(late, {1, 2}, BucketClass::Primary);  // added mid-iteration
    }
  });
  CHECK(seen.size() == 1);  // removed one skipped, late insert not visited
}

PENDING_TEST("EC-GRID-007", area_wall_check_requires_clear_shootable_line) {
  MapGrid g(32, 32);
  open_all(g);
  g.set_terrain(5, 3, cell::Walkable);  // walkable but not shootable: blocks the line
  SpatialIndex sp(g);
  EntityAllocator al;
  EntityId behind = al.create(), clear = al.create();
  sp.insert(behind, {8, 3}, BucketClass::Primary);
  sp.insert(clear, {8, 6}, BucketClass::Primary);
  AreaQuery q = AreaQuery::around({3, 3}, 6);
  q.wall_check_from = Point{3, 3};
  std::vector<EntityId> seen;
  sp.for_each_in_area(q, [&](EntityId e) { seen.push_back(e); });
  CHECK(seen.size() == 1 && seen[0] == clear);
}

PENDING_TEST("EC-GRID-008", spatial_move_and_remove_track_position) {
  MapGrid g(32, 32);
  SpatialIndex sp(g);
  EntityAllocator al;
  EntityId a = al.create();
  sp.insert(a, {1, 1}, BucketClass::Primary);
  sp.move(a, {20, 20});
  CHECK(sp.position_of(a) == Point({20, 20}));
  std::vector<EntityId> seen;
  sp.for_each_in_area(AreaQuery::around({1, 1}, 3), [&](EntityId e) { seen.push_back(e); });
  CHECK(seen.empty());
  CHECK(sp.remove(a));
  CHECK(!sp.position_of(a).has_value());
}

