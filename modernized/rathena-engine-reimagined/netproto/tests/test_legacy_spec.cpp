// Tests over the REAL spec: the catalog generated from proto/client_packets.yaml, whose
// opcodes/lengths/offsets/fields are verified against the legacy headers by
// `netproto_codegen.py --verify-legacy` (ctest verify_legacy). The other test files use a
// hand-built synthetic catalog (fixtures.hpp); these use no hand-typed legacy value except
// the expectations written out below, each of which is the legacy table's answer at the target.
#include "fixtures.hpp"
#include "generated/catalog_client_packets.hpp"
#include "generated/frames_main_20211103.hpp"
#include "generated/frames_re_20211103.hpp"
#include "harness.hpp"

using namespace netproto;
using namespace fx;

namespace {
const ClientVersion kRe{20211103, Flavour::Re};      // what a stock build selects (PACKETVER_RE)
const ClientVersion kMain{20211103, Flavour::Main};  // hypothetical: no legacy build selects it

const PacketCatalog& real() {
  static const PacketCatalog c = generated::client_packets_catalog();
  return c;
}
const ResolvedPacket* find(const std::vector<ResolvedPacket>& r, const char* name) {
  for (const auto& rp : r) if (rp.def->name == name) return &rp;
  return nullptr;
}
Phase gate_of(const CodecSet& s, std::uint16_t opcode) { return s.frames().at(opcode).gate; }
}  // namespace

TEST("CAP-24", real_catalog_version_table_lookup_by_date_and_flavour) {
  CodecRegistry reg(real(), {kRe, kMain});
  CHECK(reg.negotiate(kRe) != nullptr);
  CHECK(reg.negotiate(kMain) != nullptr);
  CHECK(reg.negotiate({20211104, Flavour::Re}) == nullptr);   // verified exactly one date
  CHECK(reg.negotiate({20211103, Flavour::Zero}) == nullptr);
  CHECK(reg.negotiate(kRe).get() != reg.negotiate(kMain).get());
}

TEST("CAP-24", real_catalog_resolves_without_collisions_on_both_targets) {
  // 9 packets in the spec, each exists on both targets
  CHECK(real().resolve(kRe).size() == 9);
  CHECK(real().resolve(kMain).size() == 9);
  CHECK(real().resolve({20211104, Flavour::Re}).empty());  // nothing is claimed beyond what was verified
}

TEST("CAP-24", connect_packet_is_0x0436_and_its_length_differs_by_flavour) {
  // legacy: clif_shuffle.hpp, RE>=20211103 -> 23 bytes (account, char, auth, tick at 2,6,10,14; sex at 22)
  //         otherwise 19 bytes (sex at 18).
  const auto re = CodecSet::build(real(), kRe);
  const auto main = CodecSet::build(real(), kMain);
  CHECK(*re.opcode_of("CZ_ENTER", Direction::ClientToServer) == 0x0436);
  CHECK(*main.opcode_of("CZ_ENTER", Direction::ClientToServer) == 0x0436);
  CHECK(re.frames().at(0x0436).length.fixed_size == 23);
  CHECK(main.frames().at(0x0436).length.fixed_size == 19);
  // the offsets the legacy handler reads are kept; their types are not guessed
  const auto rr = real().resolve(kRe);
  const auto* enter = find(rr, "CZ_ENTER");
  CHECK(enter && !enter->version->fields_known && enter->version->fields.empty());
  CHECK((enter->version->handler_offsets == std::vector<std::uint16_t>{2, 6, 10, 14, 22}));
}

TEST("BR-MAP-059", framing_of_the_real_connect_packet_waits_for_all_23_bytes) {
  const auto t = CodecSet::build(real(), kRe).frames();
  CHECK(peek_frame(fixed_frame(0x0436, 22), t).status == FrameStatus::kNeedMore);
  const auto done = peek_frame(fixed_frame(0x0436, 23), t);
  CHECK(done.status == FrameStatus::kComplete && done.length == 23 && done.opcode == 0x0436);
  const auto t19 = CodecSet::build(real(), kMain).frames();
  CHECK(peek_frame(fixed_frame(0x0436, 19), t19).status == FrameStatus::kComplete);
}

TEST("BR-MAP-059", real_fixed_length_packets_frame_with_legacy_sizes) {
  const auto t = CodecSet::build(real(), kRe).frames();
  struct Row { std::uint16_t op; std::uint32_t len; const char* name; };
  // opcode, bytes: from the legacy table at the target (WalkToXY, TickSend, LoadEndAck, CONTACTNPC)
  for (const Row& r : {Row{0x035f, 5, "CZ_REQUEST_MOVE"}, Row{0x0360, 6, "CZ_REQUEST_TIME"},
                       Row{0x007d, 2, "CZ_NOTIFY_ACTORINIT"}, Row{0x0090, 7, "CZ_CONTACTNPC"},
                       Row{0x0064, 55, "CA_LOGIN"}}) {
    CHECK(t.at(r.op).name == r.name);
    const auto ok = peek_frame(fixed_frame(r.op, r.len), t);
    CHECK(ok.status == FrameStatus::kComplete && ok.length == r.len);
    if (r.len > 2) CHECK(peek_frame(fixed_frame(r.op, r.len - 1), t).status == FrameStatus::kNeedMore);
  }
}

TEST("BR-INTER-013", real_variable_length_chat_validates_the_declared_length) {
  // GlobalMessage (CZ_REQUEST_CHAT): legacy len -1, length word at offset 2, db opcode 0x00f3
  const auto s = CodecSet::build(real(), kRe);
  const auto& rule = s.frames().at(0x00f3).length;
  CHECK(rule.kind == LengthKind::Variable && rule.prefix_offset == 2 && rule.header_size == 4);
  const auto t = s.frames();
  CHECK(peek_frame(variable_frame(0x00f3, 4), t).status == FrameStatus::kComplete);
  CHECK(peek_frame(variable_frame(0x00f3, 3), t).status == FrameStatus::kInvalidLength);
  CHECK(peek_frame(variable_frame(0x00f3, 0), t).status == FrameStatus::kInvalidLength);  // the zero-length S4 case
  CHECK(peek_frame(variable_frame(0x00f3, 32768), t).status == FrameStatus::kComplete);
  CHECK(peek_frame(bytes({0xf3, 0x00, 0x01, 0x80}), t).status == FrameStatus::kInvalidLength);  // 32769
  // declared 40 but only 12 bytes buffered: wait
  Bytes part = variable_frame(0x00f3, 40); part.resize(12);
  CHECK(peek_frame(part, t).status == FrameStatus::kNeedMore);
}

TEST("BR-MAP-059", real_unknown_opcodes_disconnect) {
  const auto t = CodecSet::build(real(), kRe).frames();
  // 0xEEEE was never a packet; 0x0085 and 0x008c were the old starter's move/chat opcodes and are
  // overwritten in the legacy table at this version (ChangeDir, GetCharNameRequest), so the
  // semantic spec does not frame them; 0x0087 is a server-to-client packet.
  for (std::uint16_t op : {0xEEEE, 0x0085, 0x008c, 0x0087, 0x0072}) {
    const auto r = peek_frame(fixed_frame(op, 8), t);
    CHECK(r.status == FrameStatus::kUnknownOpcode);
    CHECK(action_for(r.status) == LinkAction::kDisconnect);
  }
}

TEST("BR-MAP-059", real_gates_phase_by_phase) {
  const auto s = CodecSet::build(real(), kRe);
  CHECK(gate_packet(Phase::PreSession, gate_of(s, 0x0436)) == GateDecision::kAllow);
  CHECK(gate_packet(Phase::PreSession, gate_of(s, 0x007d)) == GateDecision::kDisconnect);
  CHECK(gate_packet(Phase::PreSession, gate_of(s, 0x035f)) == GateDecision::kDisconnect);
  CHECK(gate_packet(Phase::PreMapLoad, gate_of(s, 0x007d)) == GateDecision::kAllow);
  CHECK(gate_packet(Phase::PreMapLoad, gate_of(s, 0x0436)) == GateDecision::kDisconnect);
  CHECK(gate_packet(Phase::PreMapLoad, gate_of(s, 0x00f3)) == GateDecision::kDisconnect);
  for (std::uint16_t op : {0x035f, 0x0360, 0x00f3, 0x0090}) {
    CHECK(gate_packet(Phase::InWorld, gate_of(s, op)) == GateDecision::kAllow);
    CHECK(gate_packet(Phase::PreSession, gate_of(s, op)) == GateDecision::kDisconnect);
  }
  CHECK(gate_packet(Phase::InWorld, gate_of(s, 0x0436)) == GateDecision::kDisconnect);  // no replay
  CHECK(gate_packet(Phase::InWorld, gate_of(s, 0x007d)) == GateDecision::kDisconnect);
}

TEST("CAP-E02", typed_legacy_structs_give_fields_and_handler_parsed_packets_do_not) {
  const auto rr = real().resolve(kRe);
  const auto* npc = find(rr, "CZ_CONTACTNPC");   // PACKET_CZ_CONTACTNPC {i16 type; u32 AID; u8 type}
  CHECK(npc && npc->version->fields_known && npc->version->fields.size() == 2);
  CHECK(npc->version->fields[0].name == "AID" && npc->version->fields[0].offset == 2 &&
        npc->version->fields[0].type == FieldType::U32);
  CHECK(npc->version->fields[1].name == "type" && npc->version->fields[1].offset == 6 &&
        npc->version->fields[1].type == FieldType::U8);
  const auto* mv = find(rr, "ZC_NOTIFY_PLAYERMOVE");  // struct-only server packet, 12 bytes
  CHECK(mv && mv->version->length.fixed_size == 12 && mv->version->opcode == 0x0087);
  CHECK(mv->version->fields.size() == 2 && mv->version->fields[1].array_len == 6);
  for (const char* n : {"CZ_REQUEST_MOVE", "CZ_REQUEST_TIME", "CZ_REQUEST_CHAT", "CZ_ENTER", "CA_LOGIN"}) {
    const auto* p = find(rr, n);
    CHECK(p && !p->version->fields_known && p->version->fields.empty());  // never guessed
  }
  CHECK((find(rr, "CZ_REQUEST_MOVE")->version->handler_offsets == std::vector<std::uint16_t>{2}));
  CHECK((find(rr, "CZ_REQUEST_CHAT")->version->handler_offsets == std::vector<std::uint16_t>{2, 4}));
}

TEST("CAP-24", one_opcode_two_names_across_targets_is_still_a_spec_error_not_silent) {
  // the real catalog plus a synthetic packet that steals CZ_ENTER's opcode must fail to resolve
  auto c = generated::client_packets_catalog();
  c.add(packet("CZ_STEAL", Direction::ClientToServer, Phase::InWorld, 0x0436, fixed(2), "RE>=20211103"));
  bool threw = false;
  try { (void)c.resolve(kRe); } catch (const SpecError&) { threw = true; }
  CHECK(threw);
}

namespace {
void same_frames(const FrameTable& a, const FrameTable& b) {
  CHECK(a.size() == b.size());
  for (const auto& [op, ea] : a) {
    const auto it = b.find(op);
    CHECK(it != b.end());
    CHECK(ea.name == it->second.name && ea.gate == it->second.gate);
    CHECK(ea.length.kind == it->second.length.kind);
    CHECK(ea.length.fixed_size == it->second.length.fixed_size || ea.length.kind == LengthKind::Variable);
    CHECK(ea.length.prefix_offset == it->second.length.prefix_offset || ea.length.kind == LengthKind::Fixed);
    CHECK(ea.length.header_size == it->second.length.header_size || ea.length.kind == LengthKind::Fixed);
  }
}
}  // namespace

TEST("CAP-E02", generated_frame_tables_equal_the_tables_built_from_the_generated_catalog) {
  same_frames(generated::frames_re_20211103(), CodecSet::build(real(), kRe).frames());
  same_frames(generated::frames_main_20211103(), CodecSet::build(real(), kMain).frames());
  CHECK(generated::frames_re_20211103().size() == 7);  // client-to-server packets only
}
