#pragma once
// Inter-server registration handshakes.
//
//   BR-INTER-001 / BR-LOGIN-025  char -> login  0x2710 (86 B)  -> 0x2711 (3 B)
//   BR-CHAR-030                  map  -> char   0x2af8 (60 B)  -> 0x2af9 (3 B)
//
// 0x2710 layout: cmd u16 | userid[24] | passwd[24] | proto_version u32 (LE)
//                | ip u32 (BE) | port u16 (BE) | name[20] | reserved u16
//                | type u16 (maintenance) | new u16 (display "new")
// 0x2af8 layout: cmd u16 | userid[24] | passwd[24] | proto_version u32 (LE)
//                | ip u32 (BE) | port u16 (BE)
// Ack:           cmd u16 | code u8   (0 = accepted, 3 = refused)
//
// Security (S1, BR-INTER-016): netproto never reads or defaults credentials.
// Expected credentials are injected by core/secrets from ${INTER_SERVER_USER}
// and ${INTER_SERVER_PASS}. Empty expected credentials fail closed (legacy
// allowed an empty password). Comparison is constant-time over the full field.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "rathena/netproto/byte_order.hpp"
#include "rathena/netproto/codec_result.hpp"
#include "rathena/netproto/frame.hpp"
#include "rathena/netproto/inter/version.hpp"

namespace rathena::netproto::inter {

inline constexpr std::uint16_t kOpCharServerLogin = 0x2710;
inline constexpr std::uint16_t kOpCharServerLoginAck = 0x2711;
inline constexpr std::uint16_t kOpMapServerLogin = 0x2af8;
inline constexpr std::uint16_t kOpMapServerLoginAck = 0x2af9;

inline constexpr std::size_t kCharServerLoginSize = 86;
inline constexpr std::size_t kMapServerLoginSize = 60;
inline constexpr std::size_t kServerLoginAckSize = 3;

enum class AckCode : std::uint8_t { kAccepted = 0, kRefused = 3 };

struct CharServerLogin {
  std::string userid;
  std::string passwd;
  ProtocolVersion version = kProtocolCurrent;
  std::uint32_t ip = 0;     // host byte order
  std::uint16_t port = 0;   // host byte order
  std::string server_name;
  std::uint16_t type = 0;   // char_maintenance
  std::uint16_t new_display = 0;
};

struct MapServerLogin {
  std::string userid;
  std::string passwd;
  ProtocolVersion version = kProtocolCurrent;
  std::uint32_t ip = 0;
  std::uint16_t port = 0;
};

struct ServerLoginAck {
  std::uint16_t opcode = kOpCharServerLoginAck;
  AckCode code = AckCode::kRefused;
};

// ---- codec ---------------------------------------------------------------

[[nodiscard]] CodecResult<std::vector<std::uint8_t>> encode(const CharServerLogin& p);
[[nodiscard]] CodecResult<std::vector<std::uint8_t>> encode(const MapServerLogin& p);
[[nodiscard]] std::vector<std::uint8_t> encode(const ServerLoginAck& p);

[[nodiscard]] CodecResult<CharServerLogin> decode_char_server_login(ByteView packet);
[[nodiscard]] CodecResult<MapServerLogin> decode_map_server_login(ByteView packet);
[[nodiscard]] CodecResult<ServerLoginAck> decode_server_login_ack(ByteView packet, std::uint16_t expected_opcode);

/// Framing specs for the pre-registration phase (only the handshake is legal).
[[nodiscard]] PacketTable char_server_handshake_table();
[[nodiscard]] PacketTable map_server_handshake_table();

/// Log-safe description: never includes the password (S2).
[[nodiscard]] std::string describe(const CharServerLogin& p);
[[nodiscard]] std::string describe(const MapServerLogin& p);

// ---- acceptance policy ---------------------------------------------------

/// Injected by core/secrets; never constructed from a literal in production.
struct InterServerCredentials {
  std::string userid;
  std::string passwd;
  [[nodiscard]] bool configured() const noexcept { return !userid.empty() && !passwd.empty(); }
};

struct HandshakePolicy {
  /// Peers announcing a lower version are refused. Default: current version
  /// (secure default). An operator may lower it to kProtocolLegacy during a
  /// mixed-version rollout.
  ProtocolVersion min_version = kProtocolCurrent;
};

enum class RefuseReason : std::uint8_t {
  kNone,
  kCredentialsNotConfigured,
  kBadCredentials,
  kUnsupportedVersion,
  kNotServerAccount,     // login: account sex != 'S'
  kServerIdOutOfRange,   // login: account_id >= kMaxCharServers
  kSlotOccupied,         // login: slot[account_id] already connected
  kNoFreeSlot,           // char: all map-server slots in use
};

/// Occupancy of the server slots on the accepting side.
class ServerSlots {
 public:
  explicit ServerSlots(std::size_t capacity) : occupied_(capacity, false) {}
  [[nodiscard]] std::size_t capacity() const noexcept { return occupied_.size(); }
  [[nodiscard]] bool occupied(std::size_t i) const { return occupied_.at(i); }
  void set_occupied(std::size_t i, bool v) { occupied_.at(i) = v; }
  [[nodiscard]] std::optional<std::size_t> first_free() const noexcept;

 private:
  std::vector<bool> occupied_;
};

struct HandshakeDecision {
  AckCode code = AckCode::kRefused;
  RefuseReason reason = RefuseReason::kNone;
  std::optional<std::size_t> slot;  // assigned slot when accepted
  [[nodiscard]] bool accepted() const noexcept { return code == AckCode::kAccepted; }
};

/// Result of login's account lookup for the userid/passwd in 0x2710.
struct ServerAccountAuth {
  bool credentials_ok = false;
  char sex = '\0';
  std::uint32_t account_id = 0;
};

/// BR-LOGIN-025 / BR-INTER-001: sex-'S' account, id < kMaxCharServers, free slot,
/// supported version. Slot index == account_id (legacy ch_server[account_id]).
[[nodiscard]] HandshakeDecision evaluate_char_server_login(const CharServerLogin& req,
                                                           const ServerAccountAuth& auth,
                                                           const ServerSlots& slots,
                                                           const HandshakePolicy& policy = {});

/// BR-CHAR-030: credentials equal the injected ones, a slot is free, version ok.
[[nodiscard]] HandshakeDecision evaluate_map_server_login(const MapServerLogin& req,
                                                          const InterServerCredentials& expected,
                                                          const ServerSlots& slots,
                                                          const HandshakePolicy& policy = {});

/// Constant-time equality (length mismatch still compares every byte).
[[nodiscard]] bool constant_time_equals(std::string_view a, std::string_view b) noexcept;

}  // namespace rathena::netproto::inter
