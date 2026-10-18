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

PENDING("CAP-E03", "step budget aborts the script with mod and hook ids") {
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

PENDING("CAP-E03", "three strikes in the window auto-disable a hook") {
  BudgetMeter m{BudgetLimits{}};
  CHECK(!m.strike("m/h", 1));
  CHECK(!m.strike("m/h", 2));
  CHECK(m.strike("m/h", 3));
}

PENDING("CAP-E03", "strikes outside the window do not accumulate") {
  BudgetMeter m{BudgetLimits{}};
  m.strike("m/h", 1);
  m.strike("m/h", 2);
  CHECK(!m.strike("m/h", 2 + m.limits().strike_window_ticks + 10));
}

PENDING("CAP-E03", "per-mod memory cap disables only the offending mod") {
  MemoryAccountant a;
  a.set_cap("hog", 1000);
  a.set_cap("good", 1000);
  CHECK(a.charge("good", 500));
  CHECK(!a.charge("hog", 5000));
  CHECK(a.disabled("hog"));
  CHECK(!a.disabled("good"));
  CHECK_EQ(a.used("good"), std::uint64_t(500));
}

PENDING("CAP-E03", "sandbox: string metatable escape does not alter other mods") {
  ModSandbox evil(env("evil")), good(env("good"));
  evil.run("probe", fx::slurp(std::string(PROBE_DIR) + "/metatable_escape.luau"));
  auto r = good.run("check", "assert(('x'):upper() == 'X')");
  CHECK(r.status == RunStatus::Ok);
}

PENDING("CAP-E03", "sandbox: shared globals are frozen") {
  ModSandbox s(env("m"));
  auto r = s.run("probe", fx::slurp(std::string(PROBE_DIR) + "/shared_mutation.luau"));
  CHECK(r.status == RunStatus::Error);
}

PENDING("CAP-E03", "sandbox: memory bomb stops at the cap") {
  ModSandbox s(env("m", 1 << 20));
  auto r = s.run("probe", fx::slurp(std::string(PROBE_DIR) + "/memory_bomb.luau"));
  CHECK(r.status == RunStatus::MemoryExceeded);
}

PENDING("CAP-E03", "sandbox: slow pattern is aborted by bounded natives") {
  ModSandbox s(env("m"));
  auto r = s.run("probe", fx::slurp(std::string(PROBE_DIR) + "/slow_pattern.luau"));
  CHECK(r.status == RunStatus::BudgetExceeded);
}

PENDING("CAP-E03", "sandbox: infinite loop is interrupted without the permission") {
  ModSandbox s(env("m"));
  auto r = s.run("probe", fx::slurp(std::string(PROBE_DIR) + "/infinite_loop.luau"));
  CHECK(r.status == RunStatus::BudgetExceeded);
}

PENDING("CAP-E03", "sandbox: os, io, debug and arbitrary require are absent") {
  ModSandbox s(env("m"));
  auto r = s.run("probe", "assert(os == nil and io == nil and debug == nil and require == nil)");
  CHECK(r.status == RunStatus::Ok);
}
