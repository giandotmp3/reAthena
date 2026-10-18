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

PENDING_TEST("CAP-E02", decode_request_move_golden_bytes) {
  const auto s = CodecSet::build(catalog(), v2021());
  const auto p = s.decode(bytes({0x85, 0x00, 0x25, 0x8C, 0x85}));
  CHECK(p.name == "CZ_REQUEST_MOVE");
  CHECK(std::get<Pos3>(p.fields.at("dest")) == (Pos3{150, 200, 5}));
}

PENDING_TEST("CAP-E02", encode_then_decode_roundtrips) {
  const auto s = CodecSet::build(catalog(), v2021());
  DecodedPacket p; p.name = "CZ_REQUEST_MOVE"; p.fields["dest"] = Pos3{10, 20, 3};
  CHECK(s.decode(s.encode(p)).fields.at("dest") == p.fields.at("dest"));
}

PENDING_TEST("BR-INTER-013", decode_rejects_declared_length_that_disagrees_with_the_buffer) {
  const auto s = CodecSet::build(catalog(), v2021());
  // Declares 32 bytes but only 4 are present: decode must report a typed error, never read past the end.
  bool rejected = false;
  try { (void)s.decode(bytes({0x04, 0x0b, 0x20, 0x00})); }
  catch (const NotImplementedError&) { throw; }
  catch (const std::exception&) { rejected = true; }
  CHECK(rejected);
}

PENDING_TEST("CAP-E02", field_width_changes_by_version_nameid_u16_to_u32) {
  // The same semantic packet decodes with a u16 id on an old client and a u32 id on a new one.
  CHECK(false);
}
