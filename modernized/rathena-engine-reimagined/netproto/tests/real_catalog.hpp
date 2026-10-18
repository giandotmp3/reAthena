#pragma once
// The full derived catalog (proto/packets_re_20211103.yaml, compiled in through the generated tables) and
// the one real client target it covers. Shared by the codec, golden-vector, fuzz and compatibility tests.
#include "fixtures.hpp"
#include "generated/catalog_packets_re_20211103.hpp"

namespace fx {
inline const ClientVersion& kRe() {
  static const ClientVersion v{20211103, Flavour::Re};
  return v;
}
inline const PacketCatalog& full_catalog() {
  static const PacketCatalog c = generated::packets_re_20211103_catalog();
  return c;
}
inline const CodecSet& full_set() {
  static const CodecSet s = CodecSet::build(full_catalog(), kRe());
  return s;
}
inline const std::vector<ResolvedPacket>& full_resolved() {
  static const std::vector<ResolvedPacket> r = full_catalog().resolve(kRe());
  return r;
}
inline std::string slurp_file(const std::string& rel) {
  std::ifstream f(std::string(NETPROTO_SOURCE_DIR) + "/" + rel, std::ios::binary);
  if (!f) throw std::runtime_error("cannot open " + rel);
  std::ostringstream o;
  o << f.rdbuf();
  return o.str();
}
}  // namespace fx
