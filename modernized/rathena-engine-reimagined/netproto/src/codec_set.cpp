#include "netproto/codec_set.hpp"

namespace netproto {

CodecSet CodecSet::build(const PacketCatalog& catalog, const ClientVersion& v) {
  CodecSet s;
  s.version_ = v;
  const auto resolved = catalog.resolve(v);
  s.frames_ = make_frame_table(resolved);
  for (const auto& rp : resolved) {
    s.by_name_.emplace(std::make_pair(rp.def->direction, rp.def->name), rp.version->opcode);
    s.known_.insert(rp.def->name);
  }
  return s;
}

std::optional<std::string> CodecSet::name_of(std::uint16_t opcode) const {
  const auto it = frames_.find(opcode);
  if (it == frames_.end()) return std::nullopt;
  return it->second.name;
}

std::optional<std::uint16_t> CodecSet::opcode_of(const std::string& name, Direction d) const {
  const auto it = by_name_.find({d, name});
  if (it == by_name_.end()) return std::nullopt;
  return it->second;
}

DecodedPacket CodecSet::decode(ByteView) const { not_implemented("CodecSet::decode"); }
Bytes CodecSet::encode(const DecodedPacket&) const { not_implemented("CodecSet::encode"); }

std::shared_ptr<const CodecSet> CodecRegistry::negotiate(const ClientVersion& v) {
  if (!supported_.count(v)) return nullptr;
  if (auto it = cache_.find(v); it != cache_.end()) return it->second;
  auto set = std::make_shared<const CodecSet>(CodecSet::build(catalog_, v));
  cache_.emplace(v, set);
  return set;
}

}  // namespace netproto
