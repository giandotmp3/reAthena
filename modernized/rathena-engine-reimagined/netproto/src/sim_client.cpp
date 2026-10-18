#include "netproto/sim_client.hpp"

namespace netproto {

SimClient::SimClient(std::shared_ptr<const CodecSet> codecs, std::optional<ObfuscationKeys> keys, SimClientOptions o)
    : codecs_(std::move(codecs)),
      obf_(keys ? std::optional<OpcodeObfuscator>(*keys) : std::nullopt),
      opt_(std::move(o)),
      server_table_(codecs_->server_frames()),
      phase_(opt_.initial_phase) {}

void SimClient::close(const std::string& why) {
  if (closed_) return;
  closed_ = true;
  reason_ = why;
  buf_.clear();
}

Bytes SimClient::send(const DecodedPacket& p) {
  if (closed_) throw std::logic_error("SimClient is closed: " + reason_);
  const auto dir = codecs_->direction_of(p.name);
  if (!dir) throw CodecError(CodecErrc::kUnknownPacket, p.name);
  if (*dir != Direction::ClientToServer) throw CodecError(CodecErrc::kWrongDirection, p.name + " is not a client-to-server packet");
  const Phase gate = *codecs_->gate_of(p.name);
  if (opt_.enforce_gates && gate != phase_)
    throw PhaseViolation(p.name + " is not allowed in this session phase (gate " + std::to_string(static_cast<int>(gate)) +
                         ", phase " + std::to_string(static_cast<int>(phase_)) + ")");
  Bytes wire = codecs_->encode(p);
  if (obf_) {
    const std::uint16_t plain = static_cast<std::uint16_t>(wire[0] | (wire[1] << 8));
    const std::uint16_t enc = obf_->encode(plain);
    wire[0] = static_cast<std::uint8_t>(enc & 0xFF);
    wire[1] = static_cast<std::uint8_t>(enc >> 8);
  }
  if (p.name == opt_.load_end_packet && phase_ == Phase::PreMapLoad) phase_ = Phase::InWorld;
  ++sent_;
  return wire;
}

Bytes SimClient::send_raw(ByteView plain_frame) {
  if (closed_) throw std::logic_error("SimClient is closed: " + reason_);
  Bytes wire(plain_frame.begin(), plain_frame.end());
  if (obf_ && wire.size() >= 2) {
    const std::uint16_t enc = obf_->encode(static_cast<std::uint16_t>(wire[0] | (wire[1] << 8)));
    wire[0] = static_cast<std::uint8_t>(enc & 0xFF);
    wire[1] = static_cast<std::uint8_t>(enc >> 8);
  }
  ++sent_;
  return wire;
}

std::vector<DecodedPacket> SimClient::receive(ByteView stream) {
  std::vector<DecodedPacket> out;
  if (closed_) return out;
  if (!buffer_within_limit(buf_.size() + stream.size(), FrameLimits{})) {
    close("receive buffer limit exceeded");
    return out;
  }
  buf_.insert(buf_.end(), stream.begin(), stream.end());
  std::size_t pos = 0;
  while (pos < buf_.size() && !closed_) {
    const ByteView rest(buf_.data() + pos, buf_.size() - pos);
    const FrameResult r = peek_frame(rest, server_table_);
    if (r.status == FrameStatus::kNeedMore) break;
    if (r.status == FrameStatus::kUnknownOpcode) { close("unknown server opcode"); break; }
    if (r.status == FrameStatus::kInvalidLength) { close("invalid declared length"); break; }
    DecodedPacket p;
    const CodecErrc e = codecs_->try_decode(rest.first(r.length), Direction::ServerToClient, p);
    if (e != CodecErrc::kOk) { close(std::string("undecodable server frame: ") + to_string(e)); break; }
    pos += r.length;
    ++received_;
    if (phase_ == Phase::PreSession && p.name.rfind(opt_.accept_prefix, 0) == 0) phase_ = Phase::PreMapLoad;
    if (p.name == "ZC_REFUSE_ENTER" || p.name == "SC_NOTIFY_BAN") {
      out.push_back(std::move(p));
      close("refused by the server");
      break;
    }
    out.push_back(std::move(p));
  }
  if (!closed_) buf_.erase(buf_.begin(), buf_.begin() + static_cast<std::ptrdiff_t>(pos));
  return out;
}

}  // namespace netproto
