#include <vector>

#include "engine/tick/tick.hpp"
#include "harness.hpp"

using namespace engine;

TEST("EC-TICK-001", virtual_clock_advances_only_when_told) {
  VirtualClock c;
  CHECK(c.now() == 0);
  c.advance(50);
  CHECK(c.now() == 50);
  c.set(1000);
  CHECK(c.now() == 1000);
}

TEST("EC-TICK-002", poll_delay_clamped_to_20ms_floor_and_1000ms_ceiling) {
  CHECK(clamp_poll_delay(0) == 20);
  CHECK(clamp_poll_delay(19) == 20);
  CHECK(clamp_poll_delay(500) == 500);
  CHECK(clamp_poll_delay(5000) == 1000);
}

TEST("EC-TICK-003", timers_fire_by_due_time_then_insertion_order) {
  VirtualClock c;
  TimerQueue q(c);
  std::vector<int> order;
  q.add_once(30, [&](TimerId, Tick, Tick) { order.push_back(1); });
  q.add_once(10, [&](TimerId, Tick, Tick) { order.push_back(2); });
  q.add_once(10, [&](TimerId, Tick, Tick) { order.push_back(3); });
  c.advance(30);
  CHECK(q.run_due() == 3);
  CHECK((order == std::vector<int>{2, 3, 1}));
}

TEST("EC-TICK-004", cancelled_timer_never_fires_and_not_due_timer_waits) {
  VirtualClock c;
  TimerQueue q(c);
  int fired = 0;
  TimerId t = q.add_once(10, [&](TimerId, Tick, Tick) { ++fired; });
  q.add_once(100, [&](TimerId, Tick, Tick) { ++fired; });
  CHECK(q.cancel(t));
  CHECK(!q.cancel(t));
  c.advance(50);
  CHECK(q.run_due() == 0);
  CHECK(fired == 0);
  CHECK(q.next_due() == Tick{100});
}

TEST("EC-TICK-005", late_timer_reports_scheduled_and_actual_time) {
  VirtualClock c;
  TimerQueue q(c);
  Tick sched = -1, actual = -1;
  q.add_once(100, [&](TimerId, Tick s, Tick n) { sched = s; actual = n; });
  c.advance(350);
  q.run_due();
  CHECK(sched == 100);
  CHECK(actual == 350);
}

TEST("EC-TICK-006", interval_timer_rearms_until_cancelled) {
  VirtualClock c;
  TimerQueue q(c);
  int n = 0;
  TimerId id = q.add_interval(100, [&](TimerId, Tick, Tick) { ++n; });
  for (int i = 0; i < 3; ++i) { c.advance(100); q.run_due(); }
  CHECK(n == 3);
  CHECK(q.cancel(id));
  c.advance(100);
  q.run_due();
  CHECK(n == 3);
}
