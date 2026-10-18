#include "fixtures.hpp"
#include "script_host/budget.hpp"
#include "script_host/sandbox.hpp"

using namespace script_host;

static ModEnvironmentSpec env(const std::string& mod, std::uint64_t cap = 1ull << 20) {
  ModEnvironmentSpec s;
  s.mod_id = mod;
  s.memory_cap_bytes = cap;
  return s;
}

TEST("CAP-E03", "step budget aborts the script with mod and hook ids") {
  BudgetLimits l;
  l.steps_per_dispatch = 10;
  BudgetMeter m(l);
  try {
    for (int i = 0; i < 100; ++i) m.step("m", "h");
    CHECK(false);
  } catch (const BudgetExceeded& e) {
    CHECK_EQ(e.mod, std::string("m"));
    CHECK_EQ(e.hook, std::string("h"));
  }
}

TEST("CAP-E03", "three strikes in the window auto-disable a hook") {
  BudgetMeter m{BudgetLimits{}};
  CHECK(!m.strike("m/h", 1));
  CHECK(!m.strike("m/h", 2));
  CHECK(m.strike("m/h", 3));
}

TEST("CAP-E03", "strikes outside the window do not accumulate") {
  BudgetMeter m{BudgetLimits{}};
  m.strike("m/h", 1);
  m.strike("m/h", 2);
  CHECK(!m.strike("m/h", 2 + m.limits().strike_window_ticks + 10));
}

TEST("CAP-E03", "per-mod memory cap disables only the offending mod") {
  MemoryAccountant a;
  a.set_cap("hog", 1000);
  a.set_cap("good", 1000);
  CHECK(a.charge("good", 500));
  CHECK(!a.charge("hog", 5000));
  CHECK(a.disabled("hog"));
  CHECK(!a.disabled("good"));
  CHECK_EQ(a.used("good"), std::uint64_t(500));
}

LUAU_TEST("CAP-E03", "sandbox: string metatable escape does not alter other mods") {
  ModSandbox evil(env("evil")), good(env("good"));
  evil.run("probe", fx::slurp(std::string(PROBE_DIR) + "/metatable_escape.luau"));
  auto r = good.run("check", "assert(('x'):upper() == 'X')");
  CHECK(r.status == RunStatus::Ok);
}

LUAU_TEST("CAP-E03", "sandbox: shared globals are frozen") {
  ModSandbox s(env("m"));
  auto r = s.run("probe", fx::slurp(std::string(PROBE_DIR) + "/shared_mutation.luau"));
  CHECK(r.status == RunStatus::Error);
}

LUAU_TEST("CAP-E03", "sandbox: memory bomb stops at the cap") {
  ModSandbox s(env("m", 1 << 20));
  auto r = s.run("probe", fx::slurp(std::string(PROBE_DIR) + "/memory_bomb.luau"));
  CHECK(r.status == RunStatus::MemoryExceeded);
}

LUAU_TEST("CAP-E03", "sandbox: slow pattern is aborted by bounded natives") {
  ModSandbox s(env("m"));
  auto r = s.run("probe", fx::slurp(std::string(PROBE_DIR) + "/slow_pattern.luau"));
  CHECK(r.status == RunStatus::BudgetExceeded);
}

LUAU_TEST("CAP-E03", "sandbox: infinite loop is interrupted without the permission") {
  ModSandbox s(env("m"));
  auto r = s.run("probe", fx::slurp(std::string(PROBE_DIR) + "/infinite_loop.luau"));
  CHECK(r.status == RunStatus::BudgetExceeded);
}

LUAU_TEST("CAP-E03", "sandbox: os, io, debug and arbitrary require are absent") {
  ModSandbox s(env("m"));
  auto r = s.run("probe", "assert(os == nil and io == nil and debug == nil and require == nil)");
  CHECK(r.status == RunStatus::Ok);
}

TEST("CAP-E03", "wall-clock budget: per-dispatch and per-tick limits trip through an injected clock") {
  BudgetLimits l;
  l.steps_per_dispatch = 1000000;
  l.wall_us_per_dispatch = 1000;
  l.wall_us_per_tick = 1500;
  BudgetMeter m(l);
  std::uint64_t now = 0;
  m.set_clock([&] { return now; });
  m.begin_tick();
  m.reset_dispatch();
  now = 900;
  for (int i = 0; i < 512; ++i) m.step("m", "h");   // under both limits
  now = 1100;
  try {
    for (int i = 0; i < 512; ++i) m.step("m", "h");
    CHECK(false);
  } catch (const BudgetExceeded& e) {
    CHECK(e.reason == BudgetExceeded::Reason::WallDispatch);
  }
  // Two dispatches of 800 us each: each is fine alone, the second pushes the tick over 1500.
  m.begin_tick();
  now = 0; m.reset_dispatch(); now = 800;
  for (int i = 0; i < 512; ++i) m.step("m", "h");
  m.end_dispatch();
  now = 0; m.reset_dispatch(); now = 800;
  try {
    for (int i = 0; i < 512; ++i) m.step("m", "h");
    CHECK(false);
  } catch (const BudgetExceeded& e) {
    CHECK(e.reason == BudgetExceeded::Reason::WallTick);
  }
}

TEST("CAP-E03", "reset_dispatch re-arms the step budget; a slice limit overrides the dispatch limit") {
  BudgetLimits l;
  l.steps_per_dispatch = 5;
  BudgetMeter m(l);
  for (int i = 0; i < 5; ++i) m.step("m", "h");
  CHECK_THROWS(m.step("m", "h"));
  m.reset_dispatch();
  for (int i = 0; i < 5; ++i) m.step("m", "h");
  m.reset_dispatch(2);
  m.step("m", "h"); m.step("m", "h");
  CHECK_THROWS(m.step("m", "h"));
}

TEST("CAP-E03", "unbounded_loop_permission lifts only the step limit") {
  BudgetLimits l;
  l.steps_per_dispatch = 3;
  l.unbounded_loop_permission = true;
  BudgetMeter m(l);
  for (int i = 0; i < 100; ++i) m.step("m", "h");
  CHECK_EQ(m.steps(), std::uint64_t(100));
}

TEST("CAP-E03", "strikes are tracked per hook") {
  BudgetMeter m{BudgetLimits{}};
  m.strike("m/a", 1); m.strike("m/a", 2);
  CHECK(!m.strike("m/b", 3));
  CHECK(m.strike("m/a", 4));
}

TEST("CAP-E03", "memory accountant: release, disable stickiness and re-enable") {
  MemoryAccountant a;
  a.set_cap("m", 100);
  CHECK(a.charge("m", 60));
  CHECK(!a.charge("m", 60));
  CHECK(a.disabled("m"));
  a.release("m", 60);
  CHECK(!a.charge("m", 1));      // disabled stays disabled until an operator re-enables
  a.reenable("m");
  CHECK(a.charge("m", 100));
  CHECK_EQ(a.used("m"), std::uint64_t(100));
  CHECK_EQ(a.used("unknown"), std::uint64_t(0));
}
