#pragma once
// Numeric fidelity helpers (BR-RULESET-115). Legacy math is int64 with
// truncation toward zero and a few float32 steps. These are the native twins of
// the ctx methods exposed to Luau (ctx:idiv, ctx:mul_pct, ctx:f32_step).
#include <cstdint>

namespace nd::num {

using i64 = std::int64_t;

// Truncating division (C++ semantics, toward zero). Divisor 0 yields 0 rather than UB.
constexpr i64 idiv(i64 a, i64 b) noexcept { return b == 0 ? 0 : a / b; }

// v * pct / 100 with truncation.
constexpr i64 mul_pct(i64 v, i64 pct) noexcept { return idiv(v * pct, 100); }

// v * num / den with truncation.
constexpr i64 mul_div(i64 v, i64 num, i64 den) noexcept { return idiv(v * num, den); }

// Round a double to float32 precision.
inline float f32(double v) noexcept { return static_cast<float>(v); }

// Legacy "(float) multiply then truncate" step, e.g. RE crit: dmg * (1.4 + 0.01*crate).
inline i64 f32_mul_trunc(i64 v, double factor) noexcept {
  return static_cast<i64>(static_cast<float>(v) * static_cast<float>(factor));
}

}  // namespace nd::num
