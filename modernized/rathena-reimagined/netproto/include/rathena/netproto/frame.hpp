#pragma once
// Packet framing and length validation.
//
// Wire format: little-endian  cmd:u16 [len:u16] payload
//   - fixed-length packets: size comes from the packet table
//   - variable-length packets: len:u16 at offset 2 is the whole packet size
//
// Rules:
//   BR-INTER-012  buffer / packet size limits. Variable-length client packets
//                 must be 4..32768 bytes (spec section 4, clif.cpp).
//   BR-INTER-013  [DEFECT fix, S4] every variable-length packet must declare
//                 len >= its header size (and >= 4). Legacy accepted len 0,
//                 which stalled the login-server on a 0-length SSO packet.
//                 The rebuild rejects such a frame immediately (no stall).
//   BR-INTER-002  an opcode that is not in the link's table is never skipped:
//                 the link is dropped (see link_policy.hpp).
//
// Design decision (documented, see summary): the 32768 cap is a *client*
// limit. Server links keep the legacy u16 ceiling (65535) because chrif/intif
// persistence packets (e.g. 0x2b01 char save) are only bounded by len:u16.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>

#include "rathena/netproto/byte_order.hpp"
#include "rathena/netproto/link_policy.hpp"

namespace rathena::netproto {

inline constexpr std::size_t kOpcodeSize = 2;
inline constexpr std::size_t kVariableHeaderSize = 4;  // cmd:u16 + len:u16

struct FrameLimits {
  std::uint16_t min_variable_len = 4;
  std::uint32_t max_variable_len = 32768;

  /// Game-client links (login 6900, char 6121, map 5121).
  [[nodiscard]] static constexpr FrameLimits client() noexcept { return {4, 32768}; }
  /// Inter-server links (char<->login, map<->char): legacy u16 ceiling.
  [[nodiscard]] static constexpr FrameLimits server_link() noexcept { return {4, 65535}; }
};

/// One entry of a packet table. Layouts themselves stay compile-time C++ (K1);
/// this is only the framing contract.
struct PacketSpec {
  enum class Kind : std::uint8_t { kFixed, kVariable };

  std::uint16_t opcode = 0;
  Kind kind = Kind::kFixed;
  /// kFixed: exact packet size (>= 2). kVariable: minimum size == header size (>= 4).
  std::uint16_t size = 0;

  [[nodiscard]] static constexpr PacketSpec fixed(std::uint16_t op, std::uint16_t sz) noexcept {
    return {op, Kind::kFixed, sz};
  }
  [[nodiscard]] static constexpr PacketSpec variable(std::uint16_t op,
                                                     std::uint16_t header_size = kVariableHeaderSize) noexcept {
    return {op, Kind::kVariable, header_size};
  }
};

/// Opcode -> framing spec for one kind of link. Immutable after construction
/// in normal use; built from legacy length arrays (0 = absent, -1 = variable).
class PacketTable {
 public:
  PacketTable(std::string name, FrameLimits limits);

  /// Throws std::invalid_argument for a malformed spec (fixed < 2 bytes,
  /// variable header < 4 bytes) or a duplicate opcode.
  PacketTable& add(PacketSpec spec);

  /// Merge another table's entries (e.g. chrif + intif on the map->char link).
  PacketTable& merge(const PacketTable& other);

  /// Build from a legacy `packet_len_table` style array starting at `base`:
  /// 0 -> opcode absent (unknown), -1 -> variable (header 4), n > 0 -> fixed n.
  static PacketTable from_legacy_lengths(std::string name, FrameLimits limits, std::uint16_t base,
                                         std::span<const std::int32_t> lengths);

  [[nodiscard]] const PacketSpec* find(std::uint16_t opcode) const noexcept;
  [[nodiscard]] const FrameLimits& limits() const noexcept { return limits_; }
  [[nodiscard]] const std::string& name() const noexcept { return name_; }
  [[nodiscard]] std::size_t size() const noexcept { return specs_.size(); }

 private:
  std::string name_;
  FrameLimits limits_;
  std::unordered_map<std::uint16_t, PacketSpec> specs_;
};

enum class FrameStatus : std::uint8_t {
  kComplete,       // `length` bytes form one whole packet
  kNeedMore,       // wait for more bytes (bounded by the stall policy)
  kUnknownPacket,  // opcode not in table -> drop link (BR-INTER-002)
  kInvalidLength,  // declared length violates limits or header size -> drop link (S4)
};

struct FrameResult {
  FrameStatus status = FrameStatus::kNeedMore;
  std::uint16_t opcode = 0;
  /// kComplete: packet size. kInvalidLength: the offending declared length.
  std::uint32_t length = 0;
};

/// Inspect the front of `buffer` without consuming it.
[[nodiscard]] FrameResult peek_frame(ByteView buffer, const PacketTable& table) noexcept;

/// Map a frame status to what the connection must do next.
[[nodiscard]] LinkAction action_for(FrameStatus status) noexcept;

struct DrainResult {
  std::size_t consumed = 0;            // bytes belonging to delivered frames
  std::size_t frames = 0;              // frames delivered to the handler
  LinkAction action = LinkAction::kContinue;
  std::optional<FrameResult> fault;    // set when action == kDisconnect
};

/// Deliver every complete frame at the front of `buffer` to `on_frame`.
/// Stops at the first incomplete frame (kWaitForData) or the first fault
/// (kDisconnect); bytes after a fault are never handed to `on_frame`.
DrainResult drain_frames(ByteView buffer, const PacketTable& table,
                         const std::function<void(std::uint16_t opcode, ByteView packet)>& on_frame);

}  // namespace rathena::netproto
