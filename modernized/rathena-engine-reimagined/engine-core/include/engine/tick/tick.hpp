#pragma once
#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <tuple>
#include <unordered_map>

namespace engine {

using Tick = int64_t;  // milliseconds
inline constexpr Tick kTickFloorMs = 20;
inline constexpr Tick kPollCeilingMs = 1000;

// Poll delay is clamped into [kTickFloorMs, kPollCeilingMs].
Tick clamp_poll_delay(Tick delay_ms);

class Clock {
public:
  virtual ~Clock() = default;
  virtual Tick now() const = 0;
};

// Test and simulation clock; advances only when told to.
class VirtualClock final : public Clock {
public:
  Tick now() const override { return now_; }
  void advance(Tick ms) { now_ += ms; }
  void set(Tick t) { now_ = t; }

private:
  Tick now_ = 0;
};

using TimerId = uint64_t;
inline constexpr TimerId kNoTimer = 0;
// (id, scheduled time, actual time)
using TimerCallback = std::function<void(TimerId, Tick scheduled, Tick now)>;

// Single-thread timer queue (ordered set keyed by (due time, insertion seq)).
//
// Semantics, each taken from legacy src/common/timer.cpp do_timer():
//  * A timer is due when due <= clock.now(). run_due() samples now once and
//    loops until the earliest timer is in the future, so a timer added from a
//    callback with delay <= 0 fires in the same call (legacy re-peeks the heap).
//  * Equal due times fire in insertion order. Legacy BHEAP is NOT stable for
//    ties; insertion order is a deliberate deterministic improvement.
//  * Late-timer rule: the callback receives its scheduled time, unless it is
//    more than kLateTimerMs late (strictly), in which case it receives `now`.
//  * Interval timers re-arm after the callback unless cancelled by it. Next due
//    is scheduled + period, except when the firing was more than kLateTimerMs
//    late: then now + period (no burst of catch-up firings beyond 1 s).
//    A timer up to 1 s late catches up with several firings inside one run_due.
//  * cancel() removes immediately (legacy only marks the timer dead and leaves
//    it in the heap; eager removal keeps next_due() truthful). Ids are never
//    reused (legacy recycles ids).
//  * add_interval with period < 1 is rejected (returns kNoTimer), as legacy.
inline constexpr Tick kLateTimerMs = 1000;

class TimerQueue {
public:
  explicit TimerQueue(const Clock& clock) : clock_(clock) {}
  TimerQueue(const TimerQueue&) = delete;
  TimerQueue& operator=(const TimerQueue&) = delete;

  TimerId add_once(Tick delay_ms, TimerCallback cb);
  TimerId add_interval(Tick period_ms, TimerCallback cb);
  bool cancel(TimerId id);  // true if the timer was pending (or firing, for intervals)
  size_t run_due();         // fires everything due at clock.now(); returns count
  std::optional<Tick> next_due() const;
  size_t pending() const { return timers_.size(); }
  // Legacy do_timer() return value: ms until the next timer clamped into
  // [kTickFloorMs, kPollCeilingMs] (kPollCeilingMs when the queue is empty).
  Tick poll_delay() const;

private:
  struct Entry {
    TimerCallback cb;
    Tick due = 0;
    Tick period = 0;  // 0 = one-shot
    uint64_t seq = 0;
    bool queued = false;
  };
  TimerId insert(Tick due, Tick period, TimerCallback cb);

  const Clock& clock_;
  std::unordered_map<TimerId, Entry> timers_;
  std::set<std::tuple<Tick, uint64_t, TimerId>> queue_;
  TimerId next_id_ = 1;
  uint64_t next_seq_ = 0;
  bool running_ = false;
};

}  // namespace engine
