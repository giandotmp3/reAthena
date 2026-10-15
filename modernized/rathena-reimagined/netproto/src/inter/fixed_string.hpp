#pragma once
// Internal: NUL-terminated fixed-width string fields.

#include <cstddef>
#include <string>
#include <string_view>

#include "rathena/netproto/byte_order.hpp"

namespace rathena::netproto::inter::detail {

/// Writes `s` NUL-padded into [off, off+width). Fails (returns false) when `s`
/// does not fit with its terminator or contains an embedded NUL: credentials
/// and names are never silently truncated.
inline bool write_fixed(MutableByteView out, std::size_t off, std::size_t width, std::string_view s) {
  if (s.size() >= width || s.find('\0') != std::string_view::npos) return false;
  for (std::size_t i = 0; i < width; ++i) {
    out[off + i] = i < s.size() ? static_cast<std::uint8_t>(s[i]) : std::uint8_t{0};
  }
  return true;
}

/// Reads at most width-1 bytes, stopping at the first NUL (legacy forced
/// field[width-1] = '\0').
inline std::string read_fixed(ByteView in, std::size_t off, std::size_t width) {
  std::string s;
  for (std::size_t i = 0; i + 1 < width; ++i) {
    const auto c = in[off + i];
    if (c == 0) break;
    s.push_back(static_cast<char>(c));
  }
  return s;
}

}  // namespace rathena::netproto::inter::detail
