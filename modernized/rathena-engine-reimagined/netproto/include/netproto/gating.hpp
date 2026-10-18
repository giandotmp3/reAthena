#pragma once
// Session-phase gating and per-cycle packet budget (BR-MAP-059, CAP-20).
//   pre-session:  only the connect packet (CZ_ENTER family)
//   pre-map-load: only the load-end ack (CZ_NOTIFY_ACTORINIT)
//   in-world:     only packets whose gate is InWorld
// The gate is data (PacketDef::gate), not a hard-coded opcode list. Stages that
// enforce gating are `protected`: mods cannot cancel or replace them.
#include <cstdint>

#include "netproto/spec.hpp"

namespace netproto {

enum class GateDecision : std::uint8_t { kAllow, kDisconnect };

/// A packet is accepted only in the phase that matches its gate exactly.
[[nodiscard]] GateDecision gate_packet(Phase session, Phase packet_gate);

/// At most `limit` packets are parsed per socket per cycle; the rest stay
/// buffered for the next cycle (delayed, not dropped).
class CycleBudget {
 public:
  explicit CycleBudget(std::uint32_t limit = 3) : limit_(limit) {}
  void begin_cycle() noexcept { used_ = 0; }
  [[nodiscard]] bool try_consume() noexcept;
  [[nodiscard]] std::uint32_t limit() const noexcept { return limit_; }

 private:
  std::uint32_t limit_;
  std::uint32_t used_ = 0;
};

}  // namespace netproto
