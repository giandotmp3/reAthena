#pragma once
// BR-MAP-060: sanity checks on the map connect packet (CZ_ENTER family),
// applied before any ticket lookup. First failing check wins.
#include <cstddef>
#include <cstdint>
#include <optional>

#include "netproto/codec_set.hpp"

namespace netproto {

inline constexpr std::int64_t kStartAccountNum = 2'000'000;
inline constexpr std::int64_t kEndAccountNum = 100'000'000;

struct ConnectPacket {
  std::int64_t account_id = 0;  // wide so out-of-range values are representable
  std::int64_t char_id = 0;
  std::uint32_t login_id1 = 0;   // not validated here: the ticket check owns it
  std::uint32_t client_tick = 0;  // not validated (legacy reserved)
  std::uint8_t sex = 0;
};

enum class ConnectError : std::uint8_t {
  kNone = 0,
  kAlreadyLoggedIn,
  kWrongLength,
  kInvalidAccount,
  kInvalidChar,
  kInvalidSex,
};

/// The decode-from-wire path: reads account_id, char_id, login_id1, client_tick and sex out of a decoded
/// connect packet (CZ_ENTER of the catalog; the offsets are per client version in the layout, nothing is
/// hard-coded here). nullopt when the packet is not a connect packet or a field is missing / not an integer.
[[nodiscard]] std::optional<ConnectPacket> connect_packet_from(const DecodedPacket& p);

[[nodiscard]] ConnectError validate_connect(std::size_t packet_len, std::size_t expected_len,
                                            const ConnectPacket& p, bool session_already_authed);

/// Error byte of the 0x006a refusal: 1 unknown version/length, 2 account,
/// 3 char, 6 sex. kAlreadyLoggedIn sends nothing (returns 0, caller ignores).
[[nodiscard]] std::uint8_t refusal_code(ConnectError e);

}  // namespace netproto
