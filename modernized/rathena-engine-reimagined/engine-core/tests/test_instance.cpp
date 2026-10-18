#include "engine/instance/instance.hpp"
#include "harness.hpp"

using namespace engine;

PENDING_TEST("EC-INST-001", create_clone_with_owner_and_lookup) {
  VirtualClock c;
  TimerQueue q(c);
  InstanceManager m(q);
  InstanceId id = m.create(/*source*/ 7, {1, 42}, 60000);
  CHECK(m.exists(id));
  CHECK(m.source_of(id) == MapId{7});
}

PENDING_TEST("EC-INST-002", ttl_expiry_destroys_instance) {
  VirtualClock c;
  TimerQueue q(c);
  InstanceManager m(q);
  InstanceId id = m.create(7, {1, 42}, 1000);
  c.advance(999); q.run_due();
  CHECK(m.exists(id));
  c.advance(1); q.run_due();
  CHECK(!m.exists(id));
}

PENDING_TEST("EC-INST-003", owner_leaving_destroys_only_its_instances) {
  VirtualClock c;
  TimerQueue q(c);
  InstanceManager m(q);
  InstanceId mine = m.create(7, {1, 42}, 60000);
  InstanceId other = m.create(7, {1, 43}, 60000);
  m.owner_left({1, 42});
  CHECK(!m.exists(mine));
  CHECK(m.exists(other));
}

PENDING_TEST("EC-INST-004", scoped_timers_die_with_instance) {
  VirtualClock c;
  TimerQueue q(c);
  InstanceManager m(q);
  InstanceId id = m.create(7, {1, 42}, 60000);
  int fired = 0;
  m.add_scoped_timer(id, 500, [&](TimerId, Tick, Tick) { ++fired; });
  CHECK(m.destroy(id));
  c.advance(1000); q.run_due();
  CHECK(fired == 0);
}
