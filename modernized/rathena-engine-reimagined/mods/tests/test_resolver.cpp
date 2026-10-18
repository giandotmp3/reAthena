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

PENDING_TEST("MOD-DEP-001", dependencies_load_before_dependents) {
  auto a = mk("a"), b = mk("b");
  b.depends["a"] = "^1";
  auto order = resolve_load_order({b, a});
  CHECK(idx(order, "a") < idx(order, "b"));
}

PENDING_TEST("MOD-DEP-002", missing_dependency_names_both_mods) {
  auto b = mk("b");
  b.depends["ghost"] = "^1";
  auto msg = expect_load_error([&] { resolve_load_order({b}); }, "resolve.missing_dependency");
  CHECK(msg.find("b") != std::string::npos && msg.find("ghost") != std::string::npos);
}

PENDING_TEST("MOD-DEP-003", unsatisfied_version_range_is_error) {
  auto a = mk("a"), b = mk("b");
  a.version = {2, 0, 0};
  b.depends["a"] = "^1";
  expect_load_error([&] { resolve_load_order({a, b}); }, "resolve.version_mismatch");
}

PENDING_TEST("MOD-DEP-004", declared_conflict_is_error) {
  auto a = mk("a"), b = mk("b");
  a.conflicts.push_back("b");
  expect_load_error([&] { resolve_load_order({a, b}); }, "resolve.conflict");
}

PENDING_TEST("MOD-DEP-005", cycle_is_error_listing_the_path) {
  auto a = mk("a"), b = mk("b"), c = mk("c");
  a.depends["b"] = "^1";
  b.depends["c"] = "^1";
  c.depends["a"] = "^1";
  auto msg = expect_load_error([&] { resolve_load_order({a, b, c}); }, "resolve.cycle");
  for (const char* id : {"a", "b", "c"}) CHECK(msg.find(id) != std::string::npos);
}

PENDING_TEST("MOD-DEP-006", load_after_and_before_are_honored) {
  auto a = mk("a"), b = mk("b"), c = mk("c");
  a.load_after.push_back("c");
  b.load_before.push_back("c");
  auto order = resolve_load_order({a, b, c});
  CHECK(idx(order, "c") < idx(order, "a"));
  CHECK(idx(order, "b") < idx(order, "c"));
}

PENDING_TEST("MOD-DEP-007", load_after_contradicting_depends_is_cycle) {
  auto a = mk("a"), b = mk("b");
  a.depends["b"] = "^1";
  b.load_after.push_back("a");
  expect_load_error([&] { resolve_load_order({a, b}); }, "resolve.cycle");
}

PENDING_TEST("MOD-DEP-008", two_ruleset_providers_are_exclusive) {
  auto r1 = mk("ruleset.renewal"), r2 = mk("ruleset.prerenewal");
  r1.provides = {"ruleset"};
  r2.provides = {"ruleset"};
  auto msg = expect_load_error([&] { resolve_load_order({r1, r2}); }, "resolve.exclusive_provider");
  CHECK(msg.find("ruleset.renewal") != std::string::npos);
  CHECK(msg.find("ruleset.prerenewal") != std::string::npos);
}

PENDING_TEST("MOD-DEP-009", engine_api_outside_range_refused) {
  auto a = mk("a");
  a.engine_api = "^2";
  expect_load_error([&] { resolve_load_order({a}); }, "resolve.engine_api");
}

PENDING_TEST("MOD-DEP-010", order_is_deterministic_regardless_of_input_order) {
  auto a = mk("a"), b = mk("b"), c = mk("c");
  auto o1 = resolve_load_order({a, b, c});
  auto o2 = resolve_load_order({c, b, a});
  CHECK(o1.size() == 3 && o2.size() == 3);
  for (size_t i = 0; i < 3; ++i) CHECK(o1[i].id == o2[i].id);
}
