#pragma once
// Numeric fidelity helpers (BR-RULESET-115). Luau numbers are doubles; damage
// lives in a native int64 ctx mutated through methods.
//
// Overflow policy: every method that would leave int64 range throws
// std::overflow_error instead of wrapping (a silently wrong damage value is
// worse than an aborted hook); division by zero throws std::domain_error.
#include <cstdint>
#include <map>
#include <random>
#include <stdexcept>
#include <string>

namespace script_host {

class NumCtx {
 public:
  explicit NumCtx(std::int64_t v = 0) : v_(v) {}
  std::int64_t value() const { return v_; }
  void set(std::int64_t v) { v_ = v; }
  void add(std::int64_t d);                 // exact int64 add
  void sub(std::int64_t d);
  void mul(std::int64_t m);
  void mul_pct(std::int64_t pct);           // v = v * pct / 100, 128-bit intermediate, truncating
  void idiv(std::int64_t d);                // truncating toward zero, d != 0
  void f32_step(double factor);             // v = (int64)((float)v * (float)factor)
  void min1();                              // v = max(v, 1)

 private:
  std::int64_t v_;
};

// Thrown when a hook draws from the main stream while strict_parity is on.
struct ParityViolation : std::runtime_error {
  using std::runtime_error::runtime_error;
};

// Deterministic RNG streams. The main stream is untouched by hook streams.
// Main: std::mt19937 (the generator legacy rAthena uses), seeded from `seed`.
// Hook streams: SplitMix64, seeded from hash(seed, mod, hook_id).
class RngStreams {
 public:
  explicit RngStreams(std::uint64_t seed);
  std::uint32_t main_next();                                       // legacy-parity stream
  std::uint32_t hook_next(const std::string& mod, const std::string& hook_id);  // derived per hook
  // A third-party hook asking for the main stream: rejected under strict_parity,
  // otherwise it draws (and shifts) the main stream.
  std::uint32_t main_next_from_hook(const std::string& mod, const std::string& hook_id);
  std::uint64_t main_draw_count() const { return main_draws_; }
  void set_strict_parity(bool v) { strict_parity_ = v; }
  bool strict_parity() const { return strict_parity_; }

 private:
  std::uint64_t seed_;
  std::uint64_t main_draws_ = 0;
  bool strict_parity_ = true;
  std::mt19937 main_;
  std::map<std::string, std::uint64_t> hook_state_;
};

}  // namespace script_host
