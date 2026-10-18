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

PENDING("CAP-E03", "dispatch order is priority then registration sequence") {
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

PENDING("CAP-E03", "after/before constraints order hooks topologically") {
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

PENDING("CAP-E03", "priority vs before/after contradiction is a load error naming both hooks") {
  EventBus bus;
  std::vector<std::string> log;
  auto a = spec("m", "a", 1);
  a.after = {"m/b"};  // a wants to run after b, but has the lower priority number
  add_logging(bus, a, log);
  add_logging(bus, spec("m", "b", 9), log);
  CHECK(throws_load_error_naming(bus, "m/a", "m/b"));
}

PENDING("CAP-E03", "constraint cycle is a load error naming both hooks") {
  EventBus bus;
  std::vector<std::string> log;
  auto a = spec("m", "a"); a.after = {"m/b"};
  auto b = spec("m", "b"); b.after = {"m/a"};
  add_logging(bus, a, log);
  add_logging(bus, b, log);
  CHECK(throws_load_error_naming(bus, "m/a", "m/b"));
}

PENDING("CAP-E03", "two replace hooks on one stage without precedence is a load error") {
  EventBus bus;
  std::vector<std::string> log;
  add_logging(bus, spec("one", "r", 0, Phase::On, Mode::Replace), log);
  add_logging(bus, spec("two", "r", 0, Phase::On, Mode::Replace), log);
  CHECK(throws_load_error_naming(bus, "one/r", "two/r"));
}

PENDING("CAP-E03", "declared precedence resolves the replace conflict") {
  EventBus bus;
  std::vector<std::string> log;
  add_logging(bus, spec("one", "r", 0, Phase::On, Mode::Replace), log);
  auto t = spec("two", "r", 0, Phase::On, Mode::Replace);
  t.precedence = true;
  add_logging(bus, t, log);
  bus.validate();  // must not throw
}

PENDING("CAP-E03", "cancel in a pre hook discards queued effects and skips on/post") {
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

PENDING("BR-RULESET-116", "effects commit in order only when the dispatch settles uncancelled") {
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

PENDING("CAP-E03", "protected stages ignore cancel from mods without permission") {
  EventBus bus;
  bus.mark_protected("anticheat:gate");
  bus.add(spec("m", "pre", 0, Phase::Pre, Mode::Modify, "anticheat:gate"),
          [](HookContext& c) { c.cancel("nope"); });
  HookContext ctx;
  auto r = bus.dispatch("anticheat:gate", ctx);
  CHECK(!r.cancelled);
}

PENDING("BR-RULESET-117", "every minimum hook category can be registered and is dispatched") {
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

PENDING("CAP-E03", "disabled hook is skipped by dispatch") {
  EventBus bus;
  std::vector<std::string> log;
  add_logging(bus, spec("m", "bad"), log);
  bus.disable_hook("m/bad");
  HookContext ctx;
  bus.dispatch("battle:stage", ctx);
  CHECK(log.empty());
}
