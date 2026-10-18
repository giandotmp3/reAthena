#pragma once
#include <cstdint>

#include "gateway/types.hpp"

namespace gateway {

struct Clock {
  virtual ~Clock() = default;
  virtual Timestamp now() const = 0;
};

// Injectable randomness (login ids, salts, web tokens). Tests use a fixed sequence.
struct Rng {
  virtual ~Rng() = default;
  virtual std::uint32_t next_u32() = 0;
};

}  // namespace gateway
