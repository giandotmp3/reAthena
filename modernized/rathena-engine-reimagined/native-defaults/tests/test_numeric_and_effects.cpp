#include "fixtures.hpp"
#include "mini_test.hpp"
#include "native_defaults/numeric.hpp"
#include "native_defaults/stage.hpp"

using namespace nd;

TEST("BR-RULESET-115", "idiv truncates toward zero like int64 C++") {
  CHECK_EQ(num::idiv(7, 2), 3);
  CHECK_EQ(num::idiv(-7, 2), -3);
  CHECK_EQ(num::idiv(5, 0), 0);
}

TEST("BR-RULESET-115", "mul_pct chain multiplies, does not add (E-3 card fix 1380)") {
  CHECK_EQ(num::mul_pct(num::mul_pct(1000, 120), 115), 1380);
}

TEST("BR-RULESET-115", "float32 step: RE crit x(1.4+0.01*crate) floors in float32") {
  CHECK_EQ(num::f32_mul_trunc(1000, 1.5), 1500);
  CHECK_EQ(num::f32_mul_trunc(999, 1.4), 1398);
}

TEST("BR-RULESET-115", "int64 headroom beyond 2^53 is exact") {
  const std::int64_t big = (1LL << 60) + 1;
  CHECK_EQ(num::mul_div(big, 1, 1), big);
}

TEST("BR-RULESET-115", "RNG draw is consumed even when bound is 0") {
  fx::CountingRng r;
  r.rnd(0);
  CHECK_EQ(r.draws(), 1u);
}

TEST("BR-RULESET-116", "effect queue preserves order") {
  EffectQueue q;
  q.push({"a", 1, 0, 0});
  q.push({"b", 2, 0, 0});
  CHECK_EQ(q.items.size(), 2u);
  CHECK_EQ(q.items[0].kind, std::string("a"));
}

TEST("BR-RULESET-116", "effect queue rejects push while host is applying (re-entrancy guard)") {
  EffectQueue q;
  q.applying = true;
  CHECK_THROWS(q.push({"x", 1, 0, 0}));
}

PENDING("BR-RULESET-116", "stages append effects instead of mutating the world; cancel discards queue") {
  // Needs weapon_basic to emit Reject Sword / Poison React style effects via ruleset hooks.
  auto st = make_weapon_basic_v1();
  DamageCtx c;
  fx::CountingRng r;
  st->apply(c, fx::tables(false), r);
  CHECK(!c.effects.items.empty());
}
