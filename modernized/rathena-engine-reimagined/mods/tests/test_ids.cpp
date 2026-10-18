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

TEST("MOD-ID-003", explicit_client_id_is_preserved) {
  auto m = fresh();
  m.claim(nid("legacy:skill/sm_bash"), 5);
  CHECK(m.client_id_of(nid("legacy:skill/sm_bash")) == 5u);
  CHECK(m.id_of("skill", 5)->str() == "legacy:skill/sm_bash");
}

TEST("MOD-ID-004", explicit_collision_is_load_error_naming_both_mods) {
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

TEST("MOD-ID-005", allocation_is_idempotent_and_in_reserved_range) {
  auto m = fresh();
  auto a = m.allocate(nid("x:skill/a"));
  CHECK(a >= 10000 && a <= 10999);
  CHECK(m.allocate(nid("x:skill/a")) == a);
  CHECK(m.allocate(nid("x:skill/b")) != a);
}

TEST("MOD-ID-006", allocation_stable_across_restart) {
  MemStore store;
  auto m1 = fresh();
  auto a = m1.allocate(nid("x:item/a"));
  m1.save(store);
  auto m2 = fresh();
  m2.load(store);
  CHECK(m2.client_id_of(nid("x:item/a")) == a);
  CHECK(m2.allocate(nid("x:item/b")) != a);
}

TEST("MOD-ID-007", removed_mod_numbers_are_tombstoned_forever) {
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

TEST("MOD-ID-008", exhausted_range_is_load_error) {
  IdMap m({{"skill", {1, 2}}});
  m.allocate(nid("x:skill/a"));
  m.allocate(nid("x:skill/b"));
  try { m.allocate(nid("x:skill/c")); }
  catch (const NotImplemented&) { CHECK(false); }
  catch (const LoadError& e) { CHECK(e.code() == "id.exhausted"); return; }
  CHECK(false);
}

TEST("MOD-ID-009", different_mod_set_cannot_repoint_a_string_id) {
  // Save data stores the string id. After removing and re-adding a mod, the
  // string id resolves to a NEW number, never to another definition's number.
  auto m = fresh();
  auto a = m.allocate(nid("m1:item/sword"));
  m.tombstone_mod("m1");
  auto b = m.allocate(nid("m2:item/sword"));
  CHECK(a != b);
  CHECK(!m.id_of("item", a).has_value());
}

// ---- added with the implementation ---------------------------------------------
#include <filesystem>

#include "mods/resolver.hpp"

template <class F>
static LoadError expect_load_error(F f, const std::string& code) {
  try { f(); }
  catch (const LoadError& e) {
    if (e.code() != code) throw CheckFailure("expected " + code + " got " + e.code() + ": " + e.what());
    return e;
  }
  throw CheckFailure("expected LoadError " + code);
}

TEST("MOD-ID-010", claim_is_idempotent_and_a_live_id_never_changes_number) {
  auto m = fresh();
  m.claim(nid("a:skill/x"), 7);
  m.claim(nid("a:skill/x"), 7);
  CHECK(m.live_count() == 1);
  auto e = expect_load_error([&] { m.claim(nid("a:skill/x"), 8); }, "id.client_id_changed");
  CHECK(std::string(e.what()).find("7") != std::string::npos && std::string(e.what()).find("8") != std::string::npos);
  CHECK(m.client_id_of(nid("a:skill/x")) == 7u);
  // an allocated id cannot silently become an explicit one either
  auto n = m.allocate(nid("a:skill/y"));
  expect_load_error([&] { m.claim(nid("a:skill/y"), n + 1); }, "id.client_id_changed");
  // the same number in a different kind is not a collision
  m.claim(nid("a:item/x"), 7);
}

TEST("MOD-ID-011", collision_with_a_tombstone_names_the_former_owner_and_the_new_claimant) {
  auto m = fresh();
  m.claim(nid("gone:skill/old"), 12);
  m.tombstone_mod("gone");
  auto e = expect_load_error([&] { m.claim(nid("new.mod:skill/n"), 12); }, "id.collision");
  std::string w = e.what();
  CHECK(w.find("gone:skill/old") != std::string::npos && w.find("new.mod") != std::string::npos && w.find("tombstoned") != std::string::npos);
  auto e2 = expect_load_error([&] { m.claim(nid("x.mod:skill/q"), 20), m.claim(nid("y.mod:skill/q"), 20); }, "id.collision");
  w = e2.what();
  CHECK(w.find("'x.mod'") != std::string::npos && w.find("'y.mod'") != std::string::npos);
}

TEST("MOD-ID-012", allocation_takes_the_lowest_free_number_and_skips_explicit_claims) {
  IdMap m({{"skill", {100, 110}}});
  m.claim(nid("legacy:skill/a"), 101);
  m.claim(nid("legacy:skill/b"), 103);
  CHECK(m.allocate(nid("x:skill/1")) == 100u);
  CHECK(m.allocate(nid("x:skill/2")) == 102u);
  CHECK(m.allocate(nid("x:skill/3")) == 104u);
  m.tombstone_mod("x");
  CHECK(m.allocate(nid("y:skill/1")) == 105u);  // 100, 102, 104 are tombstones now
  CHECK(m.tombstone_count() == 3);
  auto e = expect_load_error([&] { m.allocate(nid("z:other/1")); }, "id.no_range");
  CHECK(std::string(e.what()).find("other") != std::string::npos);
}

TEST("MOD-ID-013", store_round_trip_keeps_tombstones_and_rejects_corrupt_tables) {
  MemoryIdMapStore store;
  auto m = fresh();
  m.claim(nid("a:item/sword"), 1201);
  m.allocate(nid("b:skill/s"));
  m.tombstone_mod("a");
  m.save(store);
  CHECK(store.saves() == 1);
  auto rows = store.load();
  CHECK(rows.size() == 2);
  bool saw = false;
  for (auto& r : rows) if (r.kind == "item") { saw = true; CHECK(!r.id.has_value() && r.was_id == std::string("a:item/sword")); }
  CHECK(saw);
  auto m2 = fresh();
  m2.load(store);
  CHECK(m2.is_tombstoned("item", 1201) && m2.client_id_of(nid("b:skill/s")).has_value());
  MemoryIdMapStore bad;
  bad.save({{"skill", 5, std::nullopt, std::string("a:skill/z")}, {"skill", 5, std::string("a:skill/y"), std::nullopt}});
  expect_load_error([&] { fresh().load(bad); }, "id_map.corrupt");   // tombstone and live row on one number
  bad.save({{"skill", 5, std::nullopt, std::nullopt}, {"skill", 5, std::nullopt, std::nullopt}});
  expect_load_error([&] { fresh().load(bad); }, "id_map.corrupt");
  bad.save({{"skill", 5, std::string("a:skill/x"), std::nullopt}, {"skill", 6, std::string("a:skill/x"), std::nullopt}});
  expect_load_error([&] { fresh().load(bad); }, "id_map.corrupt");
  bad.save({{"skill", 5, std::string("not an id"), std::nullopt}});
  expect_load_error([&] { fresh().load(bad); }, "id_map.corrupt");
  bad.save({{"item", 5, std::string("a:skill/x"), std::nullopt}});  // kind in the id disagrees with the row
  expect_load_error([&] { fresh().load(bad); }, "id_map.corrupt");
}

TEST("MOD-ID-014", thirty_thousand_allocations_are_fast_and_unique) {
  IdMap m({{"item", {1000000, 1999999}}});
  std::set<std::uint32_t> seen;
  for (int i = 0; i < 30000; ++i) {
    auto n = m.allocate(NamespacedId{"bulk", "item", "i" + std::to_string(i)});
    CHECK(seen.insert(n).second);
  }
  CHECK(m.live_count() == 30000 && *seen.begin() == 1000000u && *seen.rbegin() == 1029999u);
}

TEST("MOD-ID-015", range_registry_detects_overlaps_between_mods_that_can_coexist) {
  std::filesystem::path root = MODS_SOURCE_DIR;
  auto reg = RangeRegistry::load_file(root / "config" / "id_ranges.example.toml");
  CHECK(!reg.owned().empty() && reg.allocation().count("skill") == 1);
  Manifest pre, re;
  pre.id = "ruleset.prerenewal"; pre.conflicts = {"ruleset.renewal"};
  re.id = "ruleset.renewal"; re.conflicts = {"ruleset.prerenewal"};
  reg.validate({pre, re});  // both rulesets keep the legacy numbers: allowed, they exclude each other
  auto e = expect_load_error([&] { reg.validate({}); }, "range.overlap");  // without the conflict info it is an error
  CHECK(std::string(e.what()).find("ruleset.prerenewal") != std::string::npos && std::string(e.what()).find("ruleset.renewal") != std::string::npos);
  // an unrelated mod inside a ruleset's range is always an error
  auto bad = reg;
  bad.add_owned({"some.shared.mod", "skill", 100, 200});
  Manifest shared;
  shared.id = "some.shared.mod";
  auto e2 = expect_load_error([&] { bad.validate({pre, re, shared}); }, "range.overlap");
  CHECK(std::string(e2.what()).find("some.shared.mod") != std::string::npos);
  // allocation range overlapping an owned range
  auto bad2 = reg;
  bad2.set_allocation("skill", {5000, 20000});
  auto e3 = expect_load_error([&] { bad2.validate({pre, re}); }, "range.overlap");
  CHECK(std::string(e3.what()).find("allocation range") != std::string::npos);
  // claims
  reg.check_claim(nid("ruleset.prerenewal:skill/sm_bash"), 5);
  expect_load_error([&] { reg.check_claim(nid("ruleset.prerenewal:skill/x"), 123456); }, "range.outside_owner");
  expect_load_error([&] { reg.check_claim(nid("unlisted:skill/x"), 5); }, "range.outside_owner");
}

TEST("MOD-ID-016", range_registry_file_validation) {
  auto code = [](const std::string& t) -> std::string {
    try { RangeRegistry::parse(t, "r.toml"); } catch (const LoadError& e) { return e.code(); }
    return "";
  };
  CHECK(code("[[range]]\nowner = \"a\"\nkind = \"skill\"\nfirst = 5\nlast = 1\n") == "range.bad_value");
  CHECK(code("[[range]]\nowner = \"a\"\nkind = \"skill\"\nfirst = 5\n") == "range.missing_key");
  CHECK(code("[allocation]\nskill = [1]\n") == "range.bad_type");
  CHECK(code("[allocation]\nskill = [-1, 5]\n") == "range.bad_value");
  CHECK(code("bogus = 1\n") == "range.unknown_key");
  CHECK(code("[[range]]\nowner = \"A\"\nkind = \"s\"\nfirst = 1\nlast = 2\n") == "range.bad_value");
  CHECK(code("[[range\n") == "range.syntax");
  CHECK(code("[allocation]\nskill = [1, 2]\n") == "");
}

TEST("MOD-ID-017", exclusive_mods_may_share_explicit_numbers_and_the_wire_lookup_needs_the_active_set) {
  auto m = fresh();
  m.set_exclusivity([](const std::string& a, const std::string& b) {
    return (a == "ruleset.pre" && b == "ruleset.re") || (a == "ruleset.re" && b == "ruleset.pre");
  });
  m.claim(nid("ruleset.pre:skill/sm_bash"), 5);
  m.claim(nid("ruleset.re:skill/sm_bash"), 5);       // allowed: never enabled together
  CHECK(!m.id_of("skill", 5).has_value());            // ambiguous without the active set
  CHECK(m.id_of("skill", 5, {"ruleset.re"})->str() == "ruleset.re:skill/sm_bash");
  CHECK(m.id_of("skill", 5, {"ruleset.pre"})->str() == "ruleset.pre:skill/sm_bash");
  CHECK(!m.id_of("skill", 5, {"other"}).has_value());
  auto e = expect_load_error([&] { m.claim(nid("third:skill/x"), 5); }, "id.collision");  // not exclusive with either
  CHECK(std::string(e.what()).find("third") != std::string::npos);
  expect_load_error([&] { m.claim(nid("ruleset.re:skill/other"), 5); }, "id.collision");  // same mod, two definitions
  // allocation never hands out a shared number
  CHECK(m.allocate(nid("x:skill/a")) != 5u);
  // persistence keeps both rows
  MemStore store;
  m.save(store);
  auto m2 = fresh();
  m2.load(store);
  CHECK(m2.id_of("skill", 5, {"ruleset.re"}).has_value() && m2.id_of("skill", 5, {"ruleset.pre"}).has_value());
  // removing one owner keeps the number alive for the other; removing both tombstones it
  m.tombstone_mod("ruleset.re");
  CHECK(!m.is_tombstoned("skill", 5) && m.id_of("skill", 5)->str() == "ruleset.pre:skill/sm_bash");
  m.tombstone_mod("ruleset.pre");
  CHECK(m.is_tombstoned("skill", 5));
}
