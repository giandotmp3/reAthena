#pragma once
// Inter-server protocol version (architecture section 4: "legacy inter-server
// framing kept + version field in the 0x2710/0x2af8 handshake").
//
// Wire placement: both handshakes already carry an unused u32 at offset 50
// (legacy senders always write 0: char_logif.cpp:827, chrif.cpp:358). The
// version goes there, so packet sizes (86 / 60) stay byte-identical and a
// legacy peer is recognisable as version 0.

#include <cstddef>
#include <cstdint>

namespace rathena::netproto::inter {

using ProtocolVersion = std::uint32_t;

/// Unpatched legacy peer (offset 50 == 0). Speaks 0x2717 with a 1-byte group_id.
inline constexpr ProtocolVersion kProtocolLegacy = 0;
/// v1: 0x2717 carries a widened group_id (BR-INTER-003 fix).
inline constexpr ProtocolVersion kProtocolV1 = 1;
inline constexpr ProtocolVersion kProtocolCurrent = kProtocolV1;

[[nodiscard]] constexpr bool is_known_version(ProtocolVersion v) noexcept {
  return v == kProtocolLegacy || v == kProtocolV1;
}

/// Char servers per login server (legacy MAX_SERVERS, login.hpp:62; BR-INTER-014).
inline constexpr std::size_t kMaxCharServers = 5;
/// Map servers per char server (legacy MAX_MAP_SERVERS default, char.hpp:212).
inline constexpr std::size_t kDefaultMaxMapServers = 2;

/// Fixed string field widths (common/mmo.hpp).
inline constexpr std::size_t kNameLength = 24;        // NAME_LENGTH (23 + NUL)
inline constexpr std::size_t kServerNameLength = 20;
inline constexpr std::size_t kEmailLength = 40;
inline constexpr std::size_t kBirthdateLength = 11;   // "YYYY-MM-DD" + NUL
inline constexpr std::size_t kPincodeLength = 5;      // 4 digits + NUL

}  // namespace rathena::netproto::inter
