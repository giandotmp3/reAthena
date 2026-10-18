#include "fixtures.hpp"
#include "script_host/effect_queue.hpp"
#include "script_host/numeric.hpp"

using namespace script_host;

TEST("BR-RULESET-116", "effect queue keeps insertion order") {
  EffectQueue q;
  q.push({"a:first", "t1", "{}"});
  q.push({"a:second", "t2", "{}"});
  CHECK_EQ(q.size(), std::size_t(2));
  CHECK_EQ(q.items()[0].kind, std::string("a:first"));
  CHECK_EQ(q.items()[1].kind, std::string("a:second"));
}

TEST("BR-RULESET-116", "effects are inert until a native applier runs them") {
  EffectQueue q;
  int applied = 0;
  q.push({"a:x", "t", "{}"});
  CHECK_EQ(applied, 0);
  EffectApplier ap;
  ap.apply(q, [&](const Effect&) { ++applied; });
  CHECK_EQ(applied, 1);
  CHECK(q.empty());
}

TEST("BR-RULESET-116", "re-entrancy guard rejects nested apply") {
  EffectQueue q, inner;
  q.push({"a:x", "t", "{}"});
  inner.push({"a:y", "t", "{}"});
  EffectApplier ap;
  CHECK_THROWS(ap.apply(q, [&](const Effect&) { ap.apply(inner, [](const Effect&) {}); }));
  CHECK(!ap.applying());
}

TEST("BR-RULESET-116", "discard drops queued effects") {
  EffectQueue q;
  q.push({"a:x", "t", "{}"});
  q.discard();
  CHECK(q.empty());
}

PENDING("BR-RULESET-115", "idiv truncates toward zero like int64 division") {
  NumCtx c(-7);
  c.idiv(2);
  CHECK_EQ(c.value(), std::int64_t(-3));
}

PENDING("BR-RULESET-115", "mul_pct is exact beyond 2^53") {
  NumCtx c(9007199254740993LL);  // 2^53 + 1, not representable as a double
  c.mul_pct(100);
  CHECK_EQ(c.value(), std::int64_t(9007199254740993LL));
  NumCtx d(1000);
  d.mul_pct(140);
  CHECK_EQ(d.value(), std::int64_t(1400));
}

PENDING("BR-RULESET-115", "f32_step rounds through float32") {
  NumCtx c(16777217);  // 2^24 + 1 is not representable as float32
  c.f32_step(1.0);
  CHECK_EQ(c.value(), std::int64_t(16777216));
}

PENDING("BR-RULESET-115", "hook RNG draws do not shift the main stream") {
  RngStreams a(12345), b(12345);
  std::uint32_t a1 = a.main_next();
  b.hook_next("mymod", "extra_hook");
  b.hook_next("mymod", "extra_hook");
  std::uint32_t b1 = b.main_next();
  CHECK_EQ(a1, b1);
  CHECK_EQ(b.main_draw_count(), std::uint64_t(1));
}

PENDING("BR-RULESET-115", "hook streams are deterministic and distinct per hook") {
  RngStreams a(7), b(7);
  CHECK_EQ(a.hook_next("m", "h1"), b.hook_next("m", "h1"));
  RngStreams c(7);
  CHECK(c.hook_next("m", "h1") != c.hook_next("m", "h2"));
}
