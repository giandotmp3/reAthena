#include "script_host/numeric.hpp"

#include <cmath>
#include <limits>

namespace script_host {

namespace {
using i128 = __int128;
std::int64_t narrow(i128 v, const char* what) {
  if (v > std::numeric_limits<std::int64_t>::max() || v < std::numeric_limits<std::int64_t>::min())
    throw std::overflow_error(std::string("int64 overflow in ") + what);
  return static_cast<std::int64_t>(v);
}
std::uint64_t splitmix(std::uint64_t& s) {
  std::uint64_t z = (s += 0x9E3779B97F4A7C15ull);
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
  return z ^ (z >> 31);
}
std::uint64_t fnv1a(const std::string& s, std::uint64_t h = 1469598103934665603ull) {
  for (unsigned char c : s) { h ^= c; h *= 1099511628211ull; }
  return h;
}
}  // namespace

void NumCtx::add(std::int64_t d) { v_ = narrow(static_cast<i128>(v_) + d, "add"); }
void NumCtx::sub(std::int64_t d) { v_ = narrow(static_cast<i128>(v_) - d, "sub"); }
void NumCtx::mul(std::int64_t m) { v_ = narrow(static_cast<i128>(v_) * m, "mul"); }
void NumCtx::mul_pct(std::int64_t pct) { v_ = narrow(static_cast<i128>(v_) * pct / 100, "mul_pct"); }
void NumCtx::idiv(std::int64_t d) {
  if (d == 0) throw std::domain_error("idiv by zero");
  v_ = narrow(static_cast<i128>(v_) / d, "idiv");  // i128 `/` truncates toward zero
}
void NumCtx::f32_step(double factor) {
  float r = static_cast<float>(v_) * static_cast<float>(factor);
  if (!std::isfinite(r) || r >= 9223372036854775808.0f || r < -9223372036854775808.0f)
    throw std::overflow_error("int64 overflow in f32_step");
  v_ = static_cast<std::int64_t>(r);  // truncating, like the legacy C++ cast
}
void NumCtx::min1() { if (v_ < 1) v_ = 1; }

RngStreams::RngStreams(std::uint64_t seed) : seed_(seed), main_(static_cast<std::uint32_t>(seed)) {}

std::uint32_t RngStreams::main_next() {
  ++main_draws_;
  return main_();
}

std::uint32_t RngStreams::hook_next(const std::string& mod, const std::string& hook_id) {
  std::string key = mod + "/" + hook_id;
  auto it = hook_state_.find(key);
  if (it == hook_state_.end()) {
    std::uint64_t s = seed_ ^ fnv1a(key);
    splitmix(s);  // warm up so nearby seeds diverge
    it = hook_state_.emplace(std::move(key), s).first;
  }
  return static_cast<std::uint32_t>(splitmix(it->second) >> 32);
}

std::uint32_t RngStreams::main_next_from_hook(const std::string& mod, const std::string& hook_id) {
  if (strict_parity_)
    throw ParityViolation("strict_parity: hook " + mod + "/" + hook_id + " may not draw from the main RNG stream");
  return main_next();
}

}  // namespace script_host
