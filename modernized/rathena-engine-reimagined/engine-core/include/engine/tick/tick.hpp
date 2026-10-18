#pragma once
#include <cstdint>
#include <functional>
#include <optional>

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

// Single-thread min-heap timer queue. Equal due times fire in insertion order.
// STUBS (EC-TICK-003..005).
class TimerQueue {
public:
  explicit TimerQueue(const Clock& clock) : clock_(clock) {}
  TimerId add_once(Tick delay_ms, TimerCallback cb);
  TimerId add_interval(Tick period_ms, TimerCallback cb);
  bool cancel(TimerId id);
  size_t run_due();  // fires everything due at clock.now(); returns count
  std::optional<Tick> next_due() const;

private:
  const Clock& clock_;
};

}  // namespace engine
