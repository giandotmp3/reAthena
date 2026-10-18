#include "fixtures.hpp"
#include "real_catalog.hpp"
#include "harness.hpp"
#include "netproto/sim_client.hpp"

using namespace netproto;
using namespace fx;

TEST("CAP-E02", sim_client_sends_a_move_request_the_server_codec_decodes) {
  CodecRegistry reg(catalog(), {v2021()});
  const auto codecs = reg.negotiate(v2021());
  SimClient client(codecs, std::nullopt);
  DecodedPacket p; p.name = "CZ_REQUEST_MOVE"; p.fields["dest"] = Pos3{10, 20, 3};
  const auto wire = client.send(p);
  CHECK(codecs->decode(wire).name == "CZ_REQUEST_MOVE");
}

TEST("CAP-24", sim_client_obfuscates_opcodes_when_keyed) {
  // FAKE keys. The scaffold used {1, 2, 3}, which the legacy schedule maps to an all-zero mask ((1*2+3)>>16 == 0
  // and the mask stays 0 for the next 16 packets), i.e. "no obfuscation"; these give a non-zero mask.
  const ObfuscationKeys keys{{0x1A2B3C4Du, 0x00010001u, 0x0000BEEFu}};
  SimClient client(CodecRegistry(catalog(), {v2021()}).negotiate(v2021()), keys);
  DecodedPacket p; p.name = "CZ_NOTIFY_ACTORINIT";
  const auto wire = client.send(p);
  CHECK(wire.size() == 2 && (wire[0] | (wire[1] << 8)) != 0x007d);
}

namespace {
const ObfuscationKeys kKeys{{0x1A2B3C4Du, 0x00010001u, 0x0000BEEFu}};  // FAKE
std::shared_ptr<const CodecSet> real_codecs() { return std::make_shared<const CodecSet>(CodecSet::build(full_catalog(), kRe())); }
DecodedPacket pkt(const char* name) { DecodedPacket p; p.name = name; return p; }
}  // namespace

TEST("CAP-E02", sim_client_session_walks_the_phases_and_the_server_codec_agrees) {
  SimClientOptions o; o.enforce_gates = true;
  SimClient c(real_codecs(), kKeys, o);
  const CodecSet& srv = full_set();
  OpcodeObfuscator server_obf(kKeys);
  auto server_reads = [&](const Bytes& wire) {   // what the server does with a frame: de-obfuscate, frame, decode
    const FrameResult r = peek_frame(wire, srv.frames(), server_obf);
    CHECK(r.status == FrameStatus::kComplete && r.length == wire.size());
    Bytes plain = wire;
    plain[0] = r.opcode & 0xFF; plain[1] = r.opcode >> 8;
    server_obf.commit();
    return srv.decode(plain);
  };
  CHECK(c.phase() == Phase::PreSession);
  auto enter = pkt("CZ_ENTER");
  enter.fields = {{"account_id", std::int64_t{2000001}}, {"char_id", std::int64_t{150001}}, {"login_id1", std::int64_t{99}}, {"client_tick", std::int64_t{1234}}, {"sex", std::int64_t{1}}};
  const auto got = server_reads(c.send(enter));
  CHECK(got.name == "CZ_ENTER" && got.fields == enter.fields);
  // only the connect packet is allowed before the session exists
  bool refused = false;
  try { (void)c.send(pkt("CZ_NOTIFY_ACTORINIT")); } catch (const PhaseViolation&) { refused = true; }
  CHECK(refused);
  // the server accepts: ZC_ACCEPT_ENTER arrives in three fragments
  DecodedPacket acc = pkt("ZC_ACCEPT_ENTER");
  acc.fields = {{"startTime", std::int64_t{5000}}, {"posDir", Pos3{150, 200, 5}}, {"xSize", std::int64_t{5}}, {"ySize", std::int64_t{5}}, {"font", std::int64_t{0}}};
  const Bytes w = srv.encode(acc);
  CHECK(c.receive(ByteView(w.data(), 1)).empty() && c.buffered() == 1);
  CHECK(c.receive(ByteView(w.data() + 1, 5)).empty() && c.buffered() == 6);
  const auto in = c.receive(ByteView(w.data() + 6, w.size() - 6));
  CHECK(in.size() == 1 && in[0].fields == acc.fields && c.buffered() == 0);
  CHECK(c.phase() == Phase::PreMapLoad);
  refused = false;
  try { (void)c.send(pkt("CZ_REQUEST_TIME")); } catch (const PhaseViolation&) { refused = true; }
  CHECK(refused);                                    // in-world packets wait for the load-end ack
  CHECK(server_reads(c.send(pkt("CZ_NOTIFY_ACTORINIT"))).name == "CZ_NOTIFY_ACTORINIT");
  CHECK(c.phase() == Phase::InWorld);
  auto mv = pkt("CZ_REQUEST_MOVE");
  mv.fields["pos"] = Pos3{12, 34, 0};
  CHECK(std::get<Pos3>(server_reads(c.send(mv)).fields.at("pos")) == (Pos3{12, 34, 0}));
  refused = false;
  try { (void)c.send(enter); } catch (const PhaseViolation&) { refused = true; }
  CHECK(refused);                                    // no second connect packet
  CHECK(c.frames_sent() == 3 && c.frames_received() == 1);
}

TEST("CAP-E02", sim_client_rejects_server_packets_unknown_names_and_bad_values_on_send) {
  SimClient c(real_codecs(), std::nullopt);
  auto code = [&](const DecodedPacket& p) {
    try { (void)c.send(p); } catch (const CodecError& e) { return e.code; }
    return CodecErrc::kOk;
  };
  CHECK(code(pkt("NO_SUCH_PACKET")) == CodecErrc::kUnknownPacket);
  CHECK(code(pkt("ZC_NOTIFY_TIME")) == CodecErrc::kWrongDirection);
  auto bad = pkt("CZ_REQUEST_TIME"); bad.fields["client_tick"] = std::int64_t{1} << 40;
  CHECK(code(bad) == CodecErrc::kBadValue);
  auto typo = pkt("CZ_REQUEST_TIME"); typo.fields["tick"] = std::int64_t{1};
  CHECK(code(typo) == CodecErrc::kUnknownField);
  CHECK(c.frames_sent() == 0);  // nothing was sent
}

TEST("BR-MAP-059", sim_client_closes_on_a_malformed_server_stream) {
  const Bytes ok = full_set().encode([] { DecodedPacket p; p.name = "ZC_NOTIFY_TIME"; p.fields["time"] = std::int64_t{7}; return p; }());
  {  // unknown opcode
    SimClient c(real_codecs(), std::nullopt);
    CHECK(c.receive(bytes({0xFF, 0xEE, 0, 0})).empty() && c.closed() && c.close_reason() == "unknown server opcode");
    CHECK(c.receive(ok).empty());   // nothing is processed after the close
    bool threw = false;
    try { (void)c.send(pkt("CZ_NOTIFY_ACTORINIT")); } catch (const std::logic_error&) { threw = true; }
    CHECK(threw);
  }
  {  // invalid declared length on a variable server packet (ZC_NOTIFY_CHAT: header 8)
    SimClient c(real_codecs(), std::nullopt);
    const auto got = c.receive(bytes({0x8d, 0x00, 0x03, 0x00}));
    CHECK(got.empty() && c.closed() && c.close_reason() == "invalid declared length");
  }
  {  // good frames before the bad one are still delivered
    SimClient c(real_codecs(), std::nullopt);
    Bytes s = ok; s.insert(s.end(), ok.begin(), ok.end()); s.push_back(0xFF); s.push_back(0xEE);
    CHECK(c.receive(s).size() == 2 && c.closed());
  }
  {  // a refusal closes the client after delivering it
    SimClient c(real_codecs(), std::nullopt);
    DecodedPacket r = pkt("ZC_REFUSE_ENTER"); r.fields["errorCode"] = std::int64_t{1};
    const auto got = c.receive(full_set().encode(r));
    CHECK(got.size() == 1 && got[0].name == "ZC_REFUSE_ENTER" && c.closed());
  }
  {  // BR-INTER-012: the receive buffer is bounded
    SimClient c(real_codecs(), std::nullopt);
    Bytes big(30000, 0);
    big[0] = 0x8d; big[1] = 0x00; big[2] = 0xFF; big[3] = 0x7F;  // declares 32767: still incomplete at 30000 bytes
    CHECK(c.receive(big).empty() && !c.closed() && c.buffered() == 30000);
    CHECK(c.receive(Bytes(40000, 0)).empty() && c.closed() && c.close_reason() == "receive buffer limit exceeded");
  }
}

TEST("CAP-E02", sim_client_passes_length_only_server_frames_through_as_opaque_packets) {
  SimClient c(real_codecs(), std::nullopt);
  // 0x02ec is in the legacy packet DB with a length (67) and no struct or handler: an OPAQUE frame
  Bytes f(67, 0x5A); f[0] = 0xec; f[1] = 0x02;
  const auto got = c.receive(f);
  CHECK(got.size() == 1 && got[0].name == "OPAQUE_02EC" && std::get<Bytes>(got[0].fields.at("payload")).size() == 65);
  // but the server does not accept an opaque frame from a client
  CHECK(full_set().frames().count(0x02ec) == 0);
}

TEST("CAP-24", sim_client_send_raw_obfuscates_and_keeps_the_schedule_in_lockstep) {
  SimClient c(real_codecs(), kKeys);
  OpcodeObfuscator server(kKeys);
  Bytes plain = fx::fixed_frame(0x007d, 2);
  for (int i = 0; i < 20; ++i) {
    const Bytes wire = i % 2 ? c.send_raw(plain) : c.send(pkt("CZ_NOTIFY_ACTORINIT"));
    CHECK(server.decode(static_cast<std::uint16_t>(wire[0] | (wire[1] << 8))) == 0x007d);
  }
}
