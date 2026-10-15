// API STUB: see link_session.hpp. Pending acceptance tests:
//   BR_INTER_001 (handshake gate), BR_INTER_007 (post-auth table switch),
//   BR_INTER_010 (keepalive on stall via tick()).
#include "rathena/netproto/link_session.hpp"

#include <stdexcept>

namespace rathena::netproto {

namespace {
[[noreturn]] void not_implemented(const char* what) {
  throw std::logic_error(std::string("netproto::LinkSession::") + what + " is not implemented yet");
}
}  // namespace

LinkSession::LinkSession(LinkRole role, StallPolicy stall) : role_(role), stall_(stall) {}

std::vector<LinkEvent> LinkSession::on_bytes(ByteView, TimePoint) { not_implemented("on_bytes"); }

void LinkSession::mark_registered(inter::ProtocolVersion) { not_implemented("mark_registered"); }

LinkAction LinkSession::tick(TimePoint) const { not_implemented("tick"); }

bool LinkSession::registered() const noexcept { return false; }

std::optional<inter::ProtocolVersion> LinkSession::version() const noexcept { return std::nullopt; }

}  // namespace rathena::netproto
