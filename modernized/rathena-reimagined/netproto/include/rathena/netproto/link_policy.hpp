#pragma once
// Link kinds and the actions a connection loop must take.
//
// BR-INTER-002: an unknown packet from a char server disconnects it. The
// rebuild applies the same fail-closed policy to every link: an opcode that is
// not in the link's packet table is never skipped or guessed at.

#include <cstdint>
#include <string>
#include <string_view>

namespace rathena::netproto {

enum class LinkKind : std::uint8_t {
  kClient,      // unmodified RO client (login/char/map)
  kServerLink,  // char->login, map->char (flag.server in legacy)
};

enum class LinkAction : std::uint8_t {
  kContinue,     // keep processing
  kWaitForData,  // incomplete frame; keep the bytes, read more
  kSendPing,     // server link stalled: send keepalive (BR-INTER-010)
  kDisconnect,   // close the link (set_eof in legacy)
};

[[nodiscard]] constexpr std::string_view to_string(LinkAction a) noexcept {
  switch (a) {
    case LinkAction::kContinue: return "continue";
    case LinkAction::kWaitForData: return "wait-for-data";
    case LinkAction::kSendPing: return "send-ping";
    case LinkAction::kDisconnect: return "disconnect";
  }
  return "unknown";
}

/// What the caller logs / dumps for an unknown packet. Deliberately carries no
/// payload bytes: payloads on inter-server links can contain credentials,
/// tickets and tokens (S2). Only the opcode and link identity are reported.
struct UnknownPacketReport {
  std::string link_name;
  LinkKind kind = LinkKind::kServerLink;
  std::uint16_t opcode = 0;
  LinkAction action = LinkAction::kDisconnect;

  [[nodiscard]] std::string message() const;
};

/// BR-INTER-002: unknown opcode -> disconnect, for every link kind.
[[nodiscard]] UnknownPacketReport on_unknown_packet(std::string_view link_name, LinkKind kind,
                                                    std::uint16_t opcode);

}  // namespace rathena::netproto
