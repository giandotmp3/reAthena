#pragma once
// Headless in-process client for rathena-sim: speaks codec-level packets over byte buffers, no sockets.
// A SimPlayer sits on top of this. It uses the same codec tables as the server, so a codec bug shows
// up on both sides identically; the golden byte vectors in tests/ (taken from the compiled legacy
// structs) are the independent oracle.
//
//   send()     encodes a client-to-server packet by name (optionally checked against the session phase),
//              and obfuscates its opcode when the session is keyed. One frame out.
//   receive()  takes any slice of the server byte stream (partial frames are buffered across calls),
//              splits it with the server frame table (typed packets and length-only frames) and decodes
//              every complete frame. A malformed length / unknown opcode closes the client, like the
//              server closes a bad client (BR-MAP-059, BR-INTER-012/013).
//
// Phase model (the same gates the server applies, see gating.hpp): PreSession -> (a packet named
// ZC_ACCEPT_ENTER* arrives) -> PreMapLoad -> (CZ_NOTIFY_ACTORINIT sent) -> InWorld. ZC_REFUSE_ENTER and
// SC_NOTIFY_BAN close the client. With `enforce_gates` a packet may only be sent in the phase that
// equals its gate, exactly like gate_packet(); without it the phase is still tracked but not enforced
// (the two-argument constructor, kept for tests that start in the middle of a session).
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "netproto/codec_set.hpp"
#include "netproto/framing.hpp"
#include "netproto/obfuscation.hpp"

namespace netproto {

struct SimClientOptions {
  bool enforce_gates = false;
  Phase initial_phase = Phase::PreSession;
  /// Receiving a packet whose name starts with this, while in PreSession, moves the client to PreMapLoad.
  std::string accept_prefix = "ZC_ACCEPT_ENTER";
  /// Name of the packet whose sending ends the map load (PreMapLoad -> InWorld).
  std::string load_end_packet = "CZ_NOTIFY_ACTORINIT";
  std::size_t max_buffered = 65536;  // BR-INTER-012
};

/// send() of a packet the current phase does not allow (only with enforce_gates).
struct PhaseViolation : std::runtime_error {
  using std::runtime_error::runtime_error;
};

class SimClient {
 public:
  SimClient(std::shared_ptr<const CodecSet> codecs, std::optional<ObfuscationKeys> keys, SimClientOptions o = {});

  /// Serialize one client-to-server packet, obfuscating the opcode if keyed. Throws CodecError (unknown
  /// name, wrong direction, bad value), PhaseViolation, or std::logic_error when the client is closed.
  [[nodiscard]] Bytes send(const DecodedPacket& p);
  /// Obfuscate (if keyed) and account for an already-encoded plain frame: for malformed-frame tests.
  [[nodiscard]] Bytes send_raw(ByteView plain_frame);
  /// Parse a server byte stream into packets. Never throws on bad input: it closes the client.
  [[nodiscard]] std::vector<DecodedPacket> receive(ByteView stream);

  [[nodiscard]] Phase phase() const noexcept { return phase_; }
  void set_phase(Phase p) noexcept { phase_ = p; }
  [[nodiscard]] bool closed() const noexcept { return closed_; }
  [[nodiscard]] const std::string& close_reason() const noexcept { return reason_; }
  [[nodiscard]] std::size_t buffered() const noexcept { return buf_.size(); }
  [[nodiscard]] std::uint64_t frames_sent() const noexcept { return sent_; }
  [[nodiscard]] std::uint64_t frames_received() const noexcept { return received_; }
  [[nodiscard]] const CodecSet& codecs() const noexcept { return *codecs_; }

 private:
  void close(const std::string& why);
  std::shared_ptr<const CodecSet> codecs_;
  std::optional<OpcodeObfuscator> obf_;
  SimClientOptions opt_;
  FrameTable server_table_;
  Bytes buf_;
  Phase phase_;
  bool closed_ = false;
  std::string reason_;
  std::uint64_t sent_ = 0, received_ = 0;
};

}  // namespace netproto
