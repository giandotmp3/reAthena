#pragma once
// API STUB (pending): per-connection state machine for an inter-server link.
//
// Combines framing, the handshake gate and the stall policy:
//   - before registration only the handshake opcode (0x2710 on login,
//     0x2af8 on char) is framed; anything else disconnects (BR-INTER-001/002)
//   - after a successful handshake the link's post-auth packet table is used
//     (BR-INTER-007) and the negotiated protocol version selects layouts such
//     as 0x2717 v0/v1 (BR-INTER-003)
//   - tick() applies the stall policy (BR-INTER-010)
//
// Not implemented yet: every member throws std::logic_error. Its acceptance
// tests are GTEST_SKIP-ed and tagged with their rule IDs.

#include <cstdint>
#include <optional>
#include <vector>

#include "rathena/netproto/frame.hpp"
#include "rathena/netproto/inter/version.hpp"
#include "rathena/netproto/link_policy.hpp"
#include "rathena/netproto/stall_policy.hpp"

namespace rathena::netproto {

enum class LinkRole : std::uint8_t {
  kLoginAcceptsChar,  // login-server side of char->login
  kCharAcceptsMap,    // char-server side of map->char
};

struct LinkEvent {
  LinkAction action = LinkAction::kContinue;
  std::optional<std::uint16_t> opcode;
  std::vector<std::uint8_t> packet;  // complete frame, if any
};

class LinkSession {
 public:
  LinkSession(LinkRole role, StallPolicy stall);

  /// Feed received bytes; returns one event per complete frame or fault.
  std::vector<LinkEvent> on_bytes(ByteView data, TimePoint now);

  /// Mark the handshake as accepted at `version` (switches to post-auth table).
  void mark_registered(inter::ProtocolVersion version);

  /// Apply the stall policy.
  LinkAction tick(TimePoint now) const;

  [[nodiscard]] bool registered() const noexcept;
  [[nodiscard]] std::optional<inter::ProtocolVersion> version() const noexcept;
  [[nodiscard]] LinkRole role() const noexcept { return role_; }
  [[nodiscard]] const StallPolicy& stall_policy() const noexcept { return stall_; }

 private:
  LinkRole role_;
  StallPolicy stall_;
};

}  // namespace rathena::netproto
