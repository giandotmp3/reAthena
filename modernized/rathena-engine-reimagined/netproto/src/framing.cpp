#include "netproto/framing.hpp"

#include <algorithm>

namespace netproto {

namespace {
FrameResult peek_opcode(ByteView buf, std::uint16_t opcode, const FrameTable& table, const FrameLimits& limits) {
  FrameResult r;
  r.opcode = opcode;
  const auto it = table.find(r.opcode);
  if (it == table.end()) {
    r.status = FrameStatus::kUnknownOpcode;
    return r;
  }
  const LengthRule& rule = it->second.length;
  if (rule.kind == LengthKind::Fixed) {
    r.length = rule.fixed_size;
    r.status = buf.size() >= r.length ? FrameStatus::kComplete : FrameStatus::kNeedMore;
    return r;
  }
  const std::size_t need = static_cast<std::size_t>(rule.prefix_offset) + 2;
  if (buf.size() < need) return r;  // kNeedMore
  r.length = static_cast<std::uint32_t>(buf[rule.prefix_offset] | (buf[rule.prefix_offset + 1] << 8));
  const std::uint32_t floor_len = std::max<std::uint32_t>(limits.min_variable, rule.header_size);
  if (r.length < floor_len || r.length > limits.max_variable) {
    r.status = FrameStatus::kInvalidLength;
    return r;
  }
  r.status = buf.size() >= r.length ? FrameStatus::kComplete : FrameStatus::kNeedMore;
  return r;
}
}  // namespace

FrameResult peek_frame(ByteView buf, const FrameTable& table, const FrameLimits& limits) {
  if (buf.size() < 2) return FrameResult{};
  return peek_opcode(buf, static_cast<std::uint16_t>(buf[0] | (buf[1] << 8)), table, limits);
}

FrameResult peek_frame(ByteView buf, const FrameTable& table, const OpcodeObfuscator& obf, const FrameLimits& limits) {
  if (buf.size() < 2) return FrameResult{};
  // legacy clif_parse: the opcode is decoded before the table lookup; the key only advances for a valid frame
  return peek_opcode(buf, obf.peek_decode(static_cast<std::uint16_t>(buf[0] | (buf[1] << 8))), table, limits);
}

LinkAction action_for(FrameStatus s) {
  switch (s) {
    case FrameStatus::kComplete: return LinkAction::kContinue;
    case FrameStatus::kNeedMore: return LinkAction::kWait;
    case FrameStatus::kInvalidLength:
    case FrameStatus::kUnknownOpcode: return LinkAction::kDisconnect;
  }
  return LinkAction::kDisconnect;
}

bool buffer_within_limit(std::size_t buffered, const FrameLimits& limits) {
  return buffered <= limits.max_buffered;
}

FrameTable make_frame_table(const std::vector<ResolvedPacket>& resolved) {
  FrameTable t;
  for (const auto& rp : resolved) {
    if (rp.def->direction != Direction::ClientToServer) continue;
    t.emplace(rp.version->opcode, FrameEntry{rp.def->name, rp.version->length, rp.def->gate});
  }
  return t;
}

}  // namespace netproto
