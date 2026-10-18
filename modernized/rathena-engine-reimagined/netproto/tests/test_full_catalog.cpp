// The full derived catalog (proto/packets_re_20211103.yaml): counts by provenance, the codec built over
// every packet, random round trips of every layout, and real frames.
#include <map>
#include <random>

#include "harness.hpp"
#include "randval.hpp"
#include "real_catalog.hpp"

using namespace netproto;
using namespace fx;

namespace {
std::map<std::string, int> by_source() {
  std::map<std::string, int> m;
  for (const auto& rp : full_resolved()) ++m[rp.version->source];
  return m;
}
const ResolvedPacket* find(const char* name) {
  for (const auto& rp : full_resolved()) if (rp.def->name == name) return &rp;
  return nullptr;
}
}  // namespace

TEST("CAP-24", full_catalog_counts_by_provenance_are_the_documented_ones) {
  // before this work the catalog held 9 packets (5 with typed fields: 2 typed structs, 7 handler-parsed with no layout)
  const auto m = by_source();
  CHECK(full_resolved().size() == 1186);
  CHECK(m.at("typed-struct") == 494);     // src/map/packets_struct.hpp + packets.hpp, both CZ and ZC (PACKET_* and packet_*), 15 aliased names folded in
  CHECK(m.at("common-struct") == 50);     // src/common/packets.hpp: login / char / zone hand-off
  CHECK(m.at("handler-derived") == 109);  // straight RFIFO reads (incl. CH_ENTER from char_clif.cpp)
  CHECK(m.at("handler") == 54);           // handler present, layout not mechanical: fields unknown
  CHECK(m.at("length-only") == 479);      // packet DB lists only a length
  CHECK(m.size() == 5);
  std::size_t unknown = 0, known = 0;
  for (const auto& rp : full_resolved()) (rp.version->fields_known ? known : unknown)++;
  CHECK(known == 494 + 50 + 109 && unknown == 54 + 479);
}

TEST("CAP-24", full_codec_set_builds_and_validates_every_layout_with_unique_names_and_opcodes) {
  CHECK(full_set().packet_count() == 1186);
  CHECK(full_set().version() == kRe());
  CodecRegistry reg(full_catalog(), {kRe()});
  CHECK(reg.negotiate(kRe()) != nullptr);
  CHECK(reg.negotiate({20211103, Flavour::Main}) == nullptr);   // the catalog was derived for the RE client only
  CHECK(reg.negotiate({20211104, Flavour::Re}) == nullptr);
  CHECK(full_catalog().resolve({20211104, Flavour::Re}).empty());
  // the client-to-server table is what gating and framing use: clients only send cz packets
  const auto& t = full_set().frames();
  std::size_t cz = 0;
  for (const auto& rp : full_resolved()) cz += rp.def->direction == Direction::ClientToServer;
  CHECK(t.size() == cz && cz > 250);
  CHECK(t.at(0x0436).name == "CZ_ENTER" && t.at(0x0436).gate == Phase::PreSession && t.at(0x0436).length.fixed_size == 23);
  CHECK(t.at(0x007d).gate == Phase::PreMapLoad);
  CHECK(!t.count(0x0087) && !t.count(0x0074));  // server packets are not client frames
  const FrameTable s = full_set().server_frames();
  CHECK(s.at(0x0087).name == "ZC_NOTIFY_PLAYERMOVE" && s.at(0x0087).length.fixed_size == 12);
  CHECK(s.count(0x02eb) && s.count(0x0074) && s.count(0x0081));
  std::size_t opaque = 0;
  for (const auto& rp : full_resolved()) opaque += rp.def->direction == Direction::Any;
  CHECK(opaque == 479);
  CHECK(!t.count(0x0065 + 0x7000));
}

TEST("CAP-E02", every_known_layout_roundtrips_random_values_and_unknown_ones_pass_bytes_through) {
  std::mt19937_64 g(0xFEEDF00D);
  std::size_t checked = 0;
  for (const auto& rp : full_resolved()) {
    const PacketVersion& v = *rp.version;
    for (int it = 0; it < 12; ++it) {
      DecodedPacket p;
      p.name = rp.def->name;
      if (!v.fields_known) {
        const std::size_t n = v.length.kind == LengthKind::Fixed ? v.length.fixed_size - 2u : v.length.header_size - v.length.prefix_offset - 2u + g() % 30;
        Bytes b(n);
        for (auto& x : b) x = static_cast<std::uint8_t>(g());
        p.fields["payload"] = b;
      } else {
        p.repeats.assign(v.length.repeat ? static_cast<std::size_t>(it % 4) : 0, {});
        for (const FieldSpec& f : v.fields) {
          if (is_row_field(f)) for (auto& r : p.repeats) r[field_key(f)] = random_value(g, f);
          else p.fields[f.name] = random_value(g, f);
        }
      }
      const Bytes w = full_set().encode(p);
      const Direction d = rp.def->direction == Direction::ClientToServer ? Direction::ClientToServer : Direction::ServerToClient;
      const DecodedPacket q = full_set().decode(w, d);
      CHECK(q.name == p.name && q.fields == p.fields && q.repeats == p.repeats);
      CHECK(full_set().encode(q) == w);
      ++checked;
    }
  }
  CHECK(checked == 1186 * 12);
}

TEST("CAP-E02", real_client_frames_decode_with_the_derived_layouts) {
  // CZ_REQUEST_MOVE: the 3-byte packed destination (the spec starter's pos3 field, now derived from RFIFOPOS)
  const auto mv = full_set().decode(bytes({0x5f, 0x03, 0x25, 0x8C, 0x85}));
  CHECK(mv.name == "CZ_REQUEST_MOVE" && std::get<Pos3>(mv.fields.at("pos")) == (Pos3{150, 200, 5}));
  // CZ_ENTER (RE, 23 bytes): account, char, auth code, tick at 2, 6, 10, 14; sex at 22
  Bytes enter(23, 0);
  enter[0] = 0x36; enter[1] = 0x04; enter[2] = 0x01; enter[3] = 0x00; enter[4] = 0x1E; enter[6] = 0x07; enter[10] = 0xEF; enter[11] = 0xBE; enter[14] = 0x10; enter[22] = 1;
  const auto e = full_set().decode(enter);
  CHECK(e.name == "CZ_ENTER" && std::get<std::int64_t>(e.fields.at("account_id")) == 0x001E0001 && std::get<std::int64_t>(e.fields.at("char_id")) == 7);
  CHECK(std::get<std::int64_t>(e.fields.at("login_id1")) == 0xBEEF && std::get<std::int64_t>(e.fields.at("client_tick")) == 16 && std::get<std::int64_t>(e.fields.at("sex")) == 1);
  CHECK(full_set().encode(e) == enter);
  // CZ_REQUEST_TIME heartbeat and the load-end ack (no fields, 2 bytes)
  const auto tick = full_set().decode(bytes({0x60, 0x03, 0x78, 0x56, 0x34, 0x12}));
  CHECK(tick.name == "CZ_REQUEST_TIME" && std::get<std::int64_t>(tick.fields.at("client_tick")) == 0x12345678);
  const auto ack = full_set().decode(bytes({0x7d, 0x00}));
  CHECK(ack.name == "CZ_NOTIFY_ACTORINIT" && ack.fields.empty());
  // CZ_CONTACTNPC is a typed struct
  const auto npc = full_set().decode(bytes({0x90, 0x00, 0x34, 0x12, 0x00, 0x00, 0x01}));
  CHECK(npc.name == "CZ_CONTACTNPC" && std::get<std::int64_t>(npc.fields.at("AID")) == 0x1234 && std::get<std::int64_t>(npc.fields.at("type")) == 1);
  // chat: variable length with a tail
  const auto chat = full_set().decode(bytes({0xf3, 0x00, 0x08, 0x00, 'h', 'i', '!', 0}));
  CHECK(chat.name == "CZ_REQUEST_CHAT" && std::get<Bytes>(chat.fields.at("tail")) == (Bytes{'h', 'i', '!', 0}));
  // server side: ZC_NOTIFY_PLAYERMOVE carries the 6-byte packed src/dst
  DecodedPacket pm; pm.name = "ZC_NOTIFY_PLAYERMOVE"; pm.fields["moveStartTime"] = std::int64_t{1234};
  pm.fields["moveData"] = Pos6{10, 20, 30, 40, 8, 8};
  const Bytes w = full_set().encode(pm);
  CHECK(w.size() == 12 && full_set().decode(w, Direction::ServerToClient).fields == pm.fields);
}

TEST("CAP-E02", every_old_curated_packet_is_in_the_full_catalog_with_the_same_numbers) {
  struct Row { const char* name; std::uint16_t op; int len; };
  for (const Row& r : {Row{"CA_LOGIN", 0x0064, 55}, Row{"CZ_ENTER", 0x0436, 23}, Row{"CZ_NOTIFY_ACTORINIT", 0x007d, 2},
                       Row{"CZ_REQUEST_MOVE", 0x035f, 5}, Row{"CZ_REQUEST_TIME", 0x0360, 6}, Row{"CZ_REQUEST_CHAT", 0x00f3, -1},
                       Row{"CZ_CONTACTNPC", 0x0090, 7}, Row{"ZC_NOTIFY_PLAYERMOVE", 0x0087, 12}, Row{"ZC_NOTIFY_TIME", 0x007f, 6}}) {
    const auto* p = find(r.name);
    CHECK(p && p->version->opcode == r.op);
    if (r.len >= 0) CHECK(p->version->length.kind == LengthKind::Fixed && p->version->length.fixed_size == r.len);
    else CHECK(p->version->length.kind == LengthKind::Variable && p->version->length.prefix_offset == 2);
  }
  CHECK(find("CZ_REQUEST_MOVE")->version->source == "handler-derived" && find("CZ_REQUEST_MOVE")->version->fields_known);
  CHECK((find("CZ_ENTER")->version->handler_offsets == std::vector<std::uint16_t>{2, 6, 10, 14, 22}));
  CHECK(find("CZ_CONTACTNPC")->version->source == "typed-struct");
}

TEST("CAP-E02", gateway_login_and_char_packets_are_typed_from_the_common_structs) {
  for (const char* n : {"CA_LOGIN", "CA_LOGIN2", "CA_LOGIN3", "CA_LOGIN4", "CA_LOGIN_PCBANG", "CA_LOGIN_CHANNEL", "CA_SSO_LOGIN_REQ", "CA_REQ_HASH",
                        "CA_EXE_HASHCHECK", "CA_CONNECT_INFO_CHANGED", "CT_AUTH", "AC_REFUSE_LOGIN", "SC_NOTIFY_BAN", "AC_ACK_HASH", "TC_RESULT",
                        "AC_ACCEPT_LOGIN", "CH_SELECT_CHAR", "PING", "CH_CHARLIST_REQ", "CH_MAKE_CHAR", "CH_DELETE_CHAR", "CH_DELETE_CHAR3_RESERVED",
                        "CH_DELETE_CHAR3", "CH_DELETE_CHAR3_CANCEL", "CH_SECOND_PASSWD_ACK", "CH_MAKE_SECOND_PASSWD", "CH_EDIT_SECOND_PASSWD",
                        "CH_AVAILABLE_SECOND_PASSWD", "CH_REQ_IS_VALID_CHARNAME", "CH_REQ_CHANGE_CHARNAME", "CH_REQ_CHANGE_CHARACTER_SLOT",
                        "HC_REFUSE_ENTER", "HC_ACCEPT_ENTER", "HC_ACK_CHARINFO_PER_PAGE", "HC_ACCEPT_ENTER2", "HC_CHARLIST_NOTIFY", "HC_BLOCK_CHARACTER",
                        "HC_ACCEPT_MAKECHAR", "HC_REFUSE_MAKECHAR", "HC_ACCEPT_DELETECHAR", "HC_REFUSE_DELETECHAR", "HC_DELETE_CHAR3_RESERVED",
                        "HC_DELETE_CHAR3", "HC_DELETE_CHAR3_CANCEL", "HC_SECOND_PASSWD_LOGIN", "HC_NOTIFY_ZONESVR", "HC_ACK_IS_VALID_CHARNAME",
                        "HC_ACK_CHANGE_CHARNAME", "HC_ACK_CHANGE_CHARACTER_SLOT", "CH_ENTER", "ZC_REFUSE_ENTER", "ZC_ACCEPT_ENTER"}) {
    const auto* p = find(n);
    CHECK(p != nullptr);
    CHECK(p->version->fields_known);
  }
  CHECK(find("CH_ENTER")->version->source == "handler-derived" && find("CH_ENTER")->version->length.fixed_size == 17);
  CHECK(find("HC_ACCEPT_ENTER")->version->length.repeat->stride == 175);   // sizeof(CHARACTER_INFO) at RE 20211103
  CHECK(find("AC_ACCEPT_LOGIN")->version->length.repeat->stride == 160 && find("AC_ACCEPT_LOGIN")->version->length.header_size == 64);
  CHECK(find("HC_ACCEPT_MAKECHAR")->version->length.fixed_size == 177);
  CHECK(find("CA_LOGIN")->def->gate == Phase::PreSession && find("CH_MAKE_CHAR")->def->gate == Phase::PreMapLoad);
}
