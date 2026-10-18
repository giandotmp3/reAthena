#pragma once
#include <cstdint>
#include <stdexcept>
#include <string>

namespace script_host {

struct BudgetLimits {
  std::uint64_t steps_per_dispatch = 100000;
  std::uint64_t steps_per_slice = 50000;
  std::uint64_t wall_us_per_dispatch = 2000;
  std::uint64_t wall_us_per_tick = 8000;
  int strikes_before_disable = 3;
  std::uint64_t strike_window_ticks = 3000;
  bool unbounded_loop_permission = false;  // freeloop equivalent
};

struct BudgetExceeded : std::runtime_error {
  std::string mod, hook;
  BudgetExceeded(std::string m, std::string h)
      : std::runtime_error("budget exceeded: " + m + "/" + h), mod(std::move(m)), hook(std::move(h)) {}
};

class BudgetMeter {
 public:
  explicit BudgetMeter(BudgetLimits l) : limits_(l) {}
  // Called from the interrupt callback; throws BudgetExceeded past the limit.
  void step(const std::string& mod, const std::string& hook, std::uint64_t n = 1);
  void reset_dispatch();
  // Records a strike; returns true when the hook must now be auto-disabled.
  bool strike(const std::string& qualified_hook, std::uint64_t tick);
  const BudgetLimits& limits() const { return limits_; }

 private:
  BudgetLimits limits_;
  std::uint64_t steps_ = 0;
};

}  // namespace script_host
