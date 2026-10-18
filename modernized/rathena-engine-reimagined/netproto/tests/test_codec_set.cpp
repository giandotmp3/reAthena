#include "fixtures.hpp"
#include "harness.hpp"

using namespace netproto;
using namespace fx;

TEST("CAP-24", registry_negotiates_only_supported_versions_without_fallback) {
  CodecRegistry reg(catalog(), {v2021()});
  CHECK(reg.negotiate(v2021()) != nullptr);
  CHECK(reg.negotiate({20211104, Flavour::Main}) == nullptr);  // no "closest" fallback
  CHECK(reg.negotiate({20211103, Flavour::Re}) == nullptr);
}

TEST("CAP-24", registry_shares_one_codec_set_per_version) {
  CodecRegistry reg(catalog(), {v2021()});
  CHECK(reg.negotiate(v2021()).get() == reg.negotiate(v2021()).get());
}

TEST("CAP-E02", name_and_opcode_lookup_are_inverse_per_direction) {
  const auto s = CodecSet::build(catalog(), v2021());
  CHECK(*s.opcode_of("CZ_REQUEST_MOVE", Direction::ClientToServer) == 0x0085);
  CHECK(!s.opcode_of("CZ_REQUEST_MOVE", Direction::ServerToClient));
  CHECK(*s.name_of(0x0085) == "CZ_REQUEST_MOVE");
}

TEST("CAP-E02", decode_request_move_golden_bytes) {
  const auto s = CodecSet::build(catalog(), v2021());
  const auto p = s.decode(bytes({0x85, 0x00, 0x25, 0x8C, 0x85}));
  CHECK(p.name == "CZ_REQUEST_MOVE");
  CHECK(std::get<Pos3>(p.fields.at("dest")) == (Pos3{150, 200, 5}));
}

TEST("CAP-E02", encode_then_decode_roundtrips) {
  const auto s = CodecSet::build(catalog(), v2021());
  DecodedPacket p; p.name = "CZ_REQUEST_MOVE"; p.fields["dest"] = Pos3{10, 20, 3};
  CHECK(s.decode(s.encode(p)).fields.at("dest") == p.fields.at("dest"));
}

TEST("BR-INTER-013", decode_rejects_declared_length_that_disagrees_with_the_buffer) {
  const auto s = CodecSet::build(catalog(), v2021());
  // Declares 32 bytes but only 4 are present: decode must report a typed error, never read past the end.
  bool rejected = false;
  try { (void)s.decode(bytes({0x04, 0x0b, 0x20, 0x00})); }
  catch (const NotImplementedError&) { throw; }
  catch (const CodecError& e) { rejected = e.code == CodecErrc::kLengthMismatch; }
  CHECK(rejected);
}

TEST("CAP-E02", field_width_changes_by_version_nameid_u16_to_u32) {
  // The same semantic packet decodes with a u16 id on an old client and a u32 id on a new one: two layouts
  // of one name, selected by the negotiated (date, flavour); the field is the same name in both.
  PacketCatalog c;
  PacketDef d;
  d.name = "CZ_USE_ITEM"; d.direction = Direction::ClientToServer; d.gate = Phase::InWorld;
  PacketVersion oldv; oldv.applies = pred("MAIN>=20000000 && MAIN<20180000"); oldv.opcode = 0x00a7;
  oldv.length = fixed(6); oldv.fields = {field("name_id", 2, FieldType::U16), field("target", 4, FieldType::U16)};
  PacketVersion newv; newv.applies = pred("MAIN>=20180000"); newv.opcode = 0x00a7;
  newv.length = fixed(10); newv.fields = {field("name_id", 2, FieldType::U32), field("target", 6, FieldType::U32)};
  d.versions = {oldv, newv};
  c.add(d);
  const auto old_set = CodecSet::build(c, {20100101, Flavour::Main});
  const auto new_set = CodecSet::build(c, {20211103, Flavour::Main});
  const auto a = old_set.decode(bytes({0xa7, 0, 0x34, 0x12, 0x78, 0x56}));
  CHECK(std::get<std::int64_t>(a.fields.at("name_id")) == 0x1234);
  const auto b = new_set.decode(bytes({0xa7, 0, 0x34, 0x12, 0x00, 0x01, 0x78, 0x56, 0x00, 0x00}));
  CHECK(std::get<std::int64_t>(b.fields.at("name_id")) == 0x01001234);
  CHECK(std::get<std::int64_t>(b.fields.at("target")) == 0x5678);
  // a frame of the old length is not a frame of the new client
  bool rejected = false;
  try { (void)new_set.decode(bytes({0xa7, 0, 0x34, 0x12, 0x78, 0x56})); } catch (const CodecError& e) { rejected = e.code == CodecErrc::kLengthMismatch; }
  CHECK(rejected);
  DecodedPacket p; p.name = "CZ_USE_ITEM"; p.fields["name_id"] = std::int64_t{40000}; p.fields["target"] = std::int64_t{7};
  CHECK(old_set.decode(old_set.encode(p)).fields == p.fields);
  p.fields["name_id"] = std::int64_t{70000};  // does not fit the old u16 field: an error, not a truncation
  bool overflow = false;
  try { (void)old_set.encode(p); } catch (const CodecError& e) { overflow = e.code == CodecErrc::kBadValue; }
  CHECK(overflow);
  CHECK(new_set.decode(new_set.encode(p)).fields == p.fields);
}
