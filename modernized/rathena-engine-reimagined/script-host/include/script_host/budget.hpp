#pragma once
// Step/time budgets (CAP-E03). The Luau interrupt callback drives
// BudgetMeter::step(); the meter throws BudgetExceeded, which the sandbox turns
// into a typed, host-visible RunStatus::BudgetExceeded that aborts only the
// script that tripped it.
#include <cstdint>
#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace script_host {

struct BudgetLimits {
  std::uint64_t steps_per_dispatch = 100000;
  std::uint64_t steps_per_slice = 50000;
  std::uint64_t wall_us_per_dispatch = 2000;
  std::uint64_t wall_us_per_tick = 8000;
  int strikes_before_disable = 3;
  std::uint64_t strike_window_ticks = 3000;
  // freeloop equivalent: lifts the step limit; the wall clock still applies,
  // at wall_us_per_tick instead of wall_us_per_dispatch.
  bool unbounded_loop_permission = false;
};

struct BudgetExceeded : std::runtime_error {
  enum class Reason { Steps, WallDispatch, WallTick };
  std::string mod, hook;
  Reason reason;
  BudgetExceeded(std::string m, std::string h, Reason r = Reason::Steps)
      : std::runtime_error("budget exceeded: " + m + "/" + h + " (" + reason_name(r) + ")"),
        mod(std::move(m)), hook(std::move(h)), reason(r) {}
  static const char* reason_name(Reason r) {
    return r == Reason::Steps ? "steps" : r == Reason::WallDispatch ? "wall-clock per dispatch" : "wall-clock per tick";
  }
};

class BudgetMeter {
 public:
  using ClockUs = std::function<std::uint64_t()>;  // monotonic microseconds
  explicit BudgetMeter(BudgetLimits l);

  // Called from the interrupt callback; throws BudgetExceeded past a limit.
  // Wall-clock is sampled every 256 steps to keep the callback cheap.
  void step(const std::string& mod, const std::string& hook, std::uint64_t n = 1);

  // Arms a dispatch (or coroutine slice): zeroes the step counter and starts
  // the wall clock. step_limit 0 means limits().steps_per_dispatch.
  void reset_dispatch(std::uint64_t step_limit = 0);
  // Stops the dispatch clock and adds its elapsed time to the tick total.
  void end_dispatch();
  void begin_tick() { tick_used_us_ = 0; }

  // Records a strike; returns true when the hook must now be auto-disabled
  // (strikes_before_disable strikes inside strike_window_ticks).
  bool strike(const std::string& qualified_hook, std::uint64_t tick);

  const BudgetLimits& limits() const { return limits_; }
  void set_limits(BudgetLimits l) { limits_ = l; }
  std::uint64_t steps() const { return steps_; }
  void set_clock(ClockUs c) { clock_ = std::move(c); }

 private:
  BudgetLimits limits_;
  ClockUs clock_;
  std::uint64_t steps_ = 0, step_limit_ = 0, since_wall_ = 0;
  std::uint64_t start_us_ = 0, tick_used_us_ = 0;
  bool armed_ = false;
  std::map<std::string, std::vector<std::uint64_t>> strikes_;
};

}  // namespace script_host
