#include "engine/ecs/registry.hpp"
#include "engine/ecs/schema.hpp"
#include "harness.hpp"

using namespace engine;

TEST("EC-ECS-001", stale_handle_never_aliases_reused_index) {
  EntityAllocator a;
  EntityId e1 = a.create();
  CHECK(a.alive(e1));
  CHECK(a.destroy(e1));
  CHECK(!a.alive(e1));
  EntityId e2 = a.create();
  CHECK(e2.index == e1.index);
  CHECK(e2.generation != e1.generation);
  CHECK(a.alive(e2));
  CHECK(!a.alive(e1));
  CHECK(!a.destroy(e1));
}

TEST("EC-ECS-002", sparse_set_add_get_remove_with_swap_remove) {
  SparseSet<Position> s;
  EntityAllocator a;
  EntityId x = a.create(), y = a.create(), z = a.create();
  s.emplace(x, Position{1, 1});
  s.emplace(y, Position{2, 2});
  s.emplace(z, Position{3, 3});
  CHECK(s.size() == 3);
  CHECK(s.remove_entity(x));
  CHECK(!s.contains(x));
  CHECK(s.size() == 2);
  CHECK(s.get(z) && s.get(z)->x == 3);  // moved element still addressable
  CHECK(s.get(y) && s.get(y)->x == 2);
  CHECK(!s.remove_entity(x));
}

TEST("EC-ECS-002", component_access_checks_generation) {
  Registry r;
  EntityId e = r.create();
  r.emplace<Position>(e, Position{5, 6});
  CHECK(r.get<Position>(e)->y == 6);
  r.destroy(e);
  EntityId e2 = r.create();
  CHECK(r.get<Position>(e) == nullptr);
  CHECK(r.get<Position>(e2) == nullptr);
}

TEST("EC-ECS-004", destroy_strips_all_components) {
  Registry r;
  EntityId e = r.create();
  r.emplace<Position>(e, Position{1, 2});
  r.emplace<Facing>(e, Facing{3});
  CHECK(r.destroy(e));
  CHECK(r.pool<Position>().size() == 0);
  CHECK(r.pool<Facing>().size() == 0);
}

PENDING_TEST("EC-ECS-003", deferred_destroy_applies_at_flush) {
  Registry r;
  EntityId e = r.create();
  r.defer_destroy(e);
  CHECK(r.alive(e));  // still alive while iterating
  r.flush_deferred();
  CHECK(!r.alive(e));
}

PENDING_TEST("EC-ECS-005", script_declared_schema_roundtrip) {
  Registry r;
  EntityId e = r.create();
  SchemaStore s;
  SchemaId id = s.register_schema("testmod:comp/example",
                                  {{"level", FieldType::Int}, {"tag", FieldType::String}});
  s.set(e, id, "level", int64_t{7});
  CHECK(std::get<int64_t>(s.get(e, id, "level")) == 7);
  bool threw = false;
  try { s.set(e, id, "level", std::string("x")); } catch (const std::exception&) { threw = true; }
  CHECK(threw);  // type mismatch rejected
}
