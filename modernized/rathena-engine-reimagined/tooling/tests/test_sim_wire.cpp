// The sim's wire vocabulary against netproto's data: every opcode and length the SimPlayer and the
// world/session boundary speak is checked against the legacy packet DB extract (netproto
// proto/legacy_20211103.yaml) and, for the nine spec packets, against the generated catalog.
// TL-SIM-05x are tooling-local ids (architecture section 8: "SimPlayer speaks codec-level packets").
#include <fstream>
#include <sstream>

#include "generated/catalog_client_packets.hpp"
#include "harness.hpp"
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

TEST("TL-SIM-050", every_opcode_and_length_the_sim_speaks_is_the_legacy_packet_dbs_answer) {
  auto blocks = legacy_blocks();
  CHECK(blocks.size() > 900);
  for (const auto& d : wire::defs()) {
    bool found = false;
    for (auto& [op, text] : blocks) {
      if (op != d.opcode) continue;
      if (!block_has_len(text, d.len)) continue;
      const std::string live = live_part(text);
      if (std::string(d.origin) == "legacy-struct") {
        // typed in the DB: the block carries the struct name; the sim's name is the DB's
        if (live.find(std::string("name: \"") + d.name + "\"") == std::string::npos) continue;
      } else if (std::string(d.origin) == "legacy-db") {
        // handler-parsed (client to server) or untyped (server to client): a handler or a header-only entry
        if (d.dir == wire::Dir::C2S && live.find("handler:") == std::string::npos) continue;
      }
      found = true;
    }
    if (!found) {
      std::ostringstream o;
      o << "no legacy DB entry matches " << d.name << " 0x" << std::hex << d.opcode << std::dec << " len " << d.len << " (" << d.origin << ")";
      throw CheckFailure(o.str());
    }
  }
}

TEST("TL-SIM-051", spec_packets_in_the_wire_table_equal_the_generated_netproto_catalog) {
  using namespace netproto;
  static const PacketCatalog cat = generated::client_packets_catalog();
  const ClientVersion v{20211103, Flavour::Re};
  int spec_packets = 0;
  for (const auto& rp : cat.resolve(v)) {
    const wire::Def* d = wire::find_name(rp.def->name);
    if (rp.def->name == "CA_LOGIN") { CHECK(d == nullptr); continue; }  // a login-server packet the sim never speaks
    CHECK(d != nullptr);
    ++spec_packets;
    CHECK(d->opcode == rp.version->opcode);
    CHECK(d->gate == rp.def->gate);
    CHECK(std::string(d->origin) == "spec");
    CHECK((d->dir == wire::Dir::C2S) == (rp.def->direction == Direction::ClientToServer));
    if (d->len < 0) CHECK(rp.version->length.kind == LengthKind::Variable);
    else CHECK(rp.version->length.kind == LengthKind::Fixed && rp.version->length.fixed_size == d->len);
  }
  CHECK(spec_packets == 8);
  // and nothing marked "spec" in the table is missing from the catalog
  int marked = 0;
  for (const auto& d : wire::defs()) marked += std::string(d.origin) == "spec";
  CHECK(marked == 8);
  // frame tables: both directions are complete and disjoint in opcodes
  CodecSet set = CodecSet::build(cat, v);
  auto c2s = wire::frame_table(wire::Dir::C2S, set);
  auto s2c = wire::frame_table(wire::Dir::S2C, set);
  for (const auto& [op, e] : c2s) CHECK(e.name.rfind("ZC_", 0) != 0);
  for (const auto& [op, e] : s2c) CHECK(e.name.rfind("ZC_", 0) == 0);
  CHECK(c2s.count(0x0436) && c2s.count(0x00b9) && s2c.count(0x00b4) && s2c.count(0x09ff) && s2c.count(0x0087));
}

TEST("TL-SIM-052", frames_built_by_the_sim_round_trip_through_its_decoder_and_netproto_framing) {
  using wire::Dir;
  netproto::CodecSet set = netproto::CodecSet::build(netproto::generated::client_packets_catalog(), {20211103, netproto::Flavour::Re});
  auto c2s = wire::frame_table(Dir::C2S, set);
  auto s2c = wire::frame_table(Dir::S2C, set);
  auto roundtrip = [&](const wire::Bytes& f, Dir d, const netproto::FrameTable& t, const char* name) {
    auto r = netproto::peek_frame(f, t);
    if (r.status != netproto::FrameStatus::kComplete || r.length != f.size()) throw CheckFailure(std::string("netproto framing rejects ") + name);
    auto p = wire::decode(f, d);
    if (!p || p->name != name) throw CheckFailure(std::string("decode lost ") + name);
    return *p;
  };
  auto e = roundtrip(wire::cz_enter(2000001, 2000001, 77, 1234, 1), Dir::C2S, c2s, "CZ_ENTER");
  CHECK(e.num("aid") == 2000001 && e.num("login_id1") == 77 && e.num("tick") == 1234 && e.num("sex") == 1);
  auto m = roundtrip(wire::cz_request_move(150, 201, 3), Dir::C2S, c2s, "CZ_REQUEST_MOVE");
  CHECK(m.num("x") == 150 && m.num("y") == 201 && m.num("dir") == 3);  // pos3: netproto's packed 10-bit coordinates
  auto chat = roundtrip(wire::cz_request_chat("neo : hi"), Dir::C2S, c2s, "CZ_REQUEST_CHAT");
  CHECK(chat.text("text") == "neo : hi");
  auto c = roundtrip(wire::cz_contactnpc(5000007, 1), Dir::C2S, c2s, "CZ_CONTACTNPC");
  CHECK(c.num("gid") == 5000007 && c.num("type") == 1);
  auto mn = roundtrip(wire::cz_choose_menu(9, 2), Dir::C2S, c2s, "CZ_CHOOSE_MENU");
  CHECK(mn.num("gid") == 9 && mn.num("choice") == 2);
  auto in = roundtrip(wire::cz_input_editdlgstr(9, "abc"), Dir::C2S, c2s, "CZ_INPUT_EDITDLGSTR");
  CHECK(in.num("gid") == 9 && in.text("text") == "abc");
  auto sk = roundtrip(wire::cz_use_skill(5, 5, 4242), Dir::C2S, c2s, "CZ_USE_SKILL");
  CHECK(sk.num("level") == 5 && sk.num("skill") == 5 && sk.num("gid") == 4242);

  auto ae = roundtrip(wire::zc_accept_enter(99, 100, 101, 2), Dir::S2C, s2c, "ZC_ACCEPT_ENTER");
  CHECK(ae.num("x") == 100 && ae.num("y") == 101 && ae.num("dir") == 2);
  auto pm = roundtrip(wire::zc_notify_playermove(5, 1, 2, 300, 400), Dir::S2C, s2c, "ZC_NOTIFY_PLAYERMOVE");
  CHECK(pm.num("x0") == 1 && pm.num("y1") == 400);
  wire::Actor a;
  a.gid = 5000001; a.name = "bob"; a.x = 12; a.y = 34; a.dir = 4; a.level = 9; a.to_x = 56; a.to_y = 78; a.move_start = 4321;
  auto se = roundtrip(wire::zc_notify_standentry(a), Dir::S2C, s2c, "ZC_NOTIFY_STANDENTRY11");
  CHECK(se.raw.size() == 108 && se.num("gid") == 5000001 && se.text("actor") == "bob" && se.num("x") == 12 && se.num("y") == 34 && se.num("level") == 9);
  auto me = roundtrip(wire::zc_notify_moveentry(a), Dir::S2C, s2c, "ZC_NOTIFY_MOVEENTRY11");
  CHECK(me.raw.size() == 114 && me.num("x") == 12 && me.num("to_x") == 56 && me.num("to_y") == 78 && me.num("move_start") == 4321);
  auto say = roundtrip(wire::zc_say_dialog(7, "line one\nline two"), Dir::S2C, s2c, "ZC_SAY_DIALOG");
  CHECK(say.num("gid") == 7 && say.text("text") == "line one\nline two");
  auto menu = roundtrip(wire::zc_menu_list(7, {"Yes", "No"}), Dir::S2C, s2c, "ZC_MENU_LIST");
  CHECK(menu.text("menu") == "Yes:No:" && menu.num("count") == 2);
  auto act = roundtrip(wire::zc_notify_act(1, 2, 3, 4, 5, -6, 7, 8, 9), Dir::S2C, s2c, "ZC_NOTIFY_ACT");
  CHECK(act.num("damage") == -6 && act.num("div") == 7 && act.num("damage2") == 9);

  // a stream with two frames and a partial third splits exactly
  wire::Bytes stream = wire::zc_notify_time(1);
  auto f2 = wire::zc_wait_dialog(3);
  stream.insert(stream.end(), f2.begin(), f2.end());
  auto f3 = wire::zc_say_dialog(3, "partial");
  stream.insert(stream.end(), f3.begin(), f3.begin() + 5);
  auto sp = wire::split_frames(stream, s2c);
  CHECK(sp.frames.size() == 2 && sp.consumed == 6 + 6 && sp.error.empty());
  // an unknown opcode and a short declared length are errors, not data
  wire::Bytes bad = {0xff, 0x0f, 0, 0};
  CHECK(!wire::split_frames(bad, s2c).error.empty());
  wire::Bytes shortlen = wire::zc_say_dialog(1, "x");
  wire::put16(shortlen, 2, 3);
  CHECK(!wire::split_frames(shortlen, s2c).error.empty());
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
