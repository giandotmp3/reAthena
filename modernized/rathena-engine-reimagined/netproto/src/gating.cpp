#include "netproto/gating.hpp"

namespace netproto {

GateDecision gate_packet(Phase session, Phase packet_gate) {
  return session == packet_gate ? GateDecision::kAllow : GateDecision::kDisconnect;
}

bool CycleBudget::try_consume() noexcept {
  if (used_ >= limit_) return false;
  ++used_;
  return true;
}

}  // namespace netproto
