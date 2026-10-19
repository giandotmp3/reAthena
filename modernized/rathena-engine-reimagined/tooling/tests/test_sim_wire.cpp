// The sim's wire vocabulary against netproto's data: the sim has no codec of its own any more. Every opcode and length
// it speaks is checked against the legacy packet DB extract (netproto proto/legacy_20211103.yaml) and the full
// catalog; the frames it builds are byte-identical to the ones its retired private codec produced (recorded below
// before that codec was deleted); and the client half of a session is netproto::SimClient.
// TL-SIM-05x are tooling-local ids (architecture section 8: "SimPlayer speaks codec-level packets").
#include <fstream>
#include <sstream>

#include "harness.hpp"
#include "netproto/sim_client.hpp"
#include "tooling/sim_value.hpp"
#include "tooling/sim_wire.hpp"

using namespace tooling::sim;

namespace {

struct Block {
  std::string text;
};
// All `- opcode: 0xNNNN` blocks of the legacy extract.
std::vector<std::pair<unsigned, std::string>> legacy_blocks() {
  std::ifstream in(std::string(TOOLING_SOURCE_DIR) + "/../netproto/proto/legacy_20211103.yaml");
  if (!in) throw CheckFailure("cannot read netproto/proto/legacy_20211103.yaml");
  std::vector<std::pair<unsigned, std::string>> out;
  std::string line;
  while (std::getline(in, line)) {
    if (line.rfind("  - opcode: 0x", 0) == 0) {
      out.emplace_back(static_cast<unsigned>(std::stoul(line.substr(14), nullptr, 16)), line + "\n");
    } else if (!out.empty() && line.rfind("  - ", 0) != 0 && line.rfind("    ", 0) == 0) {
      out.back().second += line + "\n";
    } else if (!out.empty() && line.rfind("  - ", 0) == 0) {
      // another list item that is not an opcode block (cannot happen in this file): ignore
    }
  }
  return out;
}

// text before the `shadows:` list: the live definition
std::string live_part(const std::string& block) {
  auto p = block.find("shadows:");
  return p == std::string::npos ? block : block.substr(0, p);
}

bool block_has_len(const std::string& b, int len) {
  const std::string live = live_part(b);
  // typed variable-length server packets list a `repeat:` group and no length
  if (len < 0 && live.find("    repeat:") != std::string::npos) return true;
  const std::string want_len = "    len: " + std::to_string(len) + "\n";
  const std::string want_size = "    size: " + std::to_string(len) + "\n";
  return live.find(want_len) != std::string::npos || live.find(want_size) != std::string::npos;
}

}  // namespace

namespace {

struct Spoken {
  const char* name;
  uint16_t opcode;
  int len;  // -1: variable
  bool c2s;
};
// the 29 packets rathena-sim speaks (names are the catalog's)
const std::vector<Spoken>& spoken() {
  static const std::vector<Spoken> v = {
      {"CZ_ENTER", 0x0436, 23, true},          {"CZ_NOTIFY_ACTORINIT", 0x007d, 2, true}, {"CZ_REQUEST_MOVE", 0x035f, 5, true},
      {"CZ_REQUEST_TIME", 0x0360, 6, true},    {"CZ_REQUEST_CHAT", 0x00f3, -1, true},    {"CZ_CONTACTNPC", 0x0090, 7, true},
      {"CZ_REQ_NEXT_SCRIPT", 0x00b9, 6, true}, {"CZ_CHOOSE_MENU", 0x00b8, 7, true},      {"CZ_CLOSE_DIALOG", 0x0146, 6, true},
      {"CZ_INPUT_EDITDLG", 0x0143, 10, true},  {"CZ_INPUT_EDITDLGSTR", 0x01d5, -1, true}, {"CZ_REQUEST_ACT2", 0x0437, 7, true},
      {"CZ_USE_SKILL2", 0x0438, 10, true},
      {"ZC_ACCEPT_ENTER", 0x02eb, 13, false},  {"ZC_REFUSE_ENTER", 0x0074, 3, false},    {"ZC_NOTIFY_TIME", 0x007f, 6, false},
      {"ZC_NOTIFY_PLAYERMOVE", 0x0087, 12, false}, {"ZC_STOPMOVE", 0x0088, 10, false},   {"ZC_NOTIFY_VANISH", 0x0080, 7, false},
      {"LEGACY_IDLE_UNIT", 0x09ff, -1, false}, {"LEGACY_UNIT_WALKING", 0x09fd, -1, false}, {"ZC_NOTIFY_CHAT", 0x008d, -1, false},
      {"OPAQUE_008E", 0x008e, -1, false},      {"ZC_SAY_DIALOG", 0x00b4, -1, false},     {"ZC_WAIT_DIALOG", 0x00b5, 6, false},
      {"ZC_CLOSE_DIALOG", 0x00b6, 6, false},   {"ZC_MENU_LIST", 0x00b7, -1, false},      {"ZC_OPEN_EDITDLGSTR", 0x01d4, 6, false},
      {"ZC_NOTIFY_ACT", 0x08c8, 34, false}};
  return v;
}

std::string hex(const wire::Bytes& b) {
  static const char d[] = "0123456789abcdef";
  std::string s;
  for (uint8_t c : b) { s += d[c >> 4]; s += d[c & 15]; }
  return s;
}

// What the retired private codec (tooling/src/sim_wire.cpp at commit 66141ec) produced for these arguments;
// recorded with a throw-away program before the file was deleted.
const std::vector<std::pair<std::string, std::string>>& retired_codec_frames() {
  static const std::vector<std::pair<std::string, std::string>> v = {
    {"CZ_ENTER", "360481841e0082841e0078563412d20400000000000001"},
    {"CZ_NOTIFY_ACTORINIT", "7d00"},
    {"CZ_REQUEST_MOVE", "5f03258c93"},
    {"CZ_REQUEST_TIME", "6003efbeadde"},
    {"CZ_REQUEST_CHAT", "f3000d006e656f203a20686900"},
    {"CZ_CONTACTNPC", "9000474b4c0001"},
    {"CZ_REQ_NEXT_SCRIPT", "b900474b4c00"},
    {"CZ_CHOOSE_MENU", "b800474b4c00ff"},
    {"CZ_CLOSE_DIALOG", "4601474b4c00"},
    {"CZ_INPUT_EDITDLG", "4301474b4c00d6ffffff"},
    {"CZ_INPUT_EDITDLGSTR", "d5010c00474b4c0061626300"},
    {"CZ_REQUEST_ACT2", "3704484b4c0007"},
    {"CZ_USE_SKILL2", "380405001c0092100000"},
    {"ZC_ACCEPT_ENTER", "eb026300000019065205050000"},
    {"ZC_REFUSE_ENTER", "740006"},
    {"ZC_NOTIFY_TIME", "7f000df0feca"},
    {"ZC_NOTIFY_PLAYERMOVE", "870005000000004024b19088"},
    {"ZC_STOPMOVE", "8800414b4c006b006400"},
    {"ZC_NOTIFY_VANISH", "8000414b4c0003"},
    {"LEGACY_IDLE_UNIT", "ff096c0006414b4c00414b4c00960000000000000000002d00000000000000000000000000000000000000000000000000000000000000000000000000000103022405050009000000a00f0000ac0d0000000000626f62000000000000000000000000000000000000000000"},
    {"LEGACY_UNIT_WALKING", "fd09720006414b4c00414b4c00960000000000000000002d00000000000000000000000000e11000000000000000000000000000000000000000000000000000000001030220e04e88050509000000a00f0000ac0d0000000000626f62000000000000000000000000000000000000000000"},
    {"ZC_NOTIFY_CHAT", "8d001600414b4c00616c696365203a2068656c6c6f00"},
    {"OPAQUE_008E", "8e001200616c696365203a2068656c6c6f00"},
    {"ZC_SAY_DIALOG", "b4001a00070000006c696e65206f6e650a6c696e652074776f00"},
    {"ZC_WAIT_DIALOG", "b50007000000"},
    {"ZC_CLOSE_DIALOG", "b60007000000"},
    {"ZC_MENU_LIST", "b7001000070000005965733a4e6f3a00"},
    {"ZC_OPEN_EDITDLGSTR", "d40107000000"},
    {"ZC_NOTIFY_ACT", "c808414b4c00484b4c00090300000000000000000000faffffff0007000809000000"},
  };
  return v;
}

wire::Bytes rebuild(const std::string& n) {
  using wire::I;
  using wire::frame;
  using netproto::Pos3;
  using netproto::Pos6;
  if (n == "CZ_ENTER") return frame(n, {{"account_id", I(2000001)}, {"char_id", I(2000002)}, {"login_id1", I(0x12345678)}, {"client_tick", I(1234)}, {"sex", I(1)}});
  if (n == "CZ_NOTIFY_ACTORINIT") return frame(n, {});
  if (n == "CZ_REQUEST_MOVE") return frame(n, {{"pos", Pos3{150, 201, 3}}});
  if (n == "CZ_REQUEST_TIME") return frame(n, {{"client_tick", I(0xdeadbeef)}});
  if (n == "CZ_REQUEST_CHAT") return frame(n, {{"tail", wire::chat_tail("neo : hi")}});
  if (n == "CZ_CONTACTNPC") return frame(n, {{"AID", I(5000007)}, {"type", I(1)}});
  if (n == "CZ_REQ_NEXT_SCRIPT") return frame(n, {{"f2", I(5000007)}});
  if (n == "CZ_CHOOSE_MENU") return frame(n, {{"npc_id", I(5000007)}, {"select", I(255)}});
  if (n == "CZ_CLOSE_DIALOG") return frame(n, {{"GID", I(5000007)}});
  if (n == "CZ_INPUT_EDITDLG") return frame(n, {{"GID", I(5000007)}, {"value", I(-42)}});
  if (n == "CZ_INPUT_EDITDLGSTR") return frame(n, {{"GID", I(5000007)}, {"value", std::string("abc")}});
  if (n == "CZ_REQUEST_ACT2") return frame(n, {{"f2", I(5000008)}, {"f6", I(7)}});
  if (n == "CZ_USE_SKILL2") return frame(n, {{"f2", I(5)}, {"f4", I(28)}, {"f6", I(4242)}});
  if (n == "ZC_ACCEPT_ENTER") return frame(n, {{"startTime", I(99)}, {"posDir", Pos3{100, 101, 2}}, {"xSize", I(5)}, {"ySize", I(5)}, {"font", I(0)}});
  if (n == "ZC_REFUSE_ENTER") return frame(n, {{"errorCode", I(6)}});
  if (n == "ZC_NOTIFY_TIME") return frame(n, {{"time", I(0xcafef00d)}});
  if (n == "ZC_NOTIFY_PLAYERMOVE") return frame(n, {{"moveStartTime", I(5)}, {"moveData", Pos6{1, 2, 300, 400, 8, 8}}});
  if (n == "ZC_STOPMOVE") return frame(n, {{"AID", I(5000001)}, {"xPos", I(107)}, {"yPos", I(100)}});
  if (n == "ZC_NOTIFY_VANISH") return frame(n, {{"gid", I(5000001)}, {"type", I(3)}});
  wire::Actor a;
  a.gid = 5000001; a.object_type = 6; a.speed = 150; a.job = 45; a.level = 9; a.max_hp = 4000; a.hp = 3500; a.sex = 1; a.name = "bob";
  a.x = 12; a.y = 34; a.dir = 4; a.to_x = 56; a.to_y = 78; a.move_start = 4321;
  if (n == "LEGACY_IDLE_UNIT") return wire::actor_frame(a, false);
  if (n == "LEGACY_UNIT_WALKING") return wire::actor_frame(a, true);
  if (n == "ZC_NOTIFY_CHAT") return frame(n, {{"GID", I(5000001)}, {"Message", std::string("alice : hello")}});
  if (n == "OPAQUE_008E") return frame(n, {{"payload", wire::chat_tail("alice : hello")}});
  if (n == "ZC_SAY_DIALOG") return frame(n, {{"NpcID", I(7)}, {"message", std::string("line one\nline two")}});
  if (n == "ZC_WAIT_DIALOG") return frame(n, {{"NpcID", I(7)}});
  if (n == "ZC_CLOSE_DIALOG") return frame(n, {{"npcId", I(7)}});
  if (n == "ZC_MENU_LIST") return frame(n, {{"npcId", I(7)}, {"menu", std::string("Yes:No:")}});
  if (n == "ZC_OPEN_EDITDLGSTR") return frame(n, {{"npcId", I(7)}});
  if (n == "ZC_NOTIFY_ACT")
    return frame(n, {{"srcID", I(5000001)}, {"targetID", I(5000008)}, {"serverTick", I(777)}, {"srcSpeed", I(0)}, {"dmgSpeed", I(0)},
                     {"damage", I(-6)}, {"isSPDamage", I(0)}, {"div", I(7)}, {"type", I(8)}, {"damage2", I(9)}});
  throw CheckFailure("no builder for " + n);
}

}  // namespace

TEST("TL-SIM-050", every_opcode_and_length_the_sim_speaks_is_the_legacy_packet_dbs_and_the_catalogs_answer) {
  auto blocks = legacy_blocks();
  CHECK(blocks.size() > 900);
  CHECK(spoken().size() == 29 && wire::codecs()->packet_count() == 1186);
  for (const auto& d : spoken()) {
    // the legacy packet DB lists this opcode with this length (or a repeat group / variable length)
    bool found = false;
    for (auto& [op, text] : blocks) found |= op == d.opcode && block_has_len(text, d.len);
    if (!found) throw CheckFailure(std::string("no legacy DB entry matches ") + d.name);
    // and the catalog has it under this name, direction and length
    CHECK(wire::codecs()->opcode_of(d.name) == d.opcode);
    CHECK((wire::codecs()->direction_of(d.name) == netproto::Direction::ClientToServer) == d.c2s);
    const netproto::FrameTable table = d.c2s ? wire::codecs()->frames() : wire::codecs()->server_frames();
    const auto it = table.find(d.opcode);
    CHECK(it != table.end() && it->second.name == d.name);
    if (d.len < 0) CHECK(it->second.length.kind == netproto::LengthKind::Variable && it->second.length.prefix_offset == 2);
    else CHECK(it->second.length.kind == netproto::LengthKind::Fixed && it->second.length.fixed_size == d.len);
  }
}

TEST("TL-SIM-051", the_sims_frame_tables_are_netprotos_client_and_server_tables) {
  const auto& cs = *wire::codecs();
  const auto c2s = cs.frames();
  const auto s2c = cs.server_frames();
  CHECK(c2s.size() == 338 && s2c.size() > 800);   // 338 client packets; 357 typed + 479 length-only server frames
  for (const auto& [op, e] : c2s) {
    (void)op;
    CHECK(e.name.rfind("ZC_", 0) != 0 && e.name.rfind("HC_", 0) != 0 && e.name.rfind("AC_", 0) != 0 && e.name.rfind("OPAQUE_", 0) != 0);
  }
  for (const auto& d : spoken()) CHECK((d.c2s ? c2s : s2c).count(d.opcode) == 1);
  // the gates are the BR-MAP-059 policy of the catalog: connect packet pre-session, load-end ack pre-map-load, the rest in-world
  CHECK(c2s.at(0x0436).gate == netproto::Phase::PreSession && c2s.at(0x007d).gate == netproto::Phase::PreMapLoad &&
        c2s.at(0x035f).gate == netproto::Phase::InWorld);
}

TEST("TL-SIM-052", frames_built_through_netproto_round_trip_through_its_decoder_and_a_simclient) {
  const auto& cs = *wire::codecs();
  // every frame equals what the retired private codec produced
  CHECK(retired_codec_frames().size() == 29);
  for (const auto& [name, golden] : retired_codec_frames()) {
    const wire::Bytes mine = rebuild(name);
    if (hex(mine) != golden) throw CheckFailure(name + ": " + hex(mine) + " differs from the retired codec's " + golden);
  }
  // decode returns the values that went in (catalog names)
  auto dec = [&](const std::string& n, netproto::Direction d) { return wire::make_packet(cs.decode(rebuild(n), d), rebuild(n), 0); };
  auto e = dec("CZ_ENTER", netproto::Direction::ClientToServer);
  CHECK(e.num("account_id") == 2000001 && e.num("login_id1") == 0x12345678 && e.num("client_tick") == 1234 && e.num("sex") == 1);
  auto m = dec("CZ_REQUEST_MOVE", netproto::Direction::ClientToServer);
  CHECK(m.pos3("pos").x == 150 && m.pos3("pos").y == 201 && m.pos3("pos").dir == 3);  // pos3: netproto's packed 10-bit coordinates
  CHECK(dec("CZ_REQUEST_CHAT", netproto::Direction::ClientToServer).text("tail") == "neo : hi");
  auto sk = dec("CZ_USE_SKILL2", netproto::Direction::ClientToServer);
  CHECK(sk.num("f2") == 5 && sk.num("f4") == 28 && sk.num("f6") == 4242);
  CHECK(dec("CZ_INPUT_EDITDLGSTR", netproto::Direction::ClientToServer).text("value") == "abc");
  auto ae = dec("ZC_ACCEPT_ENTER", netproto::Direction::ServerToClient);
  CHECK(ae.pos3("posDir").x == 100 && ae.pos3("posDir").y == 101 && ae.pos3("posDir").dir == 2);
  auto pm = dec("ZC_NOTIFY_PLAYERMOVE", netproto::Direction::ServerToClient);
  CHECK(pm.pos6("moveData").x0 == 1 && pm.pos6("moveData").y1 == 400);
  auto se = dec("LEGACY_IDLE_UNIT", netproto::Direction::ServerToClient);
  CHECK(se.num("GID") == 5000001 && se.text("name") == "bob" && se.pos3("PosDir").x == 12 && se.pos3("PosDir").y == 34 && se.num("clevel") == 9);
  CHECK(wire::actor_frame(wire::Actor{}, false).size() == 108 && wire::actor_frame(wire::Actor{}, true).size() == 114);
  auto me = dec("LEGACY_UNIT_WALKING", netproto::Direction::ServerToClient);
  CHECK(me.pos6("MoveData").x0 == 12 && me.pos6("MoveData").x1 == 56 && me.pos6("MoveData").y1 == 78 && me.num("moveStartTime") == 4321);
  CHECK(dec("ZC_SAY_DIALOG", netproto::Direction::ServerToClient).text("message") == "line one\nline two");
  CHECK(dec("ZC_MENU_LIST", netproto::Direction::ServerToClient).text("menu") == "Yes:No:");
  auto act = dec("ZC_NOTIFY_ACT", netproto::Direction::ServerToClient);
  CHECK(act.num("damage") == -6 && act.num("div") == 7 && act.num("damage2") == 9);
  CHECK(dec("OPAQUE_008E", netproto::Direction::ServerToClient).text("payload") == "alice : hello");   // a length-only frame decodes to its payload

  // a stream with two frames and a partial third splits exactly, through netproto's framing and through a SimClient
  const auto s2c = cs.server_frames();
  wire::Bytes stream = rebuild("ZC_NOTIFY_TIME");
  auto f2 = rebuild("ZC_WAIT_DIALOG");
  stream.insert(stream.end(), f2.begin(), f2.end());
  auto f3 = rebuild("ZC_SAY_DIALOG");
  stream.insert(stream.end(), f3.begin(), f3.begin() + 5);
  auto sp = wire::split_frames(stream, s2c);
  CHECK(sp.frames.size() == 2 && sp.consumed == 6 + 6 && sp.error.empty());
  netproto::SimClient client(wire::codecs(), std::nullopt);
  CHECK(client.receive(stream).size() == 2 && client.buffered() == 5 && !client.closed());
  CHECK(client.receive(wire::Bytes(f3.begin() + 5, f3.end())).size() == 1 && client.buffered() == 0);
  CHECK(client.phase() == netproto::Phase::PreSession);   // no ZC_ACCEPT_ENTER yet
  CHECK(client.receive(rebuild("ZC_ACCEPT_ENTER")).size() == 1);
  CHECK(client.phase() == netproto::Phase::PreMapLoad);
  (void)client.send(netproto::DecodedPacket{"CZ_NOTIFY_ACTORINIT", {}, {}});
  CHECK(client.phase() == netproto::Phase::InWorld);
  // an unknown opcode and a short declared length are errors, not data
  wire::Bytes bad = {0xff, 0x0f, 0, 0};
  CHECK(!wire::split_frames(bad, s2c).error.empty());
  wire::Bytes shortlen = rebuild("ZC_SAY_DIALOG");
  shortlen[2] = 3; shortlen[3] = 0;
  CHECK(!wire::split_frames(shortlen, s2c).error.empty());
  netproto::SimClient closing(wire::codecs(), std::nullopt);
  CHECK(closing.receive(bad).empty() && closing.closed());
  // the refusal closes the client, like the server closing a refused connection
  netproto::SimClient refused(wire::codecs(), std::nullopt);
  CHECK(refused.receive(rebuild("ZC_REFUSE_ENTER")).size() == 1 && refused.closed());
}

TEST("TL-SIM-053", rpc_values_round_trip_and_malformed_input_is_rejected) {
  VMap m;
  m["n"] = 1.5;
  m["s"] = std::string("a\0b:c", 5);
  m["t"] = true;
  m["nil"] = Val();
  m["arr"] = Val(VArr{Val(1), Val("x"), Val(VArr{Val(false)})});
  Val v(std::move(m));
  Val back = decode_all(encode(v));
  CHECK(back.get("n")->num() == 1.5);
  CHECK(back.get("s")->str() == std::string("a\0b:c", 5));
  CHECK(back.get("t")->boolean() && back.get("nil")->is_nil());
  CHECK(back.get("arr")->arr().size() == 3 && back.get("arr")->arr()[2].arr()[0].boolean() == false);
  CHECK_THROWS(decode_all("s5:abc"));        // string overruns the input
  CHECK_THROWS(decode_all("a2:N"));          // missing element
  CHECK_THROWS(decode_all("q"));             // unknown tag
  CHECK_THROWS(decode_all("nxx;"));          // bad number
  CHECK_THROWS(decode_all("m1:n1;N"));       // map key is not a string
  CHECK_THROWS(decode_all("N" "N"));         // trailing bytes
  std::string deep;
  for (int i = 0; i < 100; ++i) deep += "a1:";
  deep += "N";
  CHECK_THROWS(decode_all(deep));            // nesting bound
  CHECK(to_json(Val(VMap{{"k", Val("v\n")}, {"n", Val(3)}})) == "{\"k\":\"v\\n\",\"n\":3}");
  // JSON is UTF-8: valid sequences pass through, broken ones become U+FFFD, truncation never cuts a character
  CHECK(to_json(Val("caf\xc3\xa9 \xe2\x86\x92")) == "\"caf\xc3\xa9 \xe2\x86\x92\"");
  CHECK(to_json(Val(std::string("cut \xe2\x86"))) == "\"cut \\ufffd\\ufffd\"");
  CHECK(to_json(Val(std::string("\xc0\xaf x"))) == "\"\\ufffd\\ufffd x\"");
  CHECK(utf8_truncate("ab\xe2\x86\x92" "cd", 3) == "ab" && utf8_truncate("ab\xe2\x86\x92" "cd", 5) == "ab\xe2\x86\x92" && utf8_truncate("abc", 9) == "abc");
}
