#include "harness.hpp"
#include "netproto/obfuscation.hpp"

using namespace netproto;

namespace { const ObfuscationKeys kFake{{0x1A2B3C4Du, 0x00010001u, 0x0000BEEFu}}; }  // FAKE, never a client key

PENDING_TEST("CAP-24", decode_is_stateful_same_wire_value_decodes_differently_as_state_advances) {
  OpcodeObfuscator a(kFake);
  const auto first = a.decode(0x1234);
  const auto second = a.decode(0x1234);
  CHECK(first != second);
}

PENDING_TEST("CAP-24", two_decoders_with_equal_keys_stay_in_lockstep_and_reset_restarts) {
  OpcodeObfuscator a(kFake), b(kFake);
  for (int i = 0; i < 100; ++i) CHECK(a.decode(0x0085) == b.decode(0x0085));
  a.reset();
  OpcodeObfuscator c(kFake);
  CHECK(a.decode(0x0085) == c.decode(0x0085));
}

PENDING_TEST("CAP-24", encode_decode_roundtrip_over_a_long_sequence) {
  OpcodeObfuscator client(kFake), server(kFake);
  for (std::uint16_t op = 0; op < 2000; ++op) CHECK(server.decode(client.encode(op & 0x7FFF)) == (op & 0x7FFF));
}
