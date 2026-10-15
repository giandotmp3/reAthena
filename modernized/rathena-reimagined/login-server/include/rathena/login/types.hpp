#pragma once
// Value types shared across the login-server domain. Local copies (the unit is
// self-contained); they move to src/domain once the unit is promoted.

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace rathena::login {

using AccountId = std::uint32_t;
using GroupId = std::int32_t;
/// Wall-clock Unix seconds. Passed in explicitly; rules never read a clock.
using UnixSeconds = std::int64_t;
/// IPv4 address in host order: a.b.c.d == (a << 24) | (b << 16) | (c << 8) | d.
using Ipv4 = std::uint32_t;

constexpr Ipv4 make_ipv4(std::uint8_t a, std::uint8_t b, std::uint8_t c, std::uint8_t d) {
  return (Ipv4{a} << 24) | (Ipv4{b} << 16) | (Ipv4{c} << 8) | Ipv4{d};
}
std::string ipv4_to_string(Ipv4 ip);

enum class Sex : char { Female = 'F', Male = 'M', Server = 'S' };

/// Legacy limits (common/mmo.hpp, login.hpp). BR-INTER-009 / BR-INTER-014.
inline constexpr std::size_t kNameLength = 24;            // NAME_LENGTH (23 + NUL)
inline constexpr std::size_t kWebAuthTokenLength = 16;     // WEB_AUTH_TOKEN_LENGTH - 1
inline constexpr AccountId kStartAccountNum = 2'000'000;   // START_ACCOUNT_NUM
inline constexpr AccountId kEndAccountNum = 100'000'000;   // END_ACCOUNT_NUM
inline constexpr int kMaxServers = 5;                      // MAX_SERVERS
inline constexpr int kMaxChars = 15;                       // MAX_CHARS (default build)
inline constexpr UnixSeconds kAuthTicketTtl = 30;          // login.hpp:150
inline constexpr const char* kDefaultEmail = "a@a.com";

/// Client-facing refusal codes (AC_REFUSE_LOGIN.error). BR-LOGIN-001.
/// State-derived refusals use (state - 1) and may fall outside this list.
enum class RefuseCode : std::uint32_t {
  UnregisteredId = 0,
  IncorrectPassword = 1,
  Expired = 2,
  RejectedFromServer = 3,
  Blocked = 4,
  ClientVersion = 5,
  BannedUntil = 6,
  AlreadyOnline = 8,
};

/// SC_NOTIFY_BAN reasons used by the login server (BR-LOGIN-016/017/018).
enum class NotifyBan : std::uint8_t {
  ServerClosed = 1,
  AlreadyLoggedIn = 8,
};

}  // namespace rathena::login
