#pragma once
// Injectable RNG (architecture §3.2/§6). Local copy of the core::Rng shape.

#include <cstdint>

namespace rathena::login {

class Rng {
 public:
  virtual ~Rng() = default;
  virtual std::uint32_t next_u32() = 0;
};

/// Deterministic SplitMix64-based generator for tests and seeded runs.
class SeededRng final : public Rng {
 public:
  explicit SeededRng(std::uint64_t seed) : state_(seed) {}
  std::uint32_t next_u32() override;

 private:
  std::uint64_t state_;
};

/// OS-entropy generator for production (std::random_device backed).
class SystemRng final : public Rng {
 public:
  std::uint32_t next_u32() override;
};

}  // namespace rathena::login
