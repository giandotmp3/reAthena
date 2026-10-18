#include "netproto/sim_client.hpp"

namespace netproto {
Bytes SimClient::send(const DecodedPacket&) { not_implemented("SimClient::send"); }
std::vector<DecodedPacket> SimClient::receive(ByteView) { not_implemented("SimClient::receive"); }
}  // namespace netproto
