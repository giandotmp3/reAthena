#include "fixtures.hpp"

using namespace script_host;
using fx::add_logging;
using fx::spec;

namespace {
bool throws_load_error_naming(EventBus& bus, const std::string& a, const std::string& b) {
  try {
    bus.validate();
  } catch (const LoadError& e) {
    std::string m = e.what();
    return m.find(a) != std::string::npos && m.find(b) != std::string::npos;
  }
  return false;
}
}  // namespace

TEST("CAP-E03", "dispatch order is priority then registration sequence") {
  EventBus bus;
  std::vector<std::string> log;
  add_logging(bus, spec("m", "late", 10), log);
  add_logging(bus, spec("m", "early", 1), log);
  add_logging(bus, spec("m", "tie_a", 5), log);
  add_logging(bus, spec("m", "tie_b", 5), log);
  HookContext ctx;
  bus.dispatch("battle:stage", ctx);
  CHECK_EQ(log.size(), std::size_t(4));
  CHECK_EQ(log[0], std::string("m/early"));
  CHECK_EQ(log[1], std::string("m/tie_a"));
  CHECK_EQ(log[2], std::string("m/tie_b"));
  CHECK_EQ(log[3], std::string("m/late"));
}

TEST("CAP-E03", "after/before constraints order hooks topologically") {
  EventBus bus;
  std::vector<std::string> log;
  auto s = spec("m", "second", 0);
  s.after = {"m/first"};
  add_logging(bus, s, log);
  add_logging(bus, spec("m", "first", 0), log);
  HookContext ctx;
  bus.dispatch("battle:stage", ctx);
  CHECK_EQ(log[0], std::string("m/first"));
  CHECK_EQ(log[1], std::string("m/second"));
}

TEST("CAP-E03", "priority vs before/after contradiction is a load error naming both hooks") {
  EventBus bus;
  std::vector<std::string> log;
  auto a = spec("m", "a", 1);
  a.after = {"m/b"};  // a wants to run after b, but has the lower priority number
  add_logging(bus, a, log);
  add_logging(bus, spec("m", "b", 9), log);
  CHECK(throws_load_error_naming(bus, "m/a", "m/b"));
}

TEST("CAP-E03", "constraint cycle is a load error naming both hooks") {
  EventBus bus;
  std::vector<std::string> log;
  auto a = spec("m", "a"); a.after = {"m/b"};
  auto b = spec("m", "b"); b.after = {"m/a"};
  add_logging(bus, a, log);
  add_logging(bus, b, log);
  CHECK(throws_load_error_naming(bus, "m/a", "m/b"));
}

TEST("CAP-E03", "two replace hooks on one stage without precedence is a load error") {
  EventBus bus;
  std::vector<std::string> log;
  add_logging(bus, spec("one", "r", 0, Phase::On, Mode::Replace), log);
  add_logging(bus, spec("two", "r", 0, Phase::On, Mode::Replace), log);
  CHECK(throws_load_error_naming(bus, "one/r", "two/r"));
}

TEST("CAP-E03", "declared precedence resolves the replace conflict") {
  EventBus bus;
  std::vector<std::string> log;
  add_logging(bus, spec("one", "r", 0, Phase::On, Mode::Replace), log);
  auto t = spec("two", "r", 0, Phase::On, Mode::Replace);
  t.precedence = true;
  add_logging(bus, t, log);
  bus.validate();  // must not throw
}

TEST("CAP-E03", "cancel in a pre hook discards queued effects and skips on/post") {
  EventBus bus;
  std::vector<std::string> log;
  bus.add(spec("m", "pre", 0, Phase::Pre, Mode::Modify), [](HookContext& c) {
    c.effects->push({"m:fx", "t", "{}"});
    c.cancel("blocked");
  });
  add_logging(bus, spec("m", "on", 0, Phase::On), log);
  add_logging(bus, spec("m", "post", 0, Phase::Post), log);
  EffectQueue q;
  HookContext ctx;
  ctx.effects = &q;
  auto r = bus.dispatch("battle:stage", ctx);
  CHECK(r.cancelled);
  CHECK_EQ(r.cancel_reason, std::string("blocked"));
  CHECK(r.committed_effects.empty());
  CHECK(q.empty());
  CHECK(log.empty());
}

TEST("BR-RULESET-116", "effects commit in order only when the dispatch settles uncancelled") {
  EventBus bus;
  bus.add(spec("m", "a", 1), [](HookContext& c) { c.effects->push({"m:one", "t", "{}"}); });
  bus.add(spec("m", "b", 2), [](HookContext& c) { c.effects->push({"m:two", "t", "{}"}); });
  EffectQueue q;
  HookContext ctx;
  ctx.effects = &q;
  auto r = bus.dispatch("battle:stage", ctx);
  CHECK(!r.cancelled);
  CHECK_EQ(r.committed_effects.size(), std::size_t(2));
  CHECK_EQ(r.committed_effects[0].kind, std::string("m:one"));
  CHECK_EQ(r.committed_effects[1].kind, std::string("m:two"));
}

TEST("CAP-E03", "protected stages ignore cancel from mods without permission") {
  EventBus bus;
  bus.mark_protected("anticheat:gate");
  bus.add(spec("m", "pre", 0, Phase::Pre, Mode::Modify, "anticheat:gate"),
          [](HookContext& c) { c.cancel("nope"); });
  HookContext ctx;
  auto r = bus.dispatch("anticheat:gate", ctx);
  CHECK(!r.cancelled);
}

TEST("BR-RULESET-117", "every minimum hook category can be registered and is dispatched") {
  EventBus bus;
  std::vector<std::string> log;
  int i = 0;
  for (auto cat : {HookCategory::Skill, HookCategory::DamageStage, HookCategory::Status,
                   HookCategory::Item, HookCategory::Formula}) {
    auto s = spec("m", "h" + std::to_string(i++), 0, Phase::On, Mode::Observe, "x:ev");
    s.category = cat;
    add_logging(bus, s, log);
  }
  bus.validate();
  HookContext ctx;
  bus.dispatch("x:ev", ctx);
  CHECK_EQ(log.size(), std::size_t(5));
}

TEST("CAP-E03", "disabled hook is skipped by dispatch") {
  EventBus bus;
  std::vector<std::string> log;
  add_logging(bus, spec("m", "bad"), log);
  bus.disable_hook("m/bad");
  HookContext ctx;
  bus.dispatch("battle:stage", ctx);
  CHECK(log.empty());
}

// ---- ordering ------------------------------------------------------------------
TEST("CAP-E03", "order is phase, then priority, then constraints, then load order, then registration sequence") {
  EventBus bus;
  std::vector<std::string> log;
  add_logging(bus, spec("late_mod", "z", 0, Phase::Post), log);
  add_logging(bus, spec("late_mod", "p", 5, Phase::Pre), log);
  add_logging(bus, spec("early_mod", "same", 0, Phase::On), log);   // registered 3rd, mod seen 2nd
  add_logging(bus, spec("late_mod", "same", 0, Phase::On), log);    // same priority: mod seen first wins
  bus.validate();
  auto o = bus.order("battle:stage");
  CHECK_EQ(o.size(), std::size_t(4));
  CHECK_EQ(o[0], std::string("late_mod/p"));       // pre before on before post, despite priority 5
  CHECK_EQ(o[1], std::string("late_mod/same"));    // late_mod registered first => loaded first
  CHECK_EQ(o[2], std::string("early_mod/same"));
  CHECK_EQ(o[3], std::string("late_mod/z"));
}

TEST("CAP-E03", "explicit mod load order overrides first-registration order") {
  EventBus bus;
  std::vector<std::string> log;
  add_logging(bus, spec("b_mod", "h"), log);
  add_logging(bus, spec("a_mod", "h"), log);
  bus.set_mod_load_order("a_mod", 0);
  bus.set_mod_load_order("b_mod", 1);
  auto o = bus.order("battle:stage");
  CHECK_EQ(o[0], std::string("a_mod/h"));
  CHECK_EQ(o[1], std::string("b_mod/h"));
}

TEST("CAP-E03", "ordering is deterministic regardless of constraint declaration style") {
  auto build = [](bool use_before) {
    EventBus bus;
    std::vector<std::string> log;
    auto a = spec("m", "a"), b = spec("m", "b"), c = spec("m", "c");
    if (use_before) { c.before = {"m/b"}; b.before = {"m/a"}; }
    else { a.after = {"m/b"}; b.after = {"m/c"}; }
    add_logging(bus, a, log); add_logging(bus, b, log); add_logging(bus, c, log);
    return bus.order("battle:stage");
  };
  auto x = build(true), y = build(false);
  CHECK_EQ(x.size(), std::size_t(3));
  CHECK(x == y);
  CHECK_EQ(x[0], std::string("m/c"));
  CHECK_EQ(x[2], std::string("m/a"));
}

TEST("CAP-E03", "constraints naming an unregistered hook are ignored") {
  EventBus bus;
  std::vector<std::string> log;
  auto a = spec("m", "a"); a.after = {"other_mod/missing"};
  add_logging(bus, a, log);
  bus.validate();
  CHECK_EQ(bus.order("battle:stage").size(), std::size_t(1));
}

TEST("CAP-E03", "a pre hook declared after a post hook contradicts phase order") {
  EventBus bus;
  std::vector<std::string> log;
  auto a = spec("m", "a", 0, Phase::Pre); a.after = {"m/b"};
  add_logging(bus, a, log);
  add_logging(bus, spec("m", "b", 0, Phase::Post), log);
  CHECK(throws_load_error_naming(bus, "m/a", "m/b"));
}

TEST("CAP-E03", "duplicate qualified hook id on one event is a load error") {
  EventBus bus;
  std::vector<std::string> log;
  add_logging(bus, spec("m", "dup"), log);
  add_logging(bus, spec("m", "dup"), log);
  CHECK_THROWS(bus.validate());
}

TEST("CAP-E03", "three-hook cycle names its members") {
  EventBus bus;
  std::vector<std::string> log;
  auto a = spec("m", "a"); a.after = {"m/c"};
  auto b = spec("m", "b"); b.after = {"m/a"};
  auto c = spec("m", "c"); c.after = {"m/b"};
  add_logging(bus, a, log); add_logging(bus, b, log); add_logging(bus, c, log);
  try { bus.validate(); CHECK(false); } catch (const LoadError& e) {
    std::string m = e.what();
    CHECK(m.find("m/a") != std::string::npos && m.find("m/b") != std::string::npos && m.find("m/c") != std::string::npos);
    CHECK(m.find("cycle") != std::string::npos);
  }
}

// ---- replace / protected / strict parity -----------------------------------------
TEST("CAP-E03", "only the replace hook with precedence runs; the loser is skipped and reported") {
  EventBus bus;
  std::vector<std::string> log;
  add_logging(bus, spec("one", "r", 0, Phase::On, Mode::Replace), log);
  auto t = spec("two", "r", 0, Phase::On, Mode::Replace);
  t.precedence = true;
  add_logging(bus, t, log);
  add_logging(bus, spec("one", "obs", 5, Phase::On, Mode::Observe), log);
  bus.validate();
  HookContext ctx;
  auto r = bus.dispatch("battle:stage", ctx);
  CHECK_EQ(r.replaced_by, std::string("two/r"));
  CHECK_EQ(log.size(), std::size_t(2));
  CHECK_EQ(log[0], std::string("two/r"));
  CHECK_EQ(log[1], std::string("one/obs"));
  CHECK(!r.diagnostics.empty());
}

TEST("CAP-E03", "two replace hooks that both declare precedence still conflict") {
  EventBus bus;
  std::vector<std::string> log;
  auto a = spec("one", "r", 0, Phase::On, Mode::Replace); a.precedence = true;
  auto b = spec("two", "r", 0, Phase::On, Mode::Replace); b.precedence = true;
  add_logging(bus, a, log); add_logging(bus, b, log);
  CHECK(throws_load_error_naming(bus, "one/r", "two/r"));
}

TEST("CAP-E03", "replace hooks in different phases or events do not conflict") {
  EventBus bus;
  std::vector<std::string> log;
  add_logging(bus, spec("one", "r", 0, Phase::Pre, Mode::Replace), log);
  add_logging(bus, spec("two", "r", 0, Phase::On, Mode::Replace), log);
  add_logging(bus, spec("three", "r", 0, Phase::On, Mode::Replace, "other:event"), log);
  bus.validate();
}

TEST("CAP-E03", "replacing a protected stage needs permission") {
  EventBus bus;
  std::vector<std::string> log;
  bus.mark_protected("anticheat:gate");
  add_logging(bus, spec("m", "r", 0, Phase::On, Mode::Replace, "anticheat:gate"), log);
  CHECK_THROWS(bus.validate());
  EventBus ok;
  auto s = spec("m", "r", 0, Phase::On, Mode::Replace, "anticheat:gate");
  s.permit_protected = true;
  ok.mark_protected("anticheat:gate");
  add_logging(ok, s, log);
  ok.validate();
}

TEST("CAP-E03", "a protected-stage hook with permission may cancel") {
  EventBus bus;
  bus.mark_protected("anticheat:gate");
  auto s = spec("m", "pre", 0, Phase::Pre, Mode::Modify, "anticheat:gate");
  s.permit_protected = true;
  bus.add(s, [](HookContext& c) { c.cancel("blocked"); });
  HookContext ctx;
  CHECK(bus.dispatch("anticheat:gate", ctx).cancelled);
}

TEST("BR-RULESET-115", "strict_parity rejects hooks declaring main-stream RNG at load time") {
  EventBus bus;
  std::vector<std::string> log;
  auto s = spec("m", "greedy"); s.draws_main_rng = true;
  add_logging(bus, s, log);
  bus.validate();  // fine while strict_parity is off
  bus.set_strict_parity(true);
  CHECK(throws_load_error_naming(bus, "m/greedy", "greedy"));
}

// ---- cancellation / effects -------------------------------------------------------
TEST("CAP-E03", "cancel skips later pre hooks too, and keeps effects queued before the dispatch") {
  EventBus bus;
  std::vector<std::string> log;
  bus.add(spec("m", "a", 0, Phase::Pre, Mode::Modify), [](HookContext& c) { c.effects->push({"m:inner", "t", "{}"}); c.cancel("stop"); });
  add_logging(bus, spec("m", "b", 1, Phase::Pre), log);
  EffectQueue q;
  q.push({"outer:keep", "t", "{}"});
  HookContext ctx;
  ctx.effects = &q;
  auto r = bus.dispatch("battle:stage", ctx);
  CHECK(r.cancelled);
  CHECK_EQ(r.cancelled_by, std::string("m/a"));
  CHECK(log.empty());
  CHECK_EQ(q.size(), std::size_t(1));
  CHECK_EQ(q.items()[0].kind, std::string("outer:keep"));
}

TEST("CAP-E03", "cancel outside the pre phase is ignored with a diagnostic") {
  EventBus bus;
  bus.add(spec("m", "on", 0, Phase::On, Mode::Modify), [](HookContext& c) { c.cancel("late"); });
  std::vector<std::string> log;
  add_logging(bus, spec("m", "post", 0, Phase::Post), log);
  HookContext ctx;
  auto r = bus.dispatch("battle:stage", ctx);
  CHECK(!r.cancelled);
  CHECK_EQ(log.size(), std::size_t(1));
  CHECK(!r.diagnostics.empty());
}

TEST("BR-RULESET-116", "dispatch without a caller queue still commits effects, and ctx.effects is restored") {
  EventBus bus;
  bus.add(spec("m", "a"), [](HookContext& c) { c.effects->push({"m:x", "t", "{}"}); });
  HookContext ctx;
  auto r = bus.dispatch("battle:stage", ctx);
  CHECK_EQ(r.committed_effects.size(), std::size_t(1));
  CHECK(ctx.effects == nullptr);
}

TEST("CAP-E03", "observe hooks cannot change ctx; modify hooks can") {
  EventBus bus;
  bus.add(spec("m", "mod", 0, Phase::On, Mode::Modify), [](HookContext& c) { c.num.mul_pct(150); });
  bus.add(spec("m", "obs", 1, Phase::On, Mode::Observe), [](HookContext& c) { c.num.set(0); });
  HookContext ctx;
  ctx.num.set(100);
  auto r = bus.dispatch("battle:stage", ctx);
  CHECK_EQ(ctx.num.value(), std::int64_t(150));
  CHECK(!r.diagnostics.empty());
}

TEST("CAP-E03", "a failing hook is rolled back, recorded, and the dispatch continues") {
  EventBus bus;
  std::vector<std::string> log;
  bus.add(spec("m", "bad", 0, Phase::On, Mode::Modify), [](HookContext& c) {
    c.effects->push({"m:half", "t", "{}"});
    c.num.set(999);
    throw std::runtime_error("boom");
  });
  bus.add(spec("m", "good", 1, Phase::On, Mode::Modify), [](HookContext& c) { c.effects->push({"m:ok", "t", "{}"}); c.num.add(1); });
  EffectQueue q;
  HookContext ctx;
  ctx.effects = &q;
  ctx.num.set(10);
  auto r = bus.dispatch("battle:stage", ctx);
  CHECK(!r.cancelled);
  CHECK_EQ(r.errors.size(), std::size_t(1));
  CHECK_EQ(r.errors[0].hook, std::string("m/bad"));
  CHECK_EQ(r.committed_effects.size(), std::size_t(1));
  CHECK_EQ(r.committed_effects[0].kind, std::string("m:ok"));
  CHECK_EQ(ctx.num.value(), std::int64_t(11));
}

TEST("CAP-E03", "three budget failures inside the window auto-disable the hook; others keep running") {
  EventBus bus;
  std::vector<std::string> log;
  bus.add(spec("evil", "spin"), [](HookContext&) { throw BudgetExceeded("evil", "spin"); });
  add_logging(bus, spec("good", "h", 1), log);
  for (int i = 0; i < 3; ++i) {
    bus.set_tick(100 + i);
    HookContext ctx;
    auto r = bus.dispatch("battle:stage", ctx);
    CHECK_EQ(r.errors.size(), std::size_t(1));
    CHECK_EQ(r.errors[0].kind, std::string("budget"));
    CHECK_EQ(r.auto_disabled.size(), std::size_t(i == 2 ? 1 : 0));
  }
  CHECK(bus.is_disabled("evil/spin"));
  HookContext ctx;
  auto r = bus.dispatch("battle:stage", ctx);
  CHECK(r.errors.empty());
  CHECK_EQ(log.size(), std::size_t(4));
}

TEST("CAP-E03", "strikes spread past the window do not disable the hook") {
  EventBus bus;
  bus.add(spec("evil", "spin"), [](HookContext&) { throw BudgetExceeded("evil", "spin"); });
  for (std::uint64_t t : {std::uint64_t(0), std::uint64_t(4000), std::uint64_t(8000), std::uint64_t(12000)}) {
    bus.set_tick(t);
    HookContext ctx;
    bus.dispatch("battle:stage", ctx);
  }
  CHECK(!bus.is_disabled("evil/spin"));
}

TEST("CAP-E03", "remove_generation drops exactly one (mod, generation) and re-enable restores a hook") {
  EventBus bus;
  std::vector<std::string> log;
  auto g1 = spec("m", "a"); g1.generation = 1;
  auto g2 = spec("m", "b"); g2.generation = 2;
  add_logging(bus, g1, log); add_logging(bus, g2, log);
  CHECK_EQ(bus.count("m", 1), std::size_t(1));
  CHECK_EQ(bus.remove_generation("m", 1), std::size_t(1));
  CHECK_EQ(bus.count("m", 1), std::size_t(0));
  HookContext ctx;
  bus.dispatch("battle:stage", ctx);
  CHECK_EQ(log.size(), std::size_t(1));
  CHECK_EQ(log[0], std::string("m/b"));
  bus.disable_hook("m/b");
  bus.enable_hook("m/b");
  bus.dispatch("battle:stage", ctx);
  CHECK_EQ(log.size(), std::size_t(2));
}

TEST("CAP-E03", "a copied bus is independent (used for atomic reload candidates)") {
  EventBus bus;
  std::vector<std::string> log;
  add_logging(bus, spec("m", "a"), log);
  EventBus copy = bus;
  add_logging(copy, spec("m", "b"), log);
  CHECK_EQ(bus.size(), std::size_t(1));
  CHECK_EQ(copy.size(), std::size_t(2));
}
