#include "harness.hpp"
#include <cstdlib>

#include "fixtures.hpp"
#include "netproto/obfuscation.hpp"

using namespace netproto;

namespace { const ObfuscationKeys kFake{{0x1A2B3C4Du, 0x00010001u, 0x0000BEEFu}}; }  // FAKE, never a client key

TEST("CAP-24", decode_is_stateful_same_wire_value_decodes_differently_as_state_advances) {
  OpcodeObfuscator a(kFake);
  const auto first = a.decode(0x1234);
  const auto second = a.decode(0x1234);
  CHECK(first != second);
}

TEST("CAP-24", two_decoders_with_equal_keys_stay_in_lockstep_and_reset_restarts) {
  OpcodeObfuscator a(kFake), b(kFake);
  for (int i = 0; i < 100; ++i) CHECK(a.decode(0x0085) == b.decode(0x0085));
  a.reset();
  OpcodeObfuscator c(kFake);
  CHECK(a.decode(0x0085) == c.decode(0x0085));
}

TEST("CAP-24", encode_decode_roundtrip_over_a_long_sequence) {
  OpcodeObfuscator client(kFake), server(kFake);
  for (std::uint16_t op = 0; op < 2000; ++op) CHECK(server.decode(client.encode(op & 0x7FFF)) == (op & 0x7FFF));
}

namespace {
struct ObfVec { ObfuscationKeys keys; std::vector<std::pair<std::uint16_t, std::uint16_t>> pairs; };
const std::vector<ObfVec>& legacy_vectors() {
  // tests/legacy_ref/obf_vectors.inc is printed by the compiled legacy routine (obfuscation_ref.cpp, FAKE keys)
  static const std::vector<ObfVec> v = {
#include "legacy_ref/obf_vectors.inc"
  };
  return v;
}
}  // namespace

TEST("CAP-24", obfuscator_matches_the_compiled_legacy_schedule_vector_for_vector) {
  CHECK(legacy_vectors().size() == 4);
  for (const auto& vec : legacy_vectors()) {
    OpcodeObfuscator server(vec.keys), client(vec.keys);
    CHECK(vec.pairs.size() == 40);
    for (const auto& [plain, wire] : vec.pairs) {
      CHECK(client.encode(plain) == wire);
      CHECK(server.decode(wire) == plain);
    }
    CHECK(server.committed() == 40 && client.committed() == 40);
  }
}

TEST("CAP-24", first_packet_uses_the_connect_mask_and_the_session_key_starts_at_f_of_f) {
  const ObfuscationKeys k{{0x1A2B3C4Du, 0x00010001u, 0x0000BEEFu}};
  OpcodeObfuscator o(k);
  // legacy: cmd ^ (((k0*k1 + k2) >> 16) & 0x7FFF) for a connection without a session
  const std::uint32_t f0 = k.k[0] * k.k[1] + k.k[2];
  CHECK(o.next_mask() == ((f0 >> 16) & 0x7FFF));
  CHECK(o.committed() == 0);
  (void)o.decode(0x1234);
  // clif_parse_WantToConnection: sd->cryptKey = f(f(k0)); the connect packet does NOT advance it
  const std::uint32_t f1 = f0 * k.k[1] + k.k[2];
  CHECK(o.state() == f1);
  CHECK(o.next_mask() == ((f1 >> 16) & 0x7FFF));
  (void)o.decode(0x1234);
  CHECK(o.state() == f1 * k.k[1] + k.k[2]);  // advanced after the second packet
}

TEST("CAP-24", peek_decode_does_not_advance_so_an_invalid_frame_leaves_the_key_alone) {
  const ObfuscationKeys k{{0x11111111u, 0x22222222u, 0x33333333u}};
  OpcodeObfuscator o(k), ref(k);
  for (int i = 0; i < 5; ++i) {
    const std::uint16_t wire = static_cast<std::uint16_t>(0x1000 + i);
    CHECK(o.peek_decode(wire) == o.peek_decode(wire));
    CHECK(o.committed() == static_cast<std::uint64_t>(i));
    (void)o.decode(wire);
    (void)ref.decode(wire);
  }
  CHECK(o.state() == ref.state());
}

TEST("CAP-24", mask_never_touches_bit_15_so_plain_opcodes_below_0x8000_stay_below) {
  const ObfuscationKeys k{{0xDEADBEEFu, 0x0BADF00Du, 0x00C0FFEEu}};
  OpcodeObfuscator c(k);
  for (int i = 0; i < 5000; ++i) CHECK((c.encode(0x0000) & 0x8000) == 0);
}

TEST("CAP-24", key_text_parses_strictly_and_comes_from_the_environment_only) {
  CHECK(parse_keys("0x1A2B3C4D,0x00010001,0xBEEF").value() == (ObfuscationKeys{{0x1A2B3C4Du, 0x00010001u, 0xBEEFu}}));
  CHECK(parse_keys("1 2 3").value() == (ObfuscationKeys{{1, 2, 3}}));
  CHECK(parse_keys("4294967295, 0, 0x0").value().k[0] == 0xFFFFFFFFu);
  for (const char* bad : {"", "1,2", "1,2,3,4", "1,2,x", "0x,1,2", "4294967296,1,2", "0x100000000,1,2", "1,,2", "0xZZ,1,2", "-1,2,3"})
    CHECK(!parse_keys(bad));
  ::setenv("NETPROTO_TEST_KEYS_FAKE", "0x11111111,0x22222222,0x33333333", 1);
  CHECK(keys_from_env("NETPROTO_TEST_KEYS_FAKE").value() == (ObfuscationKeys{{0x11111111u, 0x22222222u, 0x33333333u}}));
  ::unsetenv("NETPROTO_TEST_KEYS_FAKE");
  CHECK(!keys_from_env("NETPROTO_TEST_KEYS_FAKE"));   // unset: no keys, and no default
  CHECK(!keys_from_env(""));
}

TEST("CAP-24", framing_deobfuscates_the_opcode_then_looks_it_up_and_commits_only_complete_frames) {
  const ObfuscationKeys k{{0x1A2B3C4Du, 0x00010001u, 0x0000BEEFu}};
  const FrameTable t = fx::table2021();
  OpcodeObfuscator client(k), server(k);
  auto frame = [&](std::uint16_t plain, std::size_t size) {
    Bytes b = fx::fixed_frame(plain, size);
    const std::uint16_t w = client.encode(plain);
    b[0] = w & 0xFF; b[1] = w >> 8;
    return b;
  };
  const Bytes f1 = frame(0x0436, 19), f2 = frame(0x007d, 2), f3 = frame(0x0436, 19);
  // the second frame arrives in two TCP segments: peeking must not consume key state
  Bytes stream = f1;
  auto r = peek_frame(stream, t, server);
  CHECK(r.status == FrameStatus::kComplete && r.opcode == 0x0436 && r.length == 19);
  server.commit();
  stream.assign(f2.begin(), f2.begin() + 1);
  CHECK(peek_frame(stream, t, server).status == FrameStatus::kNeedMore);
  stream = f2;
  r = peek_frame(stream, t, server);
  CHECK(r.status == FrameStatus::kComplete && r.opcode == 0x007d);
  CHECK(peek_frame(stream, t, server).opcode == 0x007d);  // peeking twice gives the same answer
  server.commit();
  r = peek_frame(f3, t, server);
  CHECK(r.status == FrameStatus::kComplete && r.opcode == 0x0436);
  // wrong key: the opcode decodes to garbage, which is an unknown opcode -> disconnect (BR-MAP-059)
  OpcodeObfuscator wrong(ObfuscationKeys{{1, 2, 3}});
  CHECK(peek_frame(f1, t, wrong).status == FrameStatus::kUnknownOpcode);
}
