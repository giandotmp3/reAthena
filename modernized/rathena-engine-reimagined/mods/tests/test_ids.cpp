// Namespaced ids, client_id handling, tombstoned id_map. Tags MOD-ID-*.
#include <memory>

#include "harness.hpp"
#include "mods/id_map.hpp"

using namespace mods;

static NamespacedId nid(const char* s) { return *parse_namespaced_id(s); }
static IdMap fresh() { return IdMap({{"skill", {10000, 10999}}, {"item", {50000, 50999}}}); }

struct MemStore : IdMapStore {
  std::vector<IdMapRow> rows;
  std::vector<IdMapRow> load() override { return rows; }
  void save(const std::vector<IdMapRow>& r) override { rows = r; }
};

TEST("MOD-ID-001", namespaced_id_parses_and_round_trips) {
  auto id = parse_namespaced_id("ruleset.renewal:skill/sm_bash");
  CHECK(id.has_value());
  CHECK(id->mod == "ruleset.renewal" && id->kind == "skill" && id->name == "sm_bash");
  CHECK(id->str() == "ruleset.renewal:skill/sm_bash");
}

TEST("MOD-ID-002", malformed_namespaced_ids_rejected) {
  for (const char* bad : {"", "nomod", "a:b", "a:/c", ":k/n", "A:skill/x", "a:Skill/x",
                          "a:skill/", "a:skill/../x", "a b:skill/x", "a:1skill/x"})
    CHECK(!parse_namespaced_id(bad).has_value());
}

PENDING_TEST("MOD-ID-003", explicit_client_id_is_preserved) {
  auto m = fresh();
  m.claim(nid("legacy:skill/sm_bash"), 5);
  CHECK(m.client_id_of(nid("legacy:skill/sm_bash")) == 5u);
  CHECK(m.id_of("skill", 5)->str() == "legacy:skill/sm_bash");
}

PENDING_TEST("MOD-ID-004", explicit_collision_is_load_error_naming_both_mods) {
  auto m = fresh();
  m.claim(nid("mod.one:skill/a"), 5);
  try { m.claim(nid("mod.two:skill/b"), 5); }
  catch (const NotImplemented&) { CHECK(false); }
  catch (const LoadError& e) {
    std::string w = e.what();
    CHECK(e.code() == "id.collision");
    CHECK(w.find("mod.one") != std::string::npos && w.find("mod.two") != std::string::npos);
    return;
  }
  CHECK(false);
}

PENDING_TEST("MOD-ID-005", allocation_is_idempotent_and_in_reserved_range) {
  auto m = fresh();
  auto a = m.allocate(nid("x:skill/a"));
  CHECK(a >= 10000 && a <= 10999);
  CHECK(m.allocate(nid("x:skill/a")) == a);
  CHECK(m.allocate(nid("x:skill/b")) != a);
}

PENDING_TEST("MOD-ID-006", allocation_stable_across_restart) {
  MemStore store;
  auto m1 = fresh();
  auto a = m1.allocate(nid("x:item/a"));
  m1.save(store);
  auto m2 = fresh();
  m2.load(store);
  CHECK(m2.client_id_of(nid("x:item/a")) == a);
  CHECK(m2.allocate(nid("x:item/b")) != a);
}

PENDING_TEST("MOD-ID-007", removed_mod_numbers_are_tombstoned_forever) {
  MemStore store;
  auto m = fresh();
  auto a = m.allocate(nid("gone:skill/a"));
  m.tombstone_mod("gone");
  CHECK(m.is_tombstoned("skill", a));
  CHECK(!m.id_of("skill", a).has_value());
  CHECK(m.allocate(nid("new:skill/a")) != a);        // never reassigned
  m.save(store);
  auto m2 = fresh();
  m2.load(store);
  CHECK(m2.is_tombstoned("skill", a));                // survives restart
  bool refused = false;                               // explicit claim of tombstone refused
  try { m2.claim(nid("new:skill/b"), a); } catch (const LoadError&) { refused = true; }
  CHECK(refused);
}

PENDING_TEST("MOD-ID-008", exhausted_range_is_load_error) {
  IdMap m({{"skill", {1, 2}}});
  m.allocate(nid("x:skill/a"));
  m.allocate(nid("x:skill/b"));
  try { m.allocate(nid("x:skill/c")); }
  catch (const NotImplemented&) { CHECK(false); }
  catch (const LoadError& e) { CHECK(e.code() == "id.exhausted"); return; }
  CHECK(false);
}

PENDING_TEST("MOD-ID-009", different_mod_set_cannot_repoint_a_string_id) {
  // Save data stores the string id. After removing and re-adding a mod, the
  // string id resolves to a NEW number, never to another definition's number.
  auto m = fresh();
  auto a = m.allocate(nid("m1:item/sword"));
  m.tombstone_mod("m1");
  auto b = m.allocate(nid("m2:item/sword"));
  CHECK(a != b);
  CHECK(!m.id_of("item", a).has_value());
}
