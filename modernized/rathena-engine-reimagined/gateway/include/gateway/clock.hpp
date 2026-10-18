#pragma once
#include <cstdint>
#include <functional>

#include "gateway/types.hpp"

namespace gateway {

// Time source plus one-shot timers (the legacy add_timer). A ManualClock fires due timers inside
// advance(); the production SystemClock keeps them in a heap that the gateway maintenance loop drains
// with run_due(). Timer callbacks are free to take the gateway strand: they never run under it.
struct Clock {
  virtual ~Clock() = default;
  virtual Timestamp now() const = 0;
  virtual void schedule_after(Timestamp delay_s, std::function<void()> fn) = 0;
};

// Injectable randomness (login ids, salts, web tokens). Tests use a fixed sequence.
struct Rng {
  virtual ~Rng() = default;
  virtual std::uint32_t next_u32() = 0;
};

}  // namespace gateway
