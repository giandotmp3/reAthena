// Dependency and ordering resolution. Tags MOD-DEP-*.
#include <algorithm>

#include "harness.hpp"
#include "mods/resolver.hpp"

using namespace mods;

static Manifest mk(std::string id) {
  Manifest m;
  m.id = std::move(id);
  m.version = {1, 0, 0};
  m.engine_api = "^1";
  return m;
}

template <class F>
static std::string expect_load_error(F f, const std::string& code) {
  try { f(); }
  catch (const NotImplemented&) { throw CheckFailure("not implemented"); }
  catch (const LoadError& e) {
    CHECK(e.code() == code);
    return e.what();
  }
  throw CheckFailure("expected LoadError " + code);
}

static long idx(const std::vector<Manifest>& v, const std::string& id) {
  for (size_t i = 0; i < v.size(); ++i) if (v[i].id == id) return static_cast<long>(i);
  return -1;
}

TEST("MOD-DEP-001", dependencies_load_before_dependents) {
  auto a = mk("a"), b = mk("b");
  b.depends["a"] = "^1";
  auto order = resolve_load_order({b, a});
  CHECK(idx(order, "a") < idx(order, "b"));
}

TEST("MOD-DEP-002", missing_dependency_names_both_mods) {
  auto b = mk("b");
  b.depends["ghost"] = "^1";
  auto msg = expect_load_error([&] { resolve_load_order({b}); }, "resolve.missing_dependency");
  CHECK(msg.find("b") != std::string::npos && msg.find("ghost") != std::string::npos);
}

TEST("MOD-DEP-003", unsatisfied_version_range_is_error) {
  auto a = mk("a"), b = mk("b");
  a.version = {2, 0, 0};
  b.depends["a"] = "^1";
  expect_load_error([&] { resolve_load_order({a, b}); }, "resolve.version_mismatch");
}

TEST("MOD-DEP-004", declared_conflict_is_error) {
  auto a = mk("a"), b = mk("b");
  a.conflicts.push_back("b");
  expect_load_error([&] { resolve_load_order({a, b}); }, "resolve.conflict");
}

TEST("MOD-DEP-005", cycle_is_error_listing_the_path) {
  auto a = mk("a"), b = mk("b"), c = mk("c");
  a.depends["b"] = "^1";
  b.depends["c"] = "^1";
  c.depends["a"] = "^1";
  auto msg = expect_load_error([&] { resolve_load_order({a, b, c}); }, "resolve.cycle");
  for (const char* id : {"a", "b", "c"}) CHECK(msg.find(id) != std::string::npos);
}

TEST("MOD-DEP-006", load_after_and_before_are_honored) {
  auto a = mk("a"), b = mk("b"), c = mk("c");
  a.load_after.push_back("c");
  b.load_before.push_back("c");
  auto order = resolve_load_order({a, b, c});
  CHECK(idx(order, "c") < idx(order, "a"));
  CHECK(idx(order, "b") < idx(order, "c"));
}

TEST("MOD-DEP-007", load_after_contradicting_depends_is_cycle) {
  auto a = mk("a"), b = mk("b");
  a.depends["b"] = "^1";
  b.load_after.push_back("a");
  expect_load_error([&] { resolve_load_order({a, b}); }, "resolve.cycle");
}

TEST("MOD-DEP-008", two_ruleset_providers_are_exclusive) {
  auto r1 = mk("ruleset.renewal"), r2 = mk("ruleset.prerenewal");
  r1.provides = {"ruleset"};
  r2.provides = {"ruleset"};
  auto msg = expect_load_error([&] { resolve_load_order({r1, r2}); }, "resolve.exclusive_provider");
  CHECK(msg.find("ruleset.renewal") != std::string::npos);
  CHECK(msg.find("ruleset.prerenewal") != std::string::npos);
}

TEST("MOD-DEP-009", engine_api_outside_range_refused) {
  auto a = mk("a");
  a.engine_api = "^2";
  expect_load_error([&] { resolve_load_order({a}); }, "resolve.engine_api");
}

TEST("MOD-DEP-010", order_is_deterministic_regardless_of_input_order) {
  auto a = mk("a"), b = mk("b"), c = mk("c");
  auto o1 = resolve_load_order({a, b, c});
  auto o2 = resolve_load_order({c, b, a});
  CHECK(o1.size() == 3 && o2.size() == 3);
  for (size_t i = 0; i < 3; ++i) CHECK(o1[i].id == o2[i].id);
}

// ---- added with the implementation ---------------------------------------------
#include <functional>
#include <random>

static std::string msg_of(const std::function<void()>& f, const std::string& code) {
  try { f(); }
  catch (const LoadError& e) {
    if (e.code() != code) throw CheckFailure("expected " + code + " got " + e.code() + ": " + e.what());
    return e.what();
  }
  throw CheckFailure("expected LoadError " + code);
}

TEST("MOD-DEP-011", cycle_message_lists_the_full_path_and_why_each_edge_exists) {
  auto a = mk("a"), b = mk("b"), c = mk("c");
  a.depends["b"] = "^1";     // b loads before a
  b.load_after = {"c"};      // c loads before b
  c.load_before = {"a"};     // c loads before a  (not part of the cycle)
  c.depends["a"] = "^1";     // a loads before c
  auto m = msg_of([&] { resolve_load_order({a, b, c}); }, "resolve.cycle");
  CHECK(m == "load order cycle: a -> c -> b -> a (each mod must load before the next: "
             "'c' depends on 'a'; 'b' load_after 'c'; 'a' depends on 'b')");
}

TEST("MOD-DEP-012", ties_break_by_id_and_diamonds_resolve) {
  auto a = mk("a"), b = mk("b"), c = mk("c"), d = mk("d"), z = mk("z");
  b.depends["a"] = "^1"; c.depends["a"] = "^1"; d.depends["b"] = "^1"; d.depends["c"] = "^1";
  auto o = resolve_load_order({d, c, z, b, a});
  std::string ids;
  for (auto& m : o) ids += m.id;
  CHECK(ids == "abcdz");
}

TEST("MOD-DEP-013", conflict_can_name_a_provided_capability_and_message_names_both) {
  auto a = mk("a"), b = mk("b");
  a.conflicts = {"cap.x"};
  b.provides = {"cap.x"};
  auto m = msg_of([&] { resolve_load_order({a, b}); }, "resolve.conflict");
  CHECK(m.find("'a'") != std::string::npos && m.find("'b'") != std::string::npos && m.find("cap.x") != std::string::npos);
  CHECK(mutually_exclusive(a, b) && mutually_exclusive(b, a) && !mutually_exclusive(a, mk("c")));
}

TEST("MOD-DEP-014", required_provider_and_exactly_one_ruleset) {
  ResolveOptions o;
  o.required_provides = {"ruleset"};
  auto a = mk("a");
  msg_of([&] { resolve_load_order({a}, o); }, "resolve.missing_provider");
  auto r = mk("ruleset.x");
  r.provides = {"ruleset"};
  CHECK(resolve_load_order({a, r}, o).size() == 2);
  auto r2 = mk("ruleset.y");
  r2.provides = {"ruleset"};
  auto m = msg_of([&] { resolve_load_order({a, r, r2}, o); }, "resolve.exclusive_provider");
  CHECK(m.find("exactly one provider but 2") != std::string::npos);
  // a capability not listed as exclusive may have several providers
  r.provides = {"free"}; r2.provides = {"free"};
  CHECK(resolve_load_order({r, r2}).size() == 2);
}

TEST("MOD-DEP-015", duplicate_ids_and_messages_for_version_mismatch) {
  auto a = mk("a"), a2 = mk("a");
  a2.version = {1, 1, 0};
  auto m = msg_of([&] { resolve_load_order({a, a2}); }, "resolve.duplicate_id");
  CHECK(m.find("1.0.0") != std::string::npos && m.find("1.1.0") != std::string::npos);
  auto b = mk("b");
  b.depends["a"] = "^2";
  m = msg_of([&] { resolve_load_order({a, b}); }, "resolve.version_mismatch");
  CHECK(m == "mod 'b' requires 'a' ^2 but 'a' 1.0.0 is installed");
  m = msg_of([&] { auto c = mk("c"); c.engine_api = ">=0.1, <1"; resolve_load_order({c}); }, "resolve.engine_api");
  CHECK(m.find("'c'") != std::string::npos && m.find("\">=0.1, <1\"") != std::string::npos && m.find("1.0.0") != std::string::npos);
  ResolveOptions old;
  old.engine_api_version = {0, 5, 0};
  auto c = mk("c");
  c.engine_api = ">=0.1, <1";
  CHECK(resolve_load_order({c}, old).size() == 1);
}

TEST("MOD-DEP-016", load_after_naming_an_absent_mod_is_ignored) {
  auto a = mk("a");
  a.load_after = {"ghost"};
  a.load_before = {"ghost2"};
  CHECK(resolve_load_order({a}).size() == 1);
}

static Manifest ver(std::string id, SemVer v) { auto m = mk(std::move(id)); m.version = v; return m; }

TEST("MOD-DEP-017", select_versions_picks_highest_satisfying_and_backtracks) {
  auto app = ver("app", {1, 0, 0});
  app.depends["lib"] = "^1";
  app.depends["plug"] = "^1";
  auto lib1 = ver("lib", {1, 0, 0}), lib12 = ver("lib", {1, 2, 0}), lib2 = ver("lib", {2, 0, 0});
  auto plug_new = ver("plug", {1, 5, 0}), plug_old = ver("plug", {1, 1, 0});
  plug_new.depends["lib"] = "^1.3";   // dead end: no lib >= 1.3 below 2
  plug_old.depends["lib"] = "^1.0";
  auto sel = select_versions({app, lib1, lib12, lib2, plug_new, plug_old}, {"app"});
  std::string got;
  for (auto& m : sel) got += m.id + "@" + to_string(m.version) + " ";
  CHECK(got == "app@1.0.0 lib@1.2.0 plug@1.1.0 ");
  // when everything fits the highest wins
  auto sel2 = select_versions({app, lib1, lib12, plug_old}, {"app"});
  CHECK(sel2.size() == 3);
  for (auto& m : sel2) if (m.id == "lib") CHECK((m.version == SemVer{1, 2, 0}));
  // the selection resolves into a load order
  CHECK(resolve_load_order(sel).size() == 3);
}

TEST("MOD-DEP-018", select_versions_reports_the_failing_requirement) {
  auto app = ver("app", {1, 0, 0});
  app.depends["lib"] = "^3";
  auto lib = ver("lib", {1, 0, 0});
  auto m = msg_of([&] { select_versions({app, lib}, {"app"}); }, "resolve.version_mismatch");
  CHECK(m.find("'app' requires 'lib' ^3") != std::string::npos);
  m = msg_of([&] { select_versions({app}, {"app"}); }, "resolve.missing_dependency");
  CHECK(m.find("'lib'") != std::string::npos);
  m = msg_of([&] { select_versions({app}, {"nope"}); }, "resolve.missing_dependency");
  CHECK(m.find("'nope'") != std::string::npos);
  auto r1 = mk("r1"), r2 = mk("r2");
  r1.provides = {"ruleset"}; r2.provides = {"ruleset"};
  msg_of([&] { select_versions({r1, r2}, {"r1", "r2"}); }, "resolve.exclusive_provider");
  auto x = mk("x"), y = mk("y");
  x.conflicts = {"y"};
  msg_of([&] { select_versions({x, y}, {"x", "y"}); }, "resolve.conflict");
}

TEST("MOD-DEP-019", random_graphs_obey_every_constraint_and_are_order_independent) {
  std::mt19937 rng(12345);
  for (int iter = 0; iter < 300; ++iter) {
    int n = 2 + static_cast<int>(rng() % 14);
    std::vector<Manifest> ms;
    for (int i = 0; i < n; ++i) ms.push_back(mk("m" + std::to_string(100 + static_cast<int>(rng() % 900)) + "_" + std::to_string(i)));
    // acyclic by construction: edges only from a lower rank to a higher rank
    std::vector<int> rank(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) rank[static_cast<std::size_t>(i)] = static_cast<int>(rng() % 1000);
    std::vector<std::pair<int, int>> edges;  // u before v
    for (int k = 0; k < n * 2; ++k) {
      int u = static_cast<int>(rng() % static_cast<unsigned>(n)), v = static_cast<int>(rng() % static_cast<unsigned>(n));
      if (u == v || rank[static_cast<std::size_t>(u)] >= rank[static_cast<std::size_t>(v)]) continue;
      auto& U = ms[static_cast<std::size_t>(u)];
      auto& V = ms[static_cast<std::size_t>(v)];
      auto has = [](const std::vector<std::string>& v, const std::string& x) { return std::find(v.begin(), v.end(), x) != v.end(); };
      switch (rng() % 3) {
        case 0:
          V.depends[U.id] = "^1";
          break;
        case 1:
          if (!has(V.load_after, U.id) && !has(V.load_before, U.id)) V.load_after.push_back(U.id);
          break;
        default:
          if (!has(U.load_before, V.id) && !has(U.load_after, V.id)) U.load_before.push_back(V.id);
          break;
      }
      edges.emplace_back(u, v);
    }
    auto o1 = resolve_load_order(ms);
    auto shuffled = ms;
    std::shuffle(shuffled.begin(), shuffled.end(), rng);
    auto o2 = resolve_load_order(shuffled);
    CHECK(o1.size() == ms.size() && o2.size() == ms.size());
    for (size_t i = 0; i < o1.size(); ++i) CHECK(o1[i].id == o2[i].id);
    for (auto [u, v] : edges)
      CHECK(idx(o1, ms[static_cast<std::size_t>(u)].id) < idx(o1, ms[static_cast<std::size_t>(v)].id));
  }
}
