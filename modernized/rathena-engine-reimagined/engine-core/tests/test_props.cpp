// Property tests (deterministic seeds, no external dependency).
#include <algorithm>
#include <cstdint>
#include <set>
#include <string>

#include "engine/attrs/attrs.hpp"
#include "engine/ecs/registry.hpp"
#include "engine/ecs/schema.hpp"
#include "engine/grid/grid.hpp"
#include "engine/path/path.hpp"
#include "harness.hpp"

using namespace engine;

namespace {
struct Rng {
  uint64_t s;
  explicit Rng(uint64_t seed) : s(seed * 6364136223846793005ull + 1442695040888963407ull) {}
  uint32_t next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return uint32_t(s >> 16); }
  int range(int lo, int hi) { return lo + int(next() % uint32_t(hi - lo + 1)); }
  bool chance(int pct) { return int(next() % 100) < pct; }
};
}  // namespace

TEST("EC-PATH-009", property_found_paths_are_walkable_contiguous_and_bounded) {
  Rng r(99);
  int found = 0;
  for (int m = 0; m < 1500; ++m) {
    int w = r.range(8, 70), h = r.range(8, 50);
    MapGrid g{static_cast<uint16_t>(w), static_cast<uint16_t>(h)};
    int wall_pct = r.range(0, 35);
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x)
        g.set_terrain(x, y, r.chance(wall_pct) ? 0 : (cell::Walkable | cell::Shootable));
    for (int q = 0; q < 5; ++q) {
      Point a{r.range(0, w - 1), r.range(0, h - 1)};
      Point b{std::clamp(a.x + r.range(-20, 20), 0, w - 1), std::clamp(a.y + r.range(-20, 20), 0, h - 1)};
      PathResult res = path_search(g, a, b);
      PathResult again = path_search(g, a, b);
      CHECK(res.found == again.found && res.steps == again.steps && res.cost == again.cost);
      if (!res.found) { CHECK(res.steps.empty()); continue; }
      ++found;
      CHECK(res.steps.size() <= 32);
      Point prev = a;
      int cost = 0;
      for (Point p : res.steps) {
        CHECK(g.walkable(p.x, p.y));
        int dx = p.x - prev.x, dy = p.y - prev.y;
        CHECK(std::abs(dx) <= 1 && std::abs(dy) <= 1 && (dx || dy));
        if (dx && dy) {  // no corner cutting
          CHECK(g.walkable(p.x, prev.y) && g.walkable(prev.x, p.y));
          cost += kCostDiagonal;
        } else {
          cost += kCostOrthogonal;
        }
        prev = p;
      }
      CHECK(prev == b);
      CHECK(cost == res.cost);
    }
  }
  CHECK(found > 2000);
}

TEST("EC-GRID-009", property_area_query_visits_each_object_once_in_pass_order) {
  Rng r(5);
  for (int round = 0; round < 200; ++round) {
    int w = r.range(8, 60), h = r.range(8, 60);
    MapGrid g{static_cast<uint16_t>(w), static_cast<uint16_t>(h)};
    SpatialIndex sp(g);
    EntityAllocator al;
    struct Obj { EntityId id; Point p; BucketClass c; bool live; };
    std::vector<Obj> objs;
    for (int i = 0; i < 80; ++i) {
      Obj o{al.create(), {r.range(0, w - 1), r.range(0, h - 1)}, r.chance(30) ? BucketClass::Secondary : BucketClass::Primary, true};
      CHECK(sp.insert(o.id, o.p, o.c));
      CHECK(!sp.insert(o.id, o.p, o.c));  // double insert refused
      objs.push_back(o);
    }
    for (int i = 0; i < 120; ++i) {  // random churn
      Obj& o = objs[size_t(r.range(0, int(objs.size()) - 1))];
      int op = r.range(0, 2);
      if (op == 0 && o.live) { CHECK(sp.remove(o.id)); o.live = false; }
      else if (op == 1 && o.live) { o.p = {r.range(0, w - 1), r.range(0, h - 1)}; CHECK(sp.move(o.id, o.p)); }
      else if (op == 2 && !o.live) { CHECK(sp.insert(o.id, o.p, o.c)); o.live = true; }
    }
    AreaQuery q = AreaQuery::around({r.range(-3, w + 2), r.range(-3, h + 2)}, r.range(0, 15));
    std::vector<EntityId> seen;
    sp.for_each_in_area(q, [&](EntityId e) { seen.push_back(e); });
    std::set<std::pair<uint32_t, uint32_t>> uniq;
    for (EntityId e : seen) uniq.insert({e.index, e.generation});
    CHECK(uniq.size() == seen.size());  // each object once
    size_t expect = 0;
    for (const Obj& o : objs)
      if (o.live && o.p.x >= q.x0 && o.p.x <= q.x1 && o.p.y >= q.y0 && o.p.y <= q.y1) {
        ++expect;
        CHECK(uniq.count({o.id.index, o.id.generation}) == 1);
      }
    CHECK(expect == seen.size());  // brute-force Chebyshev square
    // Primary pass strictly before Secondary pass.
    bool seen_secondary = false;
    for (EntityId e : seen) {
      auto it = std::find_if(objs.begin(), objs.end(), [&](const Obj& o) { return o.id == e; });
      if (it->c == BucketClass::Secondary) seen_secondary = true;
      else CHECK(!seen_secondary);
    }
    CHECK(sp.size() == size_t(std::count_if(objs.begin(), objs.end(), [](const Obj& o) { return o.live; })));
  }
}

TEST("EC-ECS-006", property_generation_safety_under_random_create_destroy) {
  Rng r(31);
  Registry reg;
  AttributeSystem attrs;
  SchemaStore schemas;
  AttrId a = attrs.intern("a");
  attrs.define_stages(a, {{"sum", [](int64_t acc, std::span<const Contribution> cs) {
                             for (auto& c : cs) acc += c.value;
                             return acc;
                           }}});
  SchemaId sid = schemas.register_schema("test:prop", {{"v", FieldType::Int}});
  std::vector<std::pair<EntityId, int>> live;  // handle, tag
  std::vector<EntityId> dead;
  for (int i = 0; i < 4000; ++i) {
    int op = r.range(0, 3);
    if (op <= 1 || live.empty()) {
      EntityId e = reg.create();
      int tag = i;
      CHECK(reg.alive(e));
      reg.emplace<Position>(e, Position{tag, tag});
      attrs.add_contribution(e, a, {1, "x", tag});
      schemas.set(e, sid, "v", int64_t{tag});
      live.push_back({e, tag});
    } else if (op == 2) {
      size_t k = size_t(r.range(0, int(live.size()) - 1));
      EntityId e = live[k].first;
      if (r.chance(50)) { reg.defer_destroy(e); CHECK(reg.alive(e)); reg.flush_deferred(); }
      else CHECK(reg.destroy(e));
      attrs.remove_entity(e);
      schemas.remove_entity(e);
      dead.push_back(e);
      live.erase(live.begin() + long(k));
    }
    // Every dead handle stays dead and never sees a successor's data.
    for (int k = 0; k < 5 && !dead.empty(); ++k) {
      EntityId d = dead[size_t(r.range(0, int(dead.size()) - 1))];
      CHECK(!reg.alive(d));
      CHECK(reg.get<Position>(d) == nullptr);
      CHECK(!reg.destroy(d));
      CHECK(attrs.get(d, a) == 0);
      CHECK(std::get<int64_t>(schemas.get(d, sid, "v")) == 0);
    }
    if (!live.empty()) {
      auto& [e, tag] = live[size_t(r.range(0, int(live.size()) - 1))];
      CHECK(reg.alive(e));
      CHECK(reg.get<Position>(e)->x == tag);
      CHECK(attrs.get(e, a) == tag);
      CHECK(std::get<int64_t>(schemas.get(e, sid, "v")) == tag);
    }
  }
  CHECK(reg.alive_count() == live.size());
}
