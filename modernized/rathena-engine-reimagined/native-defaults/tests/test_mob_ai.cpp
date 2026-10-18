#include "mini_test.hpp"
#include "native_defaults/mob_ai.hpp"
#include "native_defaults/registry.hpp"

using namespace nd::mobai;

TEST("BR-RULESET-113", "hard tick 100 ms and lazy tick 1000 ms") {
  CHECK_EQ(kHardTickMs, 100);
  CHECK_EQ(kLazyTickMs, 1000);
}

static ModeMap modes() {
  ModeMap m; m.can_move = 1; m.looter = 2; m.aggressive = 4; m.assist = 8; m.change_target = 16; m.angry = 32; m.berserk = 64;
  return m;
}
static MobView mob(std::uint32_t bits) {
  MobView v; v.id = 1; v.modes = bits; v.tunables.aggro_range = 9; v.tunables.chase_range = 12; v.tunables.attack_range = 1;
  return v;
}

PENDING("BR-RULESET-113", "idle aggressive mob with a target in aggro range switches to Chase on the hard tick") {
  auto d = tick(mob(1 | 4), modes(), {{42, 5, true}});
  CHECK(d.next_state == State::Chase);
  CHECK(d.action == Action::MoveToTarget);
  CHECK_EQ(d.next_tick_ms, kHardTickMs);
}
PENDING("BR-RULESET-113", "target within attack range -> Attack") {
  auto v = mob(1 | 4); v.state = State::Chase; v.target = 42;
  auto d = tick(v, modes(), {{42, 1, true}});
  CHECK(d.action == Action::AttackTarget);
}
PENDING("BR-RULESET-113", "idle mob with no one nearby uses the lazy tick") {
  auto d = tick(mob(1), modes(), {});
  CHECK_EQ(d.next_tick_ms, kLazyTickMs);
}
PENDING("BR-RULESET-113", "non-aggressive mob never acquires a target by scan") {
  auto d = tick(mob(1), modes(), {{42, 2, true}});
  CHECK(!d.target.has_value());
}
PENDING("BR-RULESET-113", "immobile mob (no can_move bit) never walks or chases") {
  auto d = tick(mob(4), modes(), {{42, 5, true}});
  CHECK(d.action != Action::MoveToTarget && d.action != Action::WalkRandom);
}
PENDING("BR-RULESET-113", "looter mob picks up loot; slave follows master") {
  auto v = mob(1 | 2); v.master = 7;
  auto d = tick(v, modes(), {});
  CHECK(d.action == Action::FollowMaster);
}
PENDING("BR-RULESET-113", "decision is pure: same input twice gives the same Decision") {
  auto a = tick(mob(1 | 4), modes(), {{42, 5, true}});
  auto b = tick(mob(1 | 4), modes(), {{42, 5, true}});
  CHECK(a.next_state == b.next_state && a.action == b.action);
}
