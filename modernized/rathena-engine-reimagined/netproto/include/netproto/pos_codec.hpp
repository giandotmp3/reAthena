#pragma once
// Packed sub-encodings (spec 4.1): 3-byte x/y/dir and 6-byte src/dst.
// Layout follows the client wire format: 10-bit coordinates, big-bit-first.
#include <array>
#include <cstdint>

namespace netproto {

struct Pos3 {
  std::uint16_t x = 0;  // 0..1023
  std::uint16_t y = 0;  // 0..1023
  std::uint8_t dir = 0;  // 0..15
  bool operator==(const Pos3&) const = default;
};
struct Pos6 {
  std::uint16_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;  // 0..1023
  std::uint8_t sx0 = 0, sy0 = 0;  // 0..15 sub-cell, high/low nibble of byte 5
  bool operator==(const Pos6&) const = default;
};

[[nodiscard]] std::array<std::uint8_t, 3> pack_pos3(const Pos3& p);
[[nodiscard]] Pos3 unpack_pos3(const std::array<std::uint8_t, 3>& b);
[[nodiscard]] std::array<std::uint8_t, 6> pack_pos6(const Pos6& p);
[[nodiscard]] Pos6 unpack_pos6(const std::array<std::uint8_t, 6>& b);

}  // namespace netproto
