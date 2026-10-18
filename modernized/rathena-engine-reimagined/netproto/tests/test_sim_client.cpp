#include "fixtures.hpp"
#include "harness.hpp"
#include "netproto/sim_client.hpp"

using namespace netproto;
using namespace fx;

PENDING_TEST("CAP-E02", sim_client_sends_a_move_request_the_server_codec_decodes) {
  CodecRegistry reg(catalog(), {v2021()});
  const auto codecs = reg.negotiate(v2021());
  SimClient client(codecs, std::nullopt);
  DecodedPacket p; p.name = "CZ_REQUEST_MOVE"; p.fields["dest"] = Pos3{10, 20, 3};
  const auto wire = client.send(p);
  CHECK(codecs->decode(wire).name == "CZ_REQUEST_MOVE");
}

PENDING_TEST("CAP-24", sim_client_obfuscates_opcodes_when_keyed) {
  const ObfuscationKeys keys{{1, 2, 3}};  // FAKE
  SimClient client(CodecRegistry(catalog(), {v2021()}).negotiate(v2021()), keys);
  DecodedPacket p; p.name = "CZ_NOTIFY_ACTORINIT";
  const auto wire = client.send(p);
  CHECK(wire.size() == 2 && (wire[0] | (wire[1] << 8)) != 0x007d);
}
