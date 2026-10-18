#pragma once
// Test fixtures. All values are FAKE and SYNTHETIC: this hand-built catalog exercises the
// framing/gating/negotiation logic with made-up layouts (e.g. CZ_ENTER_OLD and CZ_USE_ITEM_OLD
// share 0x0072, CZ_VAR_EXAMPLE is invented) and is NOT the legacy data. The real, verified
// opcodes/lengths live in proto/client_packets.yaml; tests over them are in test_legacy_spec.cpp
// (generated catalog, see generated/catalog_client_packets.hpp).
#include "netproto/codec_set.hpp"
#include "netproto/connect_check.hpp"
#include "netproto/flood_guard.hpp"
#include "netproto/framing.hpp"
#include "netproto/gating.hpp"
#include "netproto/obfuscation.hpp"
#include "netproto/pos_codec.hpp"
#include <fstream>
#include <sstream>

namespace fx {
using namespace netproto;

inline FieldSpec field(const char* n, std::uint16_t off, FieldType t, std::uint16_t w = 0) {
  FieldSpec f; f.name = n; f.offset = off; f.type = t; f.width = w; return f;
}
inline LengthRule fixed(std::uint16_t n) { LengthRule l; l.fixed_size = n; return l; }
inline LengthRule variable(std::uint16_t header) {
  LengthRule l; l.kind = LengthKind::Variable; l.header_size = header; return l;
}
inline VersionPredicate pred(const char* s) { return *VersionPredicate::parse(s); }

inline PacketDef packet(const char* name, Direction d, Phase gate, std::uint16_t opcode,
                        LengthRule len, const char* applies = "MAIN>=20000000",
                        std::vector<FieldSpec> fields = {}) {
  PacketDef def; def.name = name; def.direction = d; def.gate = gate;
  PacketVersion pv; pv.applies = pred(applies); pv.opcode = opcode; pv.length = len;
  pv.fields = std::move(fields);
  def.versions.push_back(std::move(pv));
  return def;
}

inline ClientVersion v2021() { return {20211103, Flavour::Main}; }

inline PacketCatalog catalog() {
  PacketCatalog c;
  c.add(packet("CA_LOGIN", Direction::ClientToServer, Phase::PreSession, 0x0064, fixed(55)));
  c.add(packet("CZ_ENTER", Direction::ClientToServer, Phase::PreSession, 0x0436, fixed(19),
               "MAIN>=20180000",
               {field("account_id", 2, FieldType::U32), field("char_id", 6, FieldType::U32),
                field("login_id1", 10, FieldType::U32), field("client_tick", 14, FieldType::U32),
                field("sex", 18, FieldType::U8)}));
  c.add(packet("CZ_NOTIFY_ACTORINIT", Direction::ClientToServer, Phase::PreMapLoad, 0x007d, fixed(2)));
  c.add(packet("CZ_REQUEST_MOVE", Direction::ClientToServer, Phase::InWorld, 0x0085, fixed(5), "MAIN>=20000000",
               {field("dest", 2, FieldType::Pos3)}));
  c.add(packet("CZ_VAR_EXAMPLE", Direction::ClientToServer, Phase::InWorld, 0x0b04, variable(10)));
  c.add(packet("ZC_NOTIFY_PLAYERMOVE", Direction::ServerToClient, Phase::InWorld, 0x0087, fixed(12)));
  // 0x0072 is CZ_ENTER in an older era and CZ_USE_ITEM_OLD in another flavour.
  c.add(packet("CZ_ENTER_OLD", Direction::ClientToServer, Phase::PreSession, 0x0072, fixed(19),
               "MAIN>=20040000 && MAIN<20180000"));
  c.add(packet("CZ_USE_ITEM_OLD", Direction::ClientToServer, Phase::InWorld, 0x0072, fixed(8),
               "RE>=20100000"));
  return c;
}

inline Bytes bytes(std::initializer_list<int> v) {
  Bytes b; for (int x : v) b.push_back(static_cast<std::uint8_t>(x)); return b;
}
inline Bytes variable_frame(std::uint16_t op, std::uint16_t len) {
  Bytes b(len < 4 ? 4 : len, 0xAB);
  b[0] = op & 0xFF; b[1] = op >> 8; b[2] = len & 0xFF; b[3] = len >> 8;
  return b;
}
inline Bytes fixed_frame(std::uint16_t op, std::size_t size) {
  Bytes b(size < 2 ? 2 : size, 0);
  b[0] = op & 0xFF; b[1] = op >> 8; return b;
}
inline FrameTable table2021() { return CodecSet::build(catalog(), v2021()).frames(); }
}  // namespace fx
