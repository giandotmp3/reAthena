#include "script_host/budget.hpp"

#include <algorithm>
#include <chrono>

namespace script_host {

static std::uint64_t steady_us() {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

BudgetMeter::BudgetMeter(BudgetLimits l) : limits_(l), clock_(steady_us), step_limit_(l.steps_per_dispatch) {}

void BudgetMeter::reset_dispatch(std::uint64_t step_limit) {
  steps_ = 0;
  since_wall_ = 0;
  step_limit_ = step_limit ? step_limit : limits_.steps_per_dispatch;
  start_us_ = clock_();
  armed_ = true;
}

void BudgetMeter::end_dispatch() {
  if (!armed_) return;
  tick_used_us_ += clock_() - start_us_;
  armed_ = false;
}

void BudgetMeter::step(const std::string& mod, const std::string& hook, std::uint64_t n) {
  steps_ += n;
  if (!limits_.unbounded_loop_permission && steps_ > step_limit_)
    throw BudgetExceeded(mod, hook, BudgetExceeded::Reason::Steps);
  since_wall_ += n;
  if (!armed_ || since_wall_ < 256) return;
  since_wall_ = 0;
  std::uint64_t elapsed = clock_() - start_us_;
  std::uint64_t dispatch_cap = limits_.unbounded_loop_permission ? limits_.wall_us_per_tick : limits_.wall_us_per_dispatch;
  if (dispatch_cap && elapsed > dispatch_cap) throw BudgetExceeded(mod, hook, BudgetExceeded::Reason::WallDispatch);
  if (limits_.wall_us_per_tick && tick_used_us_ + elapsed > limits_.wall_us_per_tick)
    throw BudgetExceeded(mod, hook, BudgetExceeded::Reason::WallTick);
}

bool BudgetMeter::strike(const std::string& qualified_hook, std::uint64_t tick) {
  auto& v = strikes_[qualified_hook];
  // Keep only strikes still inside the window (tick - t < window).
  v.erase(std::remove_if(v.begin(), v.end(),
                         [&](std::uint64_t t) { return tick >= t && tick - t >= limits_.strike_window_ticks; }),
          v.end());
  v.push_back(tick);
  return static_cast<int>(v.size()) >= limits_.strikes_before_disable;
}

}  // namespace script_host
