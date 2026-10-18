#pragma once
// Numeric fidelity helpers (BR-RULESET-115). Luau numbers are doubles; damage
// lives in a native int64 ctx mutated through methods.
#include <cstdint>
#include <string>

namespace script_host {

class NumCtx {
 public:
  explicit NumCtx(std::int64_t v = 0) : v_(v) {}
  std::int64_t value() const { return v_; }
  void set(std::int64_t v) { v_ = v; }
  void add(std::int64_t d);                 // exact int64 add
  void mul_pct(std::int64_t pct);           // v = v * pct / 100, int64 intermediate, truncating
  void idiv(std::int64_t d);                // truncating toward zero, d != 0
  void f32_step(double factor);             // v = (int64)((float)v * (float)factor)

 private:
  std::int64_t v_;
};

// Deterministic RNG streams. The main stream is untouched by hook streams.
class RngStreams {
 public:
  explicit RngStreams(std::uint64_t seed) : seed_(seed), main_state_(seed) {}
  std::uint32_t main_next();                                       // legacy-parity stream
  std::uint32_t hook_next(const std::string& mod, const std::string& hook_id);  // derived per hook
  std::uint64_t main_draw_count() const { return main_draws_; }

 private:
  std::uint64_t seed_, main_state_;
  std::uint64_t main_draws_ = 0;
};

}  // namespace script_host
