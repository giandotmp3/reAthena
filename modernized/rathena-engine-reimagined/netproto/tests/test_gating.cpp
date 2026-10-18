#include "fixtures.hpp"
#include "harness.hpp"

using namespace netproto;
using namespace fx;

namespace {
GateDecision gate_for(Phase session, std::uint16_t opcode) {
  const auto t = table2021();
  return gate_packet(session, t.at(opcode).gate);
}
}  // namespace

TEST("BR-MAP-059", pre_session_accepts_only_the_connect_packet) {
  CHECK(gate_for(Phase::PreSession, 0x0436) == GateDecision::kAllow);
  CHECK(gate_for(Phase::PreSession, 0x007d) == GateDecision::kDisconnect);
  CHECK(gate_for(Phase::PreSession, 0x0085) == GateDecision::kDisconnect);
}

TEST("BR-MAP-059", pre_map_load_accepts_only_load_end_ack) {
  CHECK(gate_for(Phase::PreMapLoad, 0x007d) == GateDecision::kAllow);
  CHECK(gate_for(Phase::PreMapLoad, 0x0085) == GateDecision::kDisconnect);
  CHECK(gate_for(Phase::PreMapLoad, 0x0436) == GateDecision::kDisconnect);
}

TEST("BR-MAP-059", in_world_rejects_connect_and_load_end_replays) {
  CHECK(gate_for(Phase::InWorld, 0x0085) == GateDecision::kAllow);
  CHECK(gate_for(Phase::InWorld, 0x0436) == GateDecision::kDisconnect);
  CHECK(gate_for(Phase::InWorld, 0x007d) == GateDecision::kDisconnect);
}

TEST("BR-MAP-059", at_most_three_packets_per_socket_per_cycle) {
  CycleBudget b;
  CHECK(b.try_consume() && b.try_consume() && b.try_consume());
  CHECK(!b.try_consume());  // the 4th stays buffered
  b.begin_cycle();
  CHECK(b.try_consume());
}

PENDING_TEST("BR-MAP-059", obfuscated_opcode_is_deobfuscated_before_table_lookup_and_gating) {
  // Stateful: the 2nd frame decodes with the key advanced by the 1st.
  const ObfuscationKeys keys{{0x11111111u, 0x22222222u, 0x33333333u}};  // FAKE
  OpcodeObfuscator client(keys), server(keys);
  const auto w1 = client.encode(0x007d);
  const auto w2 = client.encode(0x0085);
  CHECK(server.decode(w1) == 0x007d);
  CHECK(server.decode(w2) == 0x0085);
}
