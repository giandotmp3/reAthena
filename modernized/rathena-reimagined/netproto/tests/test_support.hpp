#pragma once
// Shared test helpers. All credential-shaped values here are FAKE and exist
// only for tests; they are not, and must never be, the legacy shipped
// defaults. Production credentials come from ${INTER_SERVER_USER} /
// ${INTER_SERVER_PASS} via core/secrets (BR-INTER-016).

#include <chrono>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

#include "rathena/netproto/inter/handshake.hpp"
#include "rathena/netproto/stall_policy.hpp"

namespace rathena::netproto::test {

inline std::vector<std::uint8_t> bytes(std::initializer_list<int> v) {
  std::vector<std::uint8_t> out;
  out.reserve(v.size());
  for (int b : v) out.push_back(static_cast<std::uint8_t>(b));
  return out;
}

inline void append(std::vector<std::uint8_t>& dst, const std::vector<std::uint8_t>& src) {
  dst.insert(dst.end(), src.begin(), src.end());
}

/// A variable-length frame: cmd, len (whole packet), then filler payload.
inline std::vector<std::uint8_t> variable_frame(std::uint16_t op, std::uint16_t len) {
  std::vector<std::uint8_t> out(len < 4 ? 4 : len, 0xAB);
  out[0] = static_cast<std::uint8_t>(op & 0xFF);
  out[1] = static_cast<std::uint8_t>(op >> 8);
  out[2] = static_cast<std::uint8_t>(len & 0xFF);
  out[3] = static_cast<std::uint8_t>(len >> 8);
  return out;
}

/// A fixed frame of `size` bytes with the given opcode.
inline std::vector<std::uint8_t> fixed_frame(std::uint16_t op, std::size_t size) {
  std::vector<std::uint8_t> out(size, 0);
  out[0] = static_cast<std::uint8_t>(op & 0xFF);
  out[1] = static_cast<std::uint8_t>(op >> 8);
  return out;
}

inline inter::InterServerCredentials fake_inter_credentials() {
  return {"fake-inter-user", "fake-inter-pass-0000"};
}

inline TimePoint t0() { return TimePoint{} + std::chrono::hours(1); }

}  // namespace rathena::netproto::test
