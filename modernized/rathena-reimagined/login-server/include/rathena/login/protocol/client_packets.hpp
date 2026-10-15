#pragma once
// Client <-> login wire codecs (spec §3.2 channel login/auth; CAP-24).
// Layouts mirror src/common/packets.hpp and stay compile-time PACKETVER gated
// (architecture K1). Before promotion into src/, the verbatim legacy headers
// replace these local mirrors; the golden-byte tests below pin the sizes.

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include "rathena/login/md5.hpp"
#include "rathena/login/types.hpp"

#ifndef RATHENA_PACKETVER
#define RATHENA_PACKETVER 20211103
#endif

namespace rathena::login::proto {

inline constexpr std::uint16_t CA_LOGIN = 0x0064;
inline constexpr std::uint16_t CA_LOGIN2 = 0x01dd;
inline constexpr std::uint16_t CA_LOGIN3 = 0x01fa;
inline constexpr std::uint16_t CA_LOGIN4 = 0x027c;
inline constexpr std::uint16_t CA_REQ_HASH = 0x01db;
inline constexpr std::uint16_t CA_SSO_LOGIN_REQ = 0x0825;
inline constexpr std::uint16_t AC_ACK_HASH = 0x01dc;
inline constexpr std::uint16_t SC_NOTIFY_BAN = 0x0081;
#if RATHENA_PACKETVER >= 20170315
inline constexpr std::uint16_t AC_ACCEPT_LOGIN = 0x0ac4;
inline constexpr std::size_t kAcceptServerEntrySize = 160;
#else
inline constexpr std::uint16_t AC_ACCEPT_LOGIN = 0x0069;
inline constexpr std::size_t kAcceptServerEntrySize = 32;
#endif
#if RATHENA_PACKETVER >= 20120000
inline constexpr std::uint16_t AC_REFUSE_LOGIN = 0x083e;
inline constexpr std::size_t kRefuseLoginSize = 26;
#else
inline constexpr std::uint16_t AC_REFUSE_LOGIN = 0x006a;
inline constexpr std::size_t kRefuseLoginSize = 23;
#endif

inline constexpr std::size_t kCaLoginSize = 55;
inline constexpr std::size_t kCaLogin2Size = 47;
inline constexpr std::size_t kCaLogin3Size = 48;
inline constexpr std::size_t kCaLogin4Size = 60;
inline constexpr std::size_t kCaReqHashSize = 2;
inline constexpr std::size_t kSsoHeaderSize = 92;  // fixed part before token[]

/// CA_LOGIN: plaintext password (passwdenc 0).
struct PlainLogin {
  std::uint32_t version = 0;
  std::string username;
  std::string password;  // sensitive: never logged
  std::uint8_t clienttype = 0;
};
/// CA_LOGIN2/3/4: MD5 challenge response (BR-LOGIN-003 shim).
struct ChallengeLogin {
  std::uint16_t opcode = 0;
  std::uint32_t version = 0;
  std::string username;
  Md5Digest digest{};
  std::uint8_t clienttype = 0;
};
struct ReqHash {};
/// CA_SSO_LOGIN_REQ (0x825).
struct SsoLogin {
  std::uint32_t version = 0;
  std::uint8_t clienttype = 0;
  std::string username;
  std::string token;  // sensitive: never logged
};

using ClientRequest = std::variant<PlainLogin, ChallengeLogin, ReqHash, SsoLogin>;

enum class DecodeError { Incomplete, BadLength, UnknownOpcode };
struct DecodeResult {
  std::optional<ClientRequest> request;
  std::optional<DecodeError> error;
  std::size_t consumed = 0;
};

/// Decodes one client packet. Variable-length packets must satisfy
/// header_len <= declared_len <= 32768 (S4; a declared 0 is BadLength).
DecodeResult decode_client_packet(std::span<const std::uint8_t> bytes);

std::vector<std::uint8_t> encode_refuse_login(std::uint32_t code, const std::string& unblock_time);
std::vector<std::uint8_t> encode_ack_hash(const std::string& salt);
std::vector<std::uint8_t> encode_notify_ban(NotifyBan reason);

struct AcceptLogin {
  std::uint32_t login_id1 = 0;
  AccountId account_id = 0;
  std::uint32_t login_id2 = 0;
  Ipv4 last_ip = 0;
  std::string last_login;
  Sex sex = Sex::Male;
  std::string web_token;  // PACKETVER >= 20170315 only
  struct Server {
    Ipv4 ip = 0;
    std::uint16_t port = 0;
    std::string name;
    std::uint16_t users = 0;
    std::uint16_t type = 0;
    std::uint16_t is_new = 0;
  };
  std::vector<Server> servers;
};
std::vector<std::uint8_t> encode_accept_login(const AcceptLogin& a);

}  // namespace rathena::login::proto
