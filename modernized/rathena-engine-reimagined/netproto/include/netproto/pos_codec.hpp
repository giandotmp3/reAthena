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

// Defined inline: the codec (codec_set.cpp) needs them, and units that compile only a subset of
// netproto's .cpp files (gateway, rathena-sim) must keep linking.


// Inputs wider than the field are masked, never UB.
[[nodiscard]] inline std::array<std::uint8_t, 3> pack_pos3(const Pos3& p) {
  const unsigned x = p.x & 0x3FFu, y = p.y & 0x3FFu, d = p.dir & 0xFu;
  return {static_cast<std::uint8_t>(x >> 2),
          static_cast<std::uint8_t>(((x & 0x3u) << 6) | (y >> 4)),
          static_cast<std::uint8_t>(((y & 0xFu) << 4) | d)};
}

[[nodiscard]] inline Pos3 unpack_pos3(const std::array<std::uint8_t, 3>& b) {
  Pos3 p;
  p.x = static_cast<std::uint16_t>((b[0] << 2) | (b[1] >> 6));
  p.y = static_cast<std::uint16_t>(((b[1] & 0x3Fu) << 4) | (b[2] >> 4));
  p.dir = static_cast<std::uint8_t>(b[2] & 0xFu);
  return p;
}

[[nodiscard]] inline std::array<std::uint8_t, 6> pack_pos6(const Pos6& p) {
  const unsigned x0 = p.x0 & 0x3FFu, y0 = p.y0 & 0x3FFu, x1 = p.x1 & 0x3FFu, y1 = p.y1 & 0x3FFu;
  return {static_cast<std::uint8_t>(x0 >> 2),
          static_cast<std::uint8_t>(((x0 & 0x3u) << 6) | (y0 >> 4)),
          static_cast<std::uint8_t>(((y0 & 0xFu) << 4) | (x1 >> 6)),
          static_cast<std::uint8_t>(((x1 & 0x3Fu) << 2) | (y1 >> 8)),
          static_cast<std::uint8_t>(y1 & 0xFFu),
          static_cast<std::uint8_t>(((p.sx0 & 0xFu) << 4) | (p.sy0 & 0xFu))};
}

[[nodiscard]] inline Pos6 unpack_pos6(const std::array<std::uint8_t, 6>& b) {
  Pos6 p;
  p.x0 = static_cast<std::uint16_t>((b[0] << 2) | (b[1] >> 6));
  p.y0 = static_cast<std::uint16_t>(((b[1] & 0x3Fu) << 4) | (b[2] >> 4));
  p.x1 = static_cast<std::uint16_t>(((b[2] & 0xFu) << 6) | (b[3] >> 2));
  p.y1 = static_cast<std::uint16_t>(((b[3] & 0x3u) << 8) | b[4]);
  p.sx0 = static_cast<std::uint8_t>(b[5] >> 4);
  p.sy0 = static_cast<std::uint8_t>(b[5] & 0xFu);
  return p;
}


}  // namespace netproto
