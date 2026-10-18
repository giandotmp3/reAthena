#include "fixtures.hpp"
#include "script_host/effect_queue.hpp"
#include <cmath>
#include <cstdint>
#include <random>

#include "script_host/numeric.hpp"

__extension__ typedef __int128 i128;  // GCC/Clang extension, as in src/numeric.cpp

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

TEST("BR-RULESET-115", "idiv truncates toward zero like int64 division") {
  NumCtx c(-7);
  c.idiv(2);
  CHECK_EQ(c.value(), std::int64_t(-3));
}

TEST("BR-RULESET-115", "mul_pct is exact beyond 2^53") {
  NumCtx c(9007199254740993LL);  // 2^53 + 1, not representable as a double
  c.mul_pct(100);
  CHECK_EQ(c.value(), std::int64_t(9007199254740993LL));
  NumCtx d(1000);
  d.mul_pct(140);
  CHECK_EQ(d.value(), std::int64_t(1400));
}

TEST("BR-RULESET-115", "f32_step rounds through float32") {
  NumCtx c(16777217);  // 2^24 + 1 is not representable as float32
  c.f32_step(1.0);
  CHECK_EQ(c.value(), std::int64_t(16777216));
}

TEST("BR-RULESET-115", "hook RNG draws do not shift the main stream") {
  RngStreams a(12345), b(12345);
  std::uint32_t a1 = a.main_next();
  b.hook_next("mymod", "extra_hook");
  b.hook_next("mymod", "extra_hook");
  std::uint32_t b1 = b.main_next();
  CHECK_EQ(a1, b1);
  CHECK_EQ(b.main_draw_count(), std::uint64_t(1));
}

TEST("BR-RULESET-115", "hook streams are deterministic and distinct per hook") {
  RngStreams a(7), b(7);
  CHECK_EQ(a.hook_next("m", "h1"), b.hook_next("m", "h1"));
  RngStreams c(7);
  CHECK(c.hook_next("m", "h1") != c.hook_next("m", "h2"));
}

TEST("BR-RULESET-115", "idiv and mul_pct truncate toward zero for negative values (legacy C++, not Luau floor)") {
  NumCtx a(-7); a.idiv(2); CHECK_EQ(a.value(), std::int64_t(-3));
  NumCtx b(7); b.idiv(-2); CHECK_EQ(b.value(), std::int64_t(-3));
  NumCtx c(-7); c.mul_pct(50); CHECK_EQ(c.value(), std::int64_t(-3));  // -350 / 100
  NumCtx d(-1); d.mul_pct(99); CHECK_EQ(d.value(), std::int64_t(0));
}

TEST("BR-RULESET-115", "add, sub, mul, min1 and set are exact int64") {
  NumCtx c(9007199254740993LL);
  c.add(2); CHECK_EQ(c.value(), std::int64_t(9007199254740995LL));
  c.sub(5); CHECK_EQ(c.value(), std::int64_t(9007199254740990LL));
  c.set(3); c.mul(7); CHECK_EQ(c.value(), std::int64_t(21));
  c.set(-4); c.min1(); CHECK_EQ(c.value(), std::int64_t(1));
  c.set(5); c.min1(); CHECK_EQ(c.value(), std::int64_t(5));
}

TEST("BR-RULESET-115", "overflow and division by zero are errors, never wraparound") {
  NumCtx a(INT64_MAX); CHECK_THROWS(a.add(1)); CHECK_EQ(a.value(), INT64_MAX);
  NumCtx b(INT64_MIN); CHECK_THROWS(b.sub(1));
  NumCtx c(INT64_MAX / 2 + 1); CHECK_THROWS(c.mul(2));
  NumCtx d(INT64_MAX); CHECK_THROWS(d.mul_pct(101));
  NumCtx e(5); CHECK_THROWS(e.idiv(0));
  NumCtx f(INT64_MIN); CHECK_THROWS(f.idiv(-1));
  NumCtx g(1); CHECK_THROWS(g.f32_step(1e30)); CHECK_THROWS(g.f32_step(std::nan("")));
}

TEST("BR-RULESET-115", "mul_pct keeps a 128-bit intermediate so large values survive pct > 100") {
  NumCtx c(INT64_MAX / 100 * 100);
  c.mul_pct(100);
  CHECK_EQ(c.value(), INT64_MAX / 100 * 100);
  NumCtx d(INT64_MAX / 3); d.mul_pct(150);  // v*150 overflows int64 but v*150/100 fits
  CHECK_EQ(d.value(), static_cast<std::int64_t>((i128)(INT64_MAX / 3) * 150 / 100));
}

TEST("BR-RULESET-115", "f32_step truncates the float32 product toward zero") {
  NumCtx c(100); c.f32_step(1.5); CHECK_EQ(c.value(), std::int64_t(150));
  NumCtx d(-100); d.f32_step(1.99); CHECK_EQ(d.value(), std::int64_t(-199));
  NumCtx e(3); e.f32_step(0.1); CHECK_EQ(e.value(), std::int64_t(0));   // 0.3f truncates to 0
}

TEST("BR-RULESET-115", "main stream is std::mt19937 and reproducible from the seed") {
  RngStreams a(42);
  std::mt19937 ref(42);
  for (int i = 0; i < 5; ++i) CHECK_EQ(a.main_next(), ref());
  CHECK_EQ(a.main_draw_count(), std::uint64_t(5));
}

TEST("BR-RULESET-115", "strict_parity rejects hooks that draw from the main stream") {
  RngStreams r(1);
  r.set_strict_parity(true);
  CHECK_THROWS(r.main_next_from_hook("mymod", "h"));
  CHECK_EQ(r.main_draw_count(), std::uint64_t(0));
  r.set_strict_parity(false);
  r.main_next_from_hook("mymod", "h");
  CHECK_EQ(r.main_draw_count(), std::uint64_t(1));
}

TEST("BR-RULESET-115", "hook streams depend on seed, mod and hook id only, not on call interleaving") {
  RngStreams a(9), b(9);
  std::uint32_t a1 = a.hook_next("m", "x"); a.hook_next("m", "y"); a.hook_next("m", "y");
  std::uint32_t a2 = a.hook_next("m", "x");
  std::uint32_t b1 = b.hook_next("m", "x"); std::uint32_t b2 = b.hook_next("m", "x");
  CHECK_EQ(a1, b1);
  CHECK_EQ(a2, b2);
  RngStreams c(10);
  CHECK(c.hook_next("m", "x") != a1);  // seed changes the stream
  RngStreams d(9);
  CHECK(d.hook_next("n", "x") != a1);  // mod changes the stream
}
