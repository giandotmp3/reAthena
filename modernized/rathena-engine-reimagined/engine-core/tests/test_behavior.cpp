// Additional behaviour tests for decisions the spec left open (each cites the
// legacy code it mirrors) and for edge cases of the primitives.
#include <vector>

#include "engine/attrs/attrs.hpp"
#include "engine/ecs/registry.hpp"
#include "engine/ecs/schema.hpp"
#include "engine/grid/grid.hpp"
#include "engine/instance/instance.hpp"
#include "engine/movement/movement.hpp"
#include "engine/path/path.hpp"
#include "engine/tick/tick.hpp"
#include "harness.hpp"

using namespace engine;

namespace {
MapGrid open_grid(uint16_t w, uint16_t h) {
  MapGrid g(w, h);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) g.set_terrain(x, y, cell::Walkable | cell::Shootable);
  return g;
}
void open_all(MapGrid& g) {
  for (int y = 0; y < g.height(); ++y)
    for (int x = 0; x < g.width(); ++x) g.set_terrain(x, y, cell::Walkable | cell::Shootable);
}
struct World {
  MapGrid grid{40, 40};
  SpatialIndex spatial{grid};
  Registry reg;
  VirtualClock clock;
  TimerQueue timers{clock};
  World() { open_all(grid); }
  EntityId spawn(Point p, int step_ms = 100) {
    EntityId e = reg.create();
    reg.emplace<Position>(e, Position{p.x, p.y});
    reg.emplace<Mover>(e, Mover{step_ms, false});
    spatial.insert(e, p, BucketClass::Primary);
    return e;
  }
  void run(Tick ms) { clock.advance(ms); timers.run_due(); }
  // Polls every 50 ms, like a game loop (a single jump of more than 1 s would
  // hit the late-timer rule and restart the step chain from `now`).
  void run_for(Tick ms) { for (Tick t = 0; t < ms; t += 50) run(50); }
};
}  // namespace

// ---- ecs / schema / attrs -------------------------------------------------

TEST("EC-ECS-003", deferred_destroy_is_idempotent_and_keeps_components_until_flush) {
  Registry r;
  EntityId e = r.create();
  r.emplace<Position>(e, Position{1, 2});
  r.defer_destroy(e);
  r.defer_destroy(e);
  CHECK(r.get<Position>(e) != nullptr);
  CHECK(r.flush_deferred() == 1);
  CHECK(!r.alive(e) && r.pool<Position>().size() == 0);
  CHECK(r.flush_deferred() == 0);
  r.defer_destroy(e);  // stale handle: ignored
  CHECK(r.flush_deferred() == 0);
}

TEST("EC-ECS-005", schema_reregistration_namespacing_defaults_and_stale_handles) {
  EntityAllocator al;
  SchemaStore s;
  std::vector<FieldDef> shape{{"n", FieldType::Int}, {"d", FieldType::Double},
                              {"s", FieldType::String}, {"b", FieldType::Bool}};
  SchemaId id = s.register_schema("m:c", shape);
  CHECK(s.register_schema("m:c", shape) == id);  // identical: same id
  bool threw = false;
  try { s.register_schema("m:c", {{"n", FieldType::Double}}); } catch (const std::exception&) { threw = true; }
  CHECK(threw);  // different shape
  threw = false;
  try { s.register_schema("nonamespace", shape); } catch (const std::exception&) { threw = true; }
  CHECK(threw);
  EntityId e = al.create();
  CHECK(std::get<int64_t>(s.get(e, id, "n")) == 0);
  CHECK(std::get<double>(s.get(e, id, "d")) == 0.0);
  CHECK(std::get<std::string>(s.get(e, id, "s")).empty());
  CHECK(std::get<bool>(s.get(e, id, "b")) == false);
  s.set(e, id, "s", std::string("hi"));
  CHECK(std::get<std::string>(s.get(e, id, "s")) == "hi");
  threw = false;
  try { s.get(e, id, "nope"); } catch (const std::out_of_range&) { threw = true; }
  CHECK(threw);
  al.destroy(e);
  EntityId e2 = al.create();  // same index, new generation
  CHECK(std::get<std::string>(s.get(e2, id, "s")).empty());
}

TEST("EC-ATTR-005", no_stage_list_reads_zero_and_stale_handles_do_not_alias) {
  AttributeSystem s;
  EntityAllocator al;
  AttrId a = s.intern("a");
  EntityId e = al.create();
  s.add_contribution(e, a, {1, "t", 5});
  CHECK(s.get(e, a) == 0);  // no stages defined: the engine does not guess
  s.define_stages(a, {{"sum", [](int64_t acc, std::span<const Contribution> cs) {
                         for (auto& c : cs) acc += c.value;
                         return acc;
                       }}});
  CHECK(s.get(e, a) == 5);  // redefining invalidates the cache
  al.destroy(e);
  EntityId e2 = al.create();
  CHECK(s.get(e2, a) == 0);
  CHECK(s.remove_source(e2, 1) == 0);
}

// ---- grid -----------------------------------------------------------------

TEST("EC-GRID-010", bucket_list_position_kept_on_same_bucket_move_head_on_cross_bucket) {
  MapGrid g(32, 32);
  SpatialIndex sp(g);
  EntityAllocator al;
  EntityId a = al.create(), b = al.create(), c = al.create();
  sp.insert(a, {1, 1}, BucketClass::Primary);
  sp.insert(b, {2, 2}, BucketClass::Primary);
  sp.insert(c, {3, 3}, BucketClass::Primary);  // all in bucket (0,0): order c b a
  auto order = [&] {
    std::vector<EntityId> v;
    sp.for_each_in_area(AreaQuery::around({12, 12}, 12), [&](EntityId e) { v.push_back(e); });
    return v;
  };
  CHECK((order() == std::vector<EntityId>{c, b, a}));
  sp.move(a, {4, 4});  // same bucket: list position unchanged (legacy map_moveblock)
  CHECK((order() == std::vector<EntityId>{c, b, a}));
  sp.move(a, {9, 1});  // other bucket: removed here, head of the new bucket (0..,1 row scans bucket (0,0) then (1,0))
  CHECK((order() == std::vector<EntityId>{c, b, a}));
  sp.move(c, {10, 1});  // joins bucket (1,0) at its head: now c before a there
  CHECK((order() == std::vector<EntityId>{b, c, a}));
}

TEST("EC-GRID-011", area_is_clamped_to_map_and_rejects_out_of_bounds_insert) {
  MapGrid g(16, 16);
  SpatialIndex sp(g);
  EntityAllocator al;
  EntityId a = al.create(), b = al.create();
  CHECK(!sp.insert(a, {16, 3}, BucketClass::Primary));
  CHECK(!sp.insert(a, {-1, 3}, BucketClass::Primary));
  CHECK(sp.insert(a, {15, 15}, BucketClass::Primary));  // last cell is a valid bucket position
  CHECK(!sp.move(a, {16, 16}));
  CHECK(sp.position_of(a) == Point({15, 15}));
  CHECK(sp.move(b, {1, 1}));  // not indexed: no-op
  int n = 0;
  sp.for_each_in_area({-50, -50, 50, 50, std::nullopt}, [&](EntityId) { ++n; });
  CHECK(n == 1);
  n = 0;
  sp.for_each_in_area({20, 20, 30, 30, std::nullopt}, [&](EntityId) { ++n; });
  CHECK(n == 0);
}

TEST("EC-GRID-012", wall_check_end_cell_and_origin_cell_are_not_tested) {
  MapGrid g(32, 32);
  open_all(g);
  g.set_terrain(8, 3, 0);  // the target itself stands on a wall cell
  g.set_terrain(3, 3, 0);  // and so does the origin
  SpatialIndex sp(g);
  EntityAllocator al;
  EntityId t = al.create();
  sp.insert(t, {8, 3}, BucketClass::Secondary);
  AreaQuery q = AreaQuery::around({3, 3}, 6);
  q.wall_check_from = Point{3, 3};
  int n = 0;
  sp.for_each_in_area(q, [&](EntityId) { ++n; });
  CHECK(n == 1);
}

// ---- path -----------------------------------------------------------------

TEST("EC-PATH-013", start_equals_destination_and_blocked_or_outside_destination) {
  MapGrid g(20, 20);
  open_all(g);
  PathResult same = path_search(g, {5, 5}, {5, 5});
  CHECK(same.found && same.steps.empty() && same.cost == 0);
  g.set_terrain(8, 8, 0);
  CHECK(!path_search(g, {5, 5}, {8, 8}).found);    // destination blocked
  CHECK(!path_search(g, {5, 5}, {19, 5}).found);   // last column is solid
  CHECK(!path_search(g, {5, 5}, {25, 5}).found);   // outside
  CHECK(!path_search(g, {-1, 5}, {6, 5}).found);   // start outside
  CHECK(path_search(g, {8, 8}, {9, 8}).found);     // start cell is not checked
}

TEST("EC-PATH-014", equal_cost_tie_break_follows_legacy_heap_and_neighbour_order) {
  // One-cell pillar between start and goal; both detours cost 48. Which one
  // the search returns is decided by the neighbour expansion order plus the
  // legacy binary-heap sift rules. The expected route was read off the legacy
  // reference copy (EC-PATH-010 proves the two agree on thousands of maps).
  MapGrid g = open_grid(20, 20);
  g.set_terrain(6, 5, 0);
  PathResult r = path_search(g, {4, 5}, {8, 5});
  CHECK(r.found && r.cost == 2 * kCostDiagonal + 2 * kCostOrthogonal);
  CHECK((r.steps == std::vector<Point>{{5, 6}, {6, 6}, {7, 6}, {8, 5}}));
}

TEST("EC-PATH-015", blownpos_slides_halts_and_caps_count) {
  MapGrid g(30, 30);
  open_all(g);
  auto never = [] { return false; };
  CHECK(path_blownpos(g, {5, 5}, 1, 0, 4, true, never) == Point({9, 5}));
  CHECK(path_blownpos(g, {5, 5}, 5, -9, 2, true, never) == Point({7, 3}));  // direction clamped to -1..1
  CHECK(path_blownpos(g, {2, 2}, 1, 0, 100, true, never) == Point({27, 2}));  // count capped at 25
  g.set_terrain(8, 5, 0);
  CHECK(path_blownpos(g, {5, 5}, 1, 0, 6, true, never) == Point({7, 5}));   // halt on wall
  CHECK(path_blownpos(g, {5, 5}, 1, 0, 6, false, never) == Point({7, 5}));  // slide: dx zeroed, dy is 0
  // Diagonal into a wall at (8,6): (8,5) free and (7,6) free -> coin decides
  // (legacy rnd_chance true zeroes dx, false zeroes dy).
  MapGrid h(30, 30);
  open_all(h);
  h.set_terrain(8, 6, 0);
  CHECK(path_blownpos(h, {7, 5}, 1, 1, 1, false, [] { return true; }) == Point({7, 6}));
  CHECK(path_blownpos(h, {7, 5}, 1, 1, 1, false, [] { return false; }) == Point({8, 5}));
  CHECK(path_blownpos(h, {7, 5}, 1, 1, 3, true, [] { return true; }) == Point({7, 5}));  // halt
  // Only one axis free: no coin needed.
  h.set_terrain(8, 5, 0);
  CHECK(path_blownpos(h, {7, 5}, 1, 1, 1, false, [] { return false; }) == Point({7, 6}));
}

// ---- tick -----------------------------------------------------------------

TEST("EC-TICK-007", callback_gets_now_when_more_than_one_second_late) {
  VirtualClock c;
  TimerQueue q(c);
  Tick s1 = -1, s2 = -1;
  q.add_once(100, [&](TimerId, Tick s, Tick) { s1 = s; });
  q.add_once(100, [&](TimerId, Tick s, Tick) { s2 = s; });
  c.advance(100 + 1000);  // exactly 1000 late: still the scheduled time
  q.run_due();
  CHECK(s1 == 100 && s2 == 100);
  Tick s3 = -1, n3 = -1;
  q.add_once(100, [&](TimerId, Tick s, Tick n) { s3 = s; n3 = n; });
  c.advance(100 + 1001);  // 1001 late: current tick replaces the scheduled one
  q.run_due();
  CHECK(s3 == n3 && n3 == c.now());
}

TEST("EC-TICK-008", interval_catches_up_within_one_second_then_skips_beyond) {
  VirtualClock c;
  TimerQueue q(c);
  std::vector<Tick> sched;
  q.add_interval(100, [&](TimerId, Tick s, Tick) { sched.push_back(s); });
  c.advance(450);
  CHECK(q.run_due() == 4);  // due 100, 200, 300, 400 in one call
  CHECK((sched == std::vector<Tick>{100, 200, 300, 400}));
  CHECK(q.next_due() == Tick{500});
  sched.clear();
  c.advance(5000 - 450 + 0);  // now 5000; next due 500 is 4500 late
  CHECK(q.run_due() == 1);
  CHECK(sched.size() == 1 && sched[0] == 5000);  // fired with `now`
  CHECK(q.next_due() == Tick{5100});             // rescheduled now + interval
}

TEST("EC-TICK-009", reentrancy_rules_cancel_inside_callback_zero_delay_and_bad_interval) {
  VirtualClock c;
  TimerQueue q(c);
  CHECK(q.add_interval(0, [](TimerId, Tick, Tick) {}) == kNoTimer);
  CHECK(q.add_interval(-5, [](TimerId, Tick, Tick) {}) == kNoTimer);
  int n = 0, chained = 0;
  TimerId self = kNoTimer;
  self = q.add_interval(10, [&](TimerId id, Tick, Tick) {
    ++n;
    CHECK(id == self);
    CHECK(q.run_due() == 0);  // not re-entrant
    q.add_once(0, [&](TimerId, Tick, Tick) { ++chained; });  // fires in the same run_due
    if (n == 2) CHECK(q.cancel(id));
  });
  c.advance(10); q.run_due();
  c.advance(10); q.run_due();
  c.advance(100); q.run_due();
  CHECK(n == 2 && chained == 2);
  CHECK(q.pending() == 0);
  CHECK(q.next_due() == std::nullopt);
  CHECK(q.poll_delay() == kPollCeilingMs);
  q.add_once(5, [](TimerId, Tick, Tick) {});
  CHECK(q.poll_delay() == kTickFloorMs);   // 5 ms clamps up to the 20 ms floor
  q.add_once(500, [](TimerId, Tick, Tick) {});
  CHECK(q.poll_delay() == kTickFloorMs);   // the earliest timer decides
}

TEST("EC-TICK-010", timer_ids_are_not_reused_and_cancel_of_fired_timer_fails) {
  VirtualClock c;
  TimerQueue q(c);
  TimerId a = q.add_once(1, [](TimerId, Tick, Tick) {});
  c.advance(1); q.run_due();
  CHECK(!q.cancel(a));
  TimerId b = q.add_once(1, [](TimerId, Tick, Tick) {});
  CHECK(b != a && b != kNoTimer);
}

// ---- movement -------------------------------------------------------------

TEST("EC-MOVE-005", diagonal_step_takes_speed_times_14_over_10_without_drift) {
  World w;
  MovementSystem mv(w.reg, w.grid, w.spatial, w.timers);
  EntityId e = w.spawn({2, 2}, 150);
  CHECK(mv.request_walk(e, {4, 3}).accepted);  // path: diagonal then orthogonal (or reverse)
  CHECK(mv.walking(e) && w.reg.get<Mover>(e)->walking);
  w.run(209);
  CHECK(w.reg.get<Position>(e)->x == 2);
  w.run(1);  // 210 = 150*14/10
  CHECK(w.reg.get<Position>(e)->x == 3);
  // Late poll (500 ms of wall time in one go) still lands on the scheduled
  // chain: second step due 210+150 = 360 (orthogonal), both done by 500.
  w.run(290);
  CHECK(w.reg.get<Position>(e)->x == 4 && w.reg.get<Position>(e)->y == 3);
  CHECK(!mv.walking(e) && !w.reg.get<Mover>(e)->walking);
  CHECK(w.timers.pending() == 0);
}

TEST("EC-MOVE-006", walk_request_while_walking_replans_after_current_cell) {
  World w;
  MovementSystem mv(w.reg, w.grid, w.spatial, w.timers);
  EntityId e = w.spawn({2, 2});
  mv.request_walk(e, {10, 2});
  w.run(100);  // at (3,2)
  CHECK(mv.request_walk(e, {3, 6}).accepted);
  CHECK(w.reg.get<Position>(e)->x == 3);
  w.run(100);  // finishes the in-flight cell (4,2), then re-plans from there
  CHECK(w.reg.get<Position>(e)->x == 4 && w.reg.get<Position>(e)->y == 2);
  w.run(1000);
  CHECK(w.reg.get<Position>(e)->x == 3 && w.reg.get<Position>(e)->y == 6);
  CHECK(w.spatial.position_of(e) == Point({3, 6}));
  CHECK(!mv.walking(e));
}

TEST("EC-MOVE-007", stop_cancels_timer_and_cell_blocked_mid_walk_triggers_replan) {
  World w;
  MovementSystem mv(w.reg, w.grid, w.spatial, w.timers);
  EntityId e = w.spawn({2, 2});
  mv.request_walk(e, {8, 2});
  w.run(100);
  mv.stop(e);
  CHECK(w.timers.pending() == 0 && !mv.walking(e));
  w.run(1000);
  CHECK(w.reg.get<Position>(e)->x == 3);
  // Block the next cell after planning: the unit searches again from where it is.
  EntityId f = w.spawn({2, 10});
  mv.request_walk(f, {6, 10});
  w.grid.set_terrain(4, 10, 0);
  w.run(100);  // (3,10) fine
  w.run(100);  // (4,10) now a wall: replan, step is delayed by one speed from now
  CHECK(w.reg.get<Position>(f)->x == 3);
  w.run(1000);
  CHECK(w.reg.get<Position>(f)->x == 6 && w.reg.get<Position>(f)->y == 10);
}

TEST("EC-MOVE-008", hook_can_stop_walk_destroyed_entity_is_dropped_and_unreachable_replan_ends_walk) {
  World w;
  MoveHooks h;
  MovementSystem* self = nullptr;
  int calls = 0;
  h.moved = [&](EntityId e, Point, Point to) {
    ++calls;
    if (to.x == 4) self->stop(e);  // hook stops the walk: no further timer
  };
  MovementSystem mv(w.reg, w.grid, w.spatial, w.timers, h);
  self = &mv;
  EntityId e = w.spawn({2, 2});
  mv.request_walk(e, {9, 2});
  w.run_for(2000);
  CHECK(w.reg.get<Position>(e)->x == 4 && calls == 2 && !mv.walking(e));
  CHECK(w.timers.pending() == 0);

  EntityId d = w.spawn({2, 6});
  mv.request_walk(d, {9, 6});
  w.run(100);
  w.reg.destroy(d);
  w.run_for(2000);
  CHECK(!mv.walking(d));
  CHECK(w.timers.pending() == 0);

  EntityId u = w.spawn({2, 12});
  mv.request_walk(u, {9, 12});
  for (int y = 0; y < 40; ++y) w.grid.set_terrain(5, y, 0);  // wall the corridor off
  w.run_for(2000);
  CHECK(w.reg.get<Position>(u)->x == 4 && !mv.walking(u));  // replan failed: stopped in place
}

TEST("EC-MOVE-009", request_to_current_cell_is_accepted_noop_and_dead_entity_rejected) {
  World w;
  MovementSystem mv(w.reg, w.grid, w.spatial, w.timers);
  EntityId e = w.spawn({3, 3});
  CHECK(mv.request_walk(e, {3, 3}).accepted);
  CHECK(!mv.walking(e) && w.timers.pending() == 0);
  w.reg.destroy(e);
  CHECK(!mv.request_walk(e, {5, 5}).accepted);
}

TEST("EC-MOVE-010", destroying_the_system_cancels_its_timers) {
  World w;
  EntityId e = w.spawn({2, 2});
  {
    MovementSystem mv(w.reg, w.grid, w.spatial, w.timers);
    mv.request_walk(e, {9, 2});
    CHECK(w.timers.pending() == 1);
  }
  CHECK(w.timers.pending() == 0);
}

// ---- instances ------------------------------------------------------------

TEST("EC-INST-005", ttl_zero_means_no_expiry_ids_not_reused_and_scoped_timer_fires_once) {
  VirtualClock c;
  TimerQueue q(c);
  InstanceManager m(q);
  InstanceId a = m.create(1, {1, 1}, 0);
  CHECK(q.pending() == 0);  // no TTL timer
  int fired = 0;
  TimerId t = m.add_scoped_timer(a, 100, [&](TimerId, Tick, Tick) { ++fired; });
  CHECK(t != kNoTimer);
  c.advance(100); q.run_due();
  CHECK(fired == 1 && m.exists(a));
  CHECK(m.destroy(a));
  CHECK(!m.destroy(a));
  InstanceId b = m.create(1, {1, 1}, 10);
  CHECK(b != a);
  CHECK(m.add_scoped_timer(a, 5, [](TimerId, Tick, Tick) {}) == kNoTimer);
  CHECK(m.owner_of(b) == InstanceOwner({1, 1}));
}

TEST("EC-INST-006", owner_left_cancels_ttl_and_scoped_timers_and_manager_cleans_up) {
  VirtualClock c;
  TimerQueue q(c);
  int fired = 0;
  {
    InstanceManager m(q);
    InstanceId a = m.create(1, {2, 9}, 500);
    m.add_scoped_timer(a, 100, [&](TimerId, Tick, Tick) { ++fired; });
    m.add_scoped_timer(a, 200, [&](TimerId, Tick, Tick) { ++fired; });
    CHECK(q.pending() == 3);
    m.owner_left({2, 9});
    CHECK(q.pending() == 0 && !m.exists(a));
    m.owner_left({2, 9});  // idempotent
    m.create(1, {3, 3}, 500);
    CHECK(q.pending() == 1);
  }  // manager destroyed with a live instance: its timer must not outlive it
  CHECK(q.pending() == 0);
  c.advance(1000); q.run_due();
  CHECK(fired == 0);
}

TEST("EC-MOVE-011", step_chain_restarts_from_now_after_a_stall_over_one_second) {
  World w;
  MovementSystem mv(w.reg, w.grid, w.spatial, w.timers);
  EntityId e = w.spawn({2, 2});
  mv.request_walk(e, {9, 2});
  w.run(2000);  // first step was 1900 ms late: the timer reports `now`, chain restarts there
  CHECK(w.reg.get<Position>(e)->x == 3);
  w.run(99);
  CHECK(w.reg.get<Position>(e)->x == 3);
  w.run(1);
  CHECK(w.reg.get<Position>(e)->x == 4);
}
