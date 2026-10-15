#include "rathena/netproto/buffer_limits.hpp"

namespace rathena::netproto {

std::size_t BufferLimits::recv_buffer_size(LinkKind kind) const noexcept {
  return kind == LinkKind::kServerLink ? server_link_fifo : client_recv_initial;
}

bool BufferLimits::write_exceeds(LinkKind kind, std::size_t pending_after) const noexcept {
  // Legacy WFIFO_MAX only applies to non-server connections; server links grow.
  return kind == LinkKind::kClient && pending_after > client_write_max;
}

std::size_t max_outbound_client_packet(std::int32_t packetver) noexcept {
  return packetver < kPacketVerLargeClientPackets ? std::size_t{0x6000} : std::size_t{0xFFFF};
}

bool outbound_client_packet_allowed(std::size_t size, std::int32_t packetver) noexcept {
  return size <= max_outbound_client_packet(packetver);
}

}  // namespace rathena::netproto
