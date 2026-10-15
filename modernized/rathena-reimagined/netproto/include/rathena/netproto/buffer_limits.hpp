#pragma once
// BR-INTER-012: buffer and packet size limits (legacy common/socket.cpp:243-263).
//
//   - receive FIFO starts at 2 KiB for clients, 256 KiB for server links
//   - pending writes to a *client* are capped at 1 MiB; over the cap -> disconnect
//   - the largest packet a client can take is 0x6000 bytes before
//     PACKETVER 20131223 and 0xFFFF from then on (bigger packets crash it)

#include <cstddef>
#include <cstdint>
#include <optional>

#include "rathena/netproto/link_policy.hpp"

namespace rathena::netproto {

inline constexpr std::int32_t kPacketVerLargeClientPackets = 20131223;

struct BufferLimits {
  std::size_t client_recv_initial = 2 * 1024;
  std::size_t server_link_fifo = 256 * 1024;
  /// Legacy WFIFO_MAX applies to non-server connections only.
  std::size_t client_write_max = 1024 * 1024;

  [[nodiscard]] std::size_t recv_buffer_size(LinkKind kind) const noexcept;

  /// True when queuing `pending_after` bytes for `kind` must close the link.
  [[nodiscard]] bool write_exceeds(LinkKind kind, std::size_t pending_after) const noexcept;
};

/// Largest single packet the selected client version can receive.
[[nodiscard]] std::size_t max_outbound_client_packet(std::int32_t packetver) noexcept;

/// True when an outbound client packet of `size` bytes is safe to send.
[[nodiscard]] bool outbound_client_packet_allowed(std::size_t size, std::int32_t packetver) noexcept;

}  // namespace rathena::netproto
