#pragma once
// Stream framing: `cmd:u16 [len:u16] payload`, little-endian.
// BR-INTER-012 (size limits), BR-INTER-013 + S4 (declared length validation),
// BR-MAP-059 (unknown opcode disconnects).
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>

#include "netproto/common.hpp"
#include "netproto/obfuscation.hpp"
#include "netproto/spec.hpp"

namespace netproto {

struct FrameLimits {
  std::uint32_t min_variable = 4;
  std::uint32_t max_variable = 32768;
  /// Upper bound for the per-session receive buffer (BR-INTER-012).
  std::uint32_t max_buffered = 65536;
};

struct FrameEntry {
  std::string name;
  LengthRule length;
  Phase gate = Phase::InWorld;
};

/// Opcode -> framing data for one session's negotiated client version.
using FrameTable = std::unordered_map<std::uint16_t, FrameEntry>;

enum class FrameStatus : std::uint8_t {
  kNeedMore,        // wait for more bytes (bounded by max_buffered)
  kComplete,        // `length` bytes form one packet
  kInvalidLength,   // declared length < max(min_variable, header) or > max
  kUnknownOpcode,   // not in the table
};

struct FrameResult {
  FrameStatus status = FrameStatus::kNeedMore;
  std::uint16_t opcode = 0;
  std::uint32_t length = 0;  // declared / fixed length when known
};

enum class LinkAction : std::uint8_t { kContinue, kWait, kDisconnect };

/// Inspect the front of `buf` without consuming it. Detects an invalid length
/// as soon as the 4-byte header is present, never waiting for the payload.
[[nodiscard]] FrameResult peek_frame(ByteView buf, const FrameTable& table,
                                     const FrameLimits& limits = {});

/// Same, for a session with opcode obfuscation: the wire opcode is de-obfuscated (without advancing
/// `obf`) before the table lookup; `result.opcode` is the PLAIN opcode. Call obf.commit() once the
/// frame was accepted and consumed (BR-MAP-059).
[[nodiscard]] FrameResult peek_frame(ByteView buf, const FrameTable& table, const OpcodeObfuscator& obf,
                                     const FrameLimits& limits = {});

[[nodiscard]] LinkAction action_for(FrameStatus s);

/// True while the session receive buffer is within its bound (BR-INTER-012).
[[nodiscard]] bool buffer_within_limit(std::size_t buffered, const FrameLimits& limits = {});

/// Client-to-server frame table for `v` (opcodes are plain, not obfuscated).
[[nodiscard]] FrameTable make_frame_table(const std::vector<ResolvedPacket>& resolved);

}  // namespace netproto
