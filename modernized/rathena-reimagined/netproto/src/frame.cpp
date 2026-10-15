#include "rathena/netproto/frame.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace rathena::netproto {

PacketTable::PacketTable(std::string name, FrameLimits limits) : name_(std::move(name)), limits_(limits) {}

PacketTable& PacketTable::add(PacketSpec spec) {
  if (spec.kind == PacketSpec::Kind::kFixed && spec.size < kOpcodeSize) {
    throw std::invalid_argument("fixed packet smaller than its opcode");
  }
  if (spec.kind == PacketSpec::Kind::kVariable && spec.size < kVariableHeaderSize) {
    throw std::invalid_argument("variable packet header smaller than cmd+len");
  }
  if (!specs_.emplace(spec.opcode, spec).second) {
    throw std::invalid_argument("duplicate opcode in packet table");
  }
  return *this;
}

PacketTable& PacketTable::merge(const PacketTable& other) {
  for (const auto& [op, spec] : other.specs_) add(spec);
  return *this;
}

PacketTable PacketTable::from_legacy_lengths(std::string name, FrameLimits limits, std::uint16_t base,
                                             std::span<const std::int32_t> lengths) {
  PacketTable table(std::move(name), limits);
  for (std::size_t i = 0; i < lengths.size(); ++i) {
    const auto op = static_cast<std::uint16_t>(base + i);
    const std::int32_t len = lengths[i];
    if (len == 0) continue;
    if (len == -1) {
      table.add(PacketSpec::variable(op));
    } else if (len > 0 && len <= 0xFFFF) {
      table.add(PacketSpec::fixed(op, static_cast<std::uint16_t>(len)));
    } else {
      throw std::invalid_argument("invalid legacy packet length");
    }
  }
  return table;
}

const PacketSpec* PacketTable::find(std::uint16_t opcode) const noexcept {
  const auto it = specs_.find(opcode);
  return it == specs_.end() ? nullptr : &it->second;
}

FrameResult peek_frame(ByteView buffer, const PacketTable& table) noexcept {
  if (buffer.size() < kOpcodeSize) return {FrameStatus::kNeedMore, 0, 0};

  const std::uint16_t opcode = read_u16le(buffer, 0);
  const PacketSpec* spec = table.find(opcode);
  if (spec == nullptr) return {FrameStatus::kUnknownPacket, opcode, 0};

  if (spec->kind == PacketSpec::Kind::kFixed) {
    if (buffer.size() < spec->size) return {FrameStatus::kNeedMore, opcode, spec->size};
    return {FrameStatus::kComplete, opcode, spec->size};
  }

  if (buffer.size() < kVariableHeaderSize) return {FrameStatus::kNeedMore, opcode, 0};

  const std::uint16_t declared = read_u16le(buffer, 2);
  const auto& limits = table.limits();
  // S4 / BR-INTER-013: declared length must cover the packet's own header.
  const std::uint32_t min_len = std::max<std::uint32_t>(limits.min_variable_len, spec->size);
  if (declared < min_len || declared > limits.max_variable_len) {
    return {FrameStatus::kInvalidLength, opcode, declared};
  }
  if (buffer.size() < declared) return {FrameStatus::kNeedMore, opcode, declared};
  return {FrameStatus::kComplete, opcode, declared};
}

LinkAction action_for(FrameStatus status) noexcept {
  switch (status) {
    case FrameStatus::kComplete: return LinkAction::kContinue;
    case FrameStatus::kNeedMore: return LinkAction::kWaitForData;
    case FrameStatus::kUnknownPacket:
    case FrameStatus::kInvalidLength: return LinkAction::kDisconnect;
  }
  return LinkAction::kDisconnect;
}

DrainResult drain_frames(ByteView buffer, const PacketTable& table,
                         const std::function<void(std::uint16_t, ByteView)>& on_frame) {
  DrainResult out;
  while (true) {
    const ByteView rest = buffer.subspan(out.consumed);
    if (rest.empty()) {
      out.action = LinkAction::kContinue;
      return out;
    }
    const FrameResult fr = peek_frame(rest, table);
    const LinkAction act = action_for(fr.status);
    if (act == LinkAction::kDisconnect) {
      out.action = act;
      out.fault = fr;
      return out;
    }
    if (act == LinkAction::kWaitForData) {
      out.action = act;
      return out;
    }
    if (on_frame) on_frame(fr.opcode, rest.first(fr.length));
    out.consumed += fr.length;
    ++out.frames;
  }
}

}  // namespace rathena::netproto
