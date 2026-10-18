// Domain-model tests that pass today.
#include "fixtures.hpp"
#include "script_host/budget.hpp"
#include "script_host/ids.hpp"

using namespace script_host;

TEST("CAP-E03", "namespaced id parses mod:kind/name") {
  auto id = NamespacedId::parse("ruleset.renewal:skill/sm_bash");
  CHECK(id.has_value());
  CHECK_EQ(id->mod, std::string("ruleset.renewal"));
  CHECK_EQ(id->kind, std::string("skill"));
  CHECK_EQ(id->name, std::string("sm_bash"));
  CHECK_EQ(id->str(), std::string("ruleset.renewal:skill/sm_bash"));
}

TEST("CAP-E03", "namespaced id rejects malformed input") {
  for (const char* bad : {"", "nomod", "m:kind", "m:/n", ":k/n", "M:k/n", "m:k/n/x y", "m:k/ n"})
    CHECK(!NamespacedId::parse(bad).has_value());
}

TEST("CAP-E03", "event name is namespace:event") {
  // packet events keep their semantic upper-case names
  CHECK(EventName::parse("net:CZ_REQUEST_MOVE").has_value());
  CHECK(!EventName::parse("Net:x").has_value());
  auto ok = EventName::parse("unit:moved");
  CHECK(ok.has_value());
  CHECK_EQ(ok->str(), std::string("unit:moved"));
}

TEST("BR-RULESET-117", "hook spec has a qualified id and ordering fields") {
  auto s = fx::spec("mymod", "fire_brand", 50, Phase::Post, Mode::Modify);
  CHECK_EQ(s.qualified(), std::string("mymod/fire_brand"));
  CHECK_EQ(s.priority, 50);
}

TEST("CAP-E03", "default budget limits are bounded and require permission to lift") {
  BudgetLimits l;
  CHECK(l.steps_per_dispatch > 0);
  CHECK(l.wall_us_per_dispatch <= l.wall_us_per_tick);
  CHECK_EQ(l.strikes_before_disable, 3);
  CHECK(!l.unbounded_loop_permission);
}
