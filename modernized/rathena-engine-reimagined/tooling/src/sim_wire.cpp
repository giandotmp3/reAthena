#include "tooling/sim_wire.hpp"

#include "generated/catalog_packets_re_20211103.hpp"

namespace tooling::sim::wire {

const std::shared_ptr<const netproto::CodecSet>& codecs() {
  static const std::shared_ptr<const netproto::CodecSet> set = std::make_shared<const netproto::CodecSet>(
      netproto::CodecSet::build(netproto::generated::packets_re_20211103_catalog(), {20211103, netproto::Flavour::Re}));
  return set;
}

int64_t Packet::num(const std::string& k, int64_t dflt) const {
  auto it = fields.find(k);
  if (it == fields.end()) return dflt;
  if (auto* i = std::get_if<int64_t>(&it->second)) return *i;
  return dflt;
}
std::string Packet::text(const std::string& k) const {
  auto it = fields.find(k);
  if (it == fields.end()) return {};
  if (auto* s = std::get_if<std::string>(&it->second)) return *s;
  if (auto* b = std::get_if<Bytes>(&it->second)) {
    std::string s;
    for (uint8_t c : *b) {
      if (c == 0) break;
      s += static_cast<char>(c);
    }
    return s;
  }
  return {};
}
netproto::Pos3 Packet::pos3(const std::string& k) const {
  auto it = fields.find(k);
  if (it != fields.end())
    if (auto* p = std::get_if<netproto::Pos3>(&it->second)) return *p;
  return {};
}
netproto::Pos6 Packet::pos6(const std::string& k) const {
  auto it = fields.find(k);
  if (it != fields.end())
    if (auto* p = std::get_if<netproto::Pos6>(&it->second)) return *p;
  return {};
}

Packet make_packet(netproto::DecodedPacket p, ByteView frame, uint16_t opcode) {
  Packet out;
  static_cast<netproto::DecodedPacket&>(out) = std::move(p);
  out.opcode = opcode;
  out.raw.assign(frame.begin(), frame.end());
  return out;
}

Bytes frame(const std::string& name, Fields fields) {
  netproto::DecodedPacket p;
  p.name = name;
  for (const auto& [k, v] : fields) p.fields[k] = v;
  return codecs()->encode(p);
}

Bytes chat_tail(const std::string& text) {
  Bytes b(text.begin(), text.end());
  b.push_back(0);
  return b;
}

Split split_frames(ByteView stream, const netproto::FrameTable& table) {
  Split out;
  size_t pos = 0;
  while (pos < stream.size()) {
    auto r = netproto::peek_frame(stream.subspan(pos), table);
    if (r.status == netproto::FrameStatus::kNeedMore) break;
    if (r.status != netproto::FrameStatus::kComplete) {
      out.error = r.status == netproto::FrameStatus::kUnknownOpcode ? "unknown opcode" : "invalid declared length";
      break;
    }
    out.frames.emplace_back(stream.begin() + static_cast<std::ptrdiff_t>(pos), stream.begin() + static_cast<std::ptrdiff_t>(pos + r.length));
    pos += r.length;
  }
  out.consumed = pos;
  return out;
}

// struct packet_idle_unit / packet_unit_walking of src/map/packets_struct.hpp at PACKETVER 20211103 (RE flavour: the
// `shield` word is present); the catalog names them LEGACY_IDLE_UNIT / LEGACY_UNIT_WALKING (no legacy source names them).
Bytes actor_frame(const Actor& a, bool walking) {
  const int64_t gid = a.gid;
  if (!walking)
    return frame("LEGACY_IDLE_UNIT", {{"objecttype", I(a.object_type)}, {"AID", I(gid)}, {"GID", I(gid)},
                                       {"speed", I(static_cast<int16_t>(a.speed))}, {"job", I(a.job)},
                                       {"sex", I(a.sex)}, {"PosDir", netproto::Pos3{a.x, a.y, static_cast<uint8_t>(a.dir)}},
                                       {"xSize", I(5)}, {"ySize", I(5)}, {"clevel", I(a.level)}, {"maxHP", I(a.max_hp)},
                                       {"HP", I(a.hp)}, {"name", a.name}});
  return frame("LEGACY_UNIT_WALKING", {{"objecttype", I(a.object_type)}, {"AID", I(gid)}, {"GID", I(gid)},
                                        {"speed", I(static_cast<int16_t>(a.speed))}, {"job", I(a.job)},
                                        {"moveStartTime", I(a.move_start)}, {"sex", I(a.sex)},
                                        {"MoveData", netproto::Pos6{a.x, a.y, a.to_x, a.to_y, 8, 8}}, {"xSize", I(5)},
                                        {"ySize", I(5)}, {"clevel", I(a.level)}, {"maxHP", I(a.max_hp)}, {"HP", I(a.hp)},
                                        {"name", a.name}});
}

}  // namespace tooling::sim::wire
