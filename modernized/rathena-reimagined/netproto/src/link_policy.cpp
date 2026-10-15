#include "rathena/netproto/link_policy.hpp"

#include <cstdio>

namespace rathena::netproto {

std::string UnknownPacketReport::message() const {
  char op[8];
  std::snprintf(op, sizeof(op), "0x%04x", static_cast<unsigned>(opcode));
  return "unknown packet " + std::string(op) + " on " + link_name +
         (kind == LinkKind::kServerLink ? " (server link)" : " (client)") + "; " +
         std::string(to_string(action));
}

UnknownPacketReport on_unknown_packet(std::string_view link_name, LinkKind kind, std::uint16_t opcode) {
  return UnknownPacketReport{std::string(link_name), kind, opcode, LinkAction::kDisconnect};
}

}  // namespace rathena::netproto
