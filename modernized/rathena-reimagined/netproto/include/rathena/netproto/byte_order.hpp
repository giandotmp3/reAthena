#pragma once
// Portable byte-order helpers over spans. The rAthena wire format is
// little-endian ("cmd:u16 [len:u16] payload"), except a few legacy inter-server
// fields that are written with htonl/htons (server ip/port) and are therefore
// big-endian on the wire. Never reinterpret_cast packet buffers: use these.

#include <cstddef>
#include <cstdint>
#include <span>

namespace rathena::netproto {

using ByteView = std::span<const std::uint8_t>;
using MutableByteView = std::span<std::uint8_t>;

[[nodiscard]] constexpr std::uint16_t read_u16le(ByteView b, std::size_t off) noexcept {
  return static_cast<std::uint16_t>(b[off] | (b[off + 1] << 8));
}

[[nodiscard]] constexpr std::uint32_t read_u32le(ByteView b, std::size_t off) noexcept {
  return static_cast<std::uint32_t>(b[off]) | (static_cast<std::uint32_t>(b[off + 1]) << 8) |
         (static_cast<std::uint32_t>(b[off + 2]) << 16) | (static_cast<std::uint32_t>(b[off + 3]) << 24);
}

[[nodiscard]] constexpr std::uint16_t read_u16be(ByteView b, std::size_t off) noexcept {
  return static_cast<std::uint16_t>((b[off] << 8) | b[off + 1]);
}

[[nodiscard]] constexpr std::uint32_t read_u32be(ByteView b, std::size_t off) noexcept {
  return (static_cast<std::uint32_t>(b[off]) << 24) | (static_cast<std::uint32_t>(b[off + 1]) << 16) |
         (static_cast<std::uint32_t>(b[off + 2]) << 8) | static_cast<std::uint32_t>(b[off + 3]);
}

constexpr void write_u16le(MutableByteView b, std::size_t off, std::uint16_t v) noexcept {
  b[off] = static_cast<std::uint8_t>(v & 0xFFu);
  b[off + 1] = static_cast<std::uint8_t>((v >> 8) & 0xFFu);
}

constexpr void write_u32le(MutableByteView b, std::size_t off, std::uint32_t v) noexcept {
  for (std::size_t i = 0; i < 4; ++i) b[off + i] = static_cast<std::uint8_t>((v >> (8 * i)) & 0xFFu);
}

constexpr void write_u16be(MutableByteView b, std::size_t off, std::uint16_t v) noexcept {
  b[off] = static_cast<std::uint8_t>((v >> 8) & 0xFFu);
  b[off + 1] = static_cast<std::uint8_t>(v & 0xFFu);
}

constexpr void write_u32be(MutableByteView b, std::size_t off, std::uint32_t v) noexcept {
  for (std::size_t i = 0; i < 4; ++i) b[off + i] = static_cast<std::uint8_t>((v >> (8 * (3 - i))) & 0xFFu);
}

}  // namespace rathena::netproto
