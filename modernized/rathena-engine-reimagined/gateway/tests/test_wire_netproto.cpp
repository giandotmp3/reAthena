// The gateway on netproto's real codec (no private interpreter any more): legacy-struct golden vectors, the
// findings netproto's review made (each judged by the legacy headers), strict encode, array_len, and the gate
// table end to end on the three listeners.
//
// Golden vectors: netproto/tests/golden/typed_re_20211103.txt was written by the compiled legacy structs
// (g++ on src/common/packets.hpp, PACKETVER 20211103); it is read here, read-only.
#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>

#include "fixture.hpp"
#include "gateway/packet_handlers.hpp"
#include "generated/catalog_packets_re_20211103.hpp"

using namespace gt;
using namespace gateway::wire;

namespace {

Bytes unhex(const std::string& h) {
  Bytes b;
  if (h == "-") return b;
  for (std::size_t i = 0; i + 1 < h.size(); i += 2) b.push_back(static_cast<std::uint8_t>(std::stoi(h.substr(i, 2), nullptr, 16)));
  return b;
}

struct Expect {
  int row = -1;
  std::string field, kind;
  std::vector<std::string> v;
};
struct Golden {
  std::string name;
  int rows = 0;
  Bytes bytes;
  std::vector<Expect> expect;
};

std::vector<Golden> load_golden() {
  std::ifstream f(std::string(GATEWAY_NETPROTO_DIR) + "/tests/golden/typed_re_20211103.txt");
  if (!f) throw CheckFailure("cannot read the netproto golden vectors");
  std::vector<Golden> out;
  std::string line;
  while (std::getline(f, line)) {
    if (line.empty() || line[0] == '#') continue;
    std::istringstream ls(line);
    std::string tag;
    ls >> tag;
    if (tag == "P") {
      Golden g;
      unsigned op, size;
      ls >> g.name >> op >> size >> g.rows;
      out.push_back(std::move(g));
    } else if (tag == "B") {
      std::string h;
      ls >> h;
      out.back().bytes = unhex(h);
    } else if (tag == "V" || tag == "R") {
      Expect e;
      if (tag == "R") ls >> e.row;
      ls >> e.field >> e.kind;
      for (std::string t; ls >> t;) e.v.push_back(t);
      out.back().expect.push_back(std::move(e));
    }
  }
  return out;
}

// Value written by the legacy struct against the decoded value. The seven members the gateway types `raw`
// (wire.cpp kRawFields) are compared as bytes: the oracle wrote text (+ NUL padding), the gateway keeps the bytes.
bool matches(const FieldValue& fv, const Expect& e, bool raw_member) {
  if (e.kind == "i") return std::get_if<std::int64_t>(&fv) && std::get<std::int64_t>(fv) == std::stoll(e.v.at(0));
  if (e.kind == "s") {
    const Bytes h = unhex(e.v.at(0));
    if (raw_member) {
      const auto* b = std::get_if<Bytes>(&fv);
      if (!b || b->size() < h.size() || !std::equal(h.begin(), h.end(), b->begin())) return false;
      return std::all_of(b->begin() + static_cast<std::ptrdiff_t>(h.size()), b->end(), [](std::uint8_t c) { return c == 0; });
    }
    return std::get_if<std::string>(&fv) && std::get<std::string>(fv) == std::string(h.begin(), h.end());
  }
  if (e.kind == "r") return std::get_if<Bytes>(&fv) && std::get<Bytes>(fv) == unhex(e.v.at(0));
  if (e.kind == "a") {
    const auto* a = std::get_if<std::vector<std::int64_t>>(&fv);
    if (!a || a->size() != std::stoul(e.v.at(0))) return false;
    for (std::size_t i = 0; i < a->size(); ++i) if ((*a)[i] != std::stoll(e.v.at(1 + i))) return false;
    return true;
  }
  return false;  // p3 / p6 / f: none of the gateway's packets
}

const std::set<std::pair<std::string, std::string>>& raw_members() {
  static const std::set<std::pair<std::string, std::string>> s = {
      {"CA_EXE_HASHCHECK", "hash"}, {"CH_SECOND_PASSWD_ACK", "pin"}, {"CH_MAKE_SECOND_PASSWD", "pin"}, {"CH_EDIT_SECOND_PASSWD", "old_pin"},
      {"CH_EDIT_SECOND_PASSWD", "new_pin"}, {"CH_DELETE_CHAR3", "birthdate"}, {"AC_ACK_HASH", "salt"}};
  return s;
}

// every typed packet of the login / char phases the gateway sends or receives
const std::vector<std::string>& typed_gateway_packets() {
  static const std::vector<std::string> v = {
      "CA_LOGIN", "CA_LOGIN2", "CA_LOGIN3", "CA_LOGIN4", "CA_LOGIN_PCBANG", "CA_LOGIN_CHANNEL", "CA_SSO_LOGIN_REQ", "CA_REQ_HASH",
      "CA_EXE_HASHCHECK", "CA_CONNECT_INFO_CHANGED", "CT_AUTH", "AC_REFUSE_LOGIN", "AC_ACK_HASH", "TC_RESULT", "AC_ACCEPT_LOGIN",
      "SC_NOTIFY_BAN", "PING", "CH_CHARLIST_REQ", "CH_SELECT_CHAR", "CH_MAKE_CHAR", "CH_DELETE_CHAR", "CH_DELETE_CHAR3_RESERVED",
      "CH_DELETE_CHAR3", "CH_DELETE_CHAR3_CANCEL", "CH_SECOND_PASSWD_ACK", "CH_MAKE_SECOND_PASSWD", "CH_EDIT_SECOND_PASSWD",
      "CH_AVAILABLE_SECOND_PASSWD", "CH_REQ_IS_VALID_CHARNAME", "CH_REQ_CHANGE_CHARNAME", "CH_REQ_CHANGE_CHARACTER_SLOT",
      "HC_REFUSE_ENTER", "HC_ACCEPT_ENTER", "HC_ACK_CHARINFO_PER_PAGE", "HC_ACCEPT_ENTER2", "HC_CHARLIST_NOTIFY", "HC_BLOCK_CHARACTER",
      "HC_ACCEPT_MAKECHAR", "HC_REFUSE_MAKECHAR", "HC_ACCEPT_DELETECHAR", "HC_REFUSE_DELETECHAR", "HC_DELETE_CHAR3_RESERVED",
      "HC_DELETE_CHAR3", "HC_DELETE_CHAR3_CANCEL", "HC_SECOND_PASSWD_LOGIN", "HC_NOTIFY_ZONESVR", "HC_ACK_IS_VALID_CHARNAME",
      "HC_ACK_CHANGE_CHARNAME", "HC_ACK_CHANGE_CHARACTER_SLOT"};
  return v;
}

struct Net {
  Env e;
  PacketCodec codec;
  ServerListEntry server;
  Net() { server.name = "fake-server"; server.port = 6121; }
  std::unique_ptr<LoginConnection> login_conn() {
    return std::make_unique<LoginConnection>(codec, e.login, e.cfg, "192.0.2.10", server, [this] { return e.dir.user_count(); });
  }
  std::unique_ptr<CharConnection> char_conn(std::uint64_t id = 1) {
    return std::make_unique<CharConnection>(codec, e.chr, e.cfg, e.rng, id, "192.0.2.10", server,
                                            [](const Character&) { return std::string("fake_map"); });
  }
  Bytes pkt(const std::string& name, const std::function<void(DecodedPacket&)>& fill = {}) {
    DecodedPacket p = make(name);
    if (fill) fill(p);
    return codec.encode(p);
  }
  // CA_LOGIN -> ticket ids, then CH_ENTER on a char connection: returns the connection in PreMapLoad
  std::unique_ptr<CharConnection> char_ready() {
    auto lc = login_conn();
    auto a = codec.decode_s2c(lc->feed(pkt("CA_LOGIN", [](DecodedPacket& p) { set_str(p, "username", "fakeuser"); set_str(p, "password", kFakePass); })).out.at(0));
    auto cc = char_conn();
    cc->feed(pkt("CH_ENTER", [&](DecodedPacket& p) {
      set_int(p, "account_id", get_int(a, "AID")); set_int(p, "login_id1", get_int(a, "login_id1"));
      set_int(p, "login_id2", get_int(a, "login_id2")); set_int(p, "sex", get_int(a, "sex")); }));
    return cc;
  }
};

// a minimal well-formed client frame for a frame-table entry: zero fields, the right length
Bytes minimal_frame(std::uint16_t opcode, const netproto::FrameEntry& e) {
  const auto& lr = e.length;
  Bytes b(lr.kind == netproto::LengthKind::Fixed ? lr.fixed_size : std::max<std::size_t>(lr.header_size, 4), 0);
  b[0] = static_cast<std::uint8_t>(opcode & 0xff);
  b[1] = static_cast<std::uint8_t>(opcode >> 8);
  if (lr.kind == netproto::LengthKind::Variable) {
    b[lr.prefix_offset] = static_cast<std::uint8_t>(b.size() & 0xff);
    b[lr.prefix_offset + 1] = static_cast<std::uint8_t>(b.size() >> 8);
  }
  return b;
}

}  // namespace

// ---- golden vectors from the compiled legacy structs --------------------------------------------------------------
TEST("CAP-24", legacy_struct_bytes_of_every_login_and_char_packet_decode_to_the_written_values_and_encode_back) {
  const PacketCodec codec;
  const auto gold = load_golden();
  std::set<std::string> wanted(typed_gateway_packets().begin(), typed_gateway_packets().end());
  std::size_t checked = 0, fields = 0, rows = 0;
  for (const Golden& g : gold) {
    if (!wanted.count(g.name)) continue;
    const auto dir = codec.set().direction_of(g.name);
    CHECK(dir.has_value());
    DecodedPacket p;
    try {
      p = *dir == netproto::Direction::ClientToServer ? codec.decode(g.bytes) : codec.decode_s2c(g.bytes);
    } catch (const WireError& e) {
      throw CheckFailure(g.name + ": " + e.what());
    }
    CHECK(p.name == g.name);
    CHECK(static_cast<int>(p.repeats.size()) == g.rows);
    for (const Expect& e : g.expect) {
      const auto& m = e.row < 0 ? p.fields : p.repeats.at(static_cast<std::size_t>(e.row));
      const auto it = m.find(e.field);
      if (it == m.end()) throw CheckFailure(g.name + ": no field " + e.field);
      if (!matches(it->second, e, raw_members().count({g.name, e.field}) != 0))
        throw CheckFailure(g.name + "." + e.field + " differs from the value the legacy struct held");
      ++fields;
    }
    if (codec.encode(p) != g.bytes) throw CheckFailure(g.name + ": encode(decode(legacy bytes)) differs from the legacy bytes");
    rows += static_cast<std::size_t>(g.rows);
    ++checked;
  }
  CHECK(checked == wanted.size() && fields > 150 && rows >= 4);   // every one of the 49 typed packets has a vector
}

TEST("BR-LOGIN-001", legacy_ca_login_bytes_log_in_and_the_legacy_reply_layout_comes_back) {
  Net n;
  Golden login;
  for (const Golden& g : load_golden()) if (g.name == "CA_LOGIN") login = g;
  CHECK(login.bytes.size() == 55);
  std::string user, pass;
  for (const Expect& e : login.expect) {
    const Bytes h = unhex(e.v.at(0));
    if (e.field == "username") user.assign(h.begin(), h.end());
    if (e.field == "password") pass.assign(h.begin(), h.end());
  }
  CHECK(!user.empty() && !pass.empty());
  auto& a = n.e.add_account(user);
  { Account u = a; u.stored_secret = n.e.hasher.hash(pass); n.e.accounts.update(u); }
  auto lc = n.login_conn();
  auto r = lc->feed(login.bytes);                      // the bytes the real client struct produces
  CHECK(!r.disconnect && r.out.size() == 1);
  CHECK(n.codec.decode_s2c(r.out[0]).name == "AC_ACCEPT_LOGIN");
  // the reply is a legacy PACKET_AC_ACCEPT_LOGIN: 0x0ac4, packetLength 64 + 160, one char-server row
  CHECK(r.out[0].size() == 224 && r.out[0][0] == 0xc4 && r.out[0][1] == 0x0a && (r.out[0][2] | r.out[0][3] << 8) == 224);
  // wrong password with the same bytes but another account secret: refused with code 1, not dropped
  n.e.dir.erase(a.id);
  { Account u = *n.e.accounts.find(a.id); u.stored_secret = n.e.hasher.hash("other-Pass-9"); n.e.accounts.update(u); }
  auto bad = n.codec.decode_s2c(n.login_conn()->feed(login.bytes).out.at(0));
  CHECK(bad.name == "AC_REFUSE_LOGIN" && get_int(bad, "error") == 1);
}

TEST("BR-CHAR-001", legacy_char_packet_bytes_reach_the_handlers_with_their_values) {
  Net n;
  n.e.add_account();
  auto cc = n.char_ready();
  for (const Golden& g : load_golden()) {
    if (g.name == "CH_SELECT_CHAR") {                  // slot 208: no such slot => HC_REFUSE_ENTER 0
      auto r = cc->feed(g.bytes);
      CHECK(!r.disconnect && n.codec.decode_s2c(r.out.at(0)).name == "HC_REFUSE_ENTER");
    }
    if (g.name == "CH_MAKE_CHAR") {                    // slot 60 is not available: HC_REFUSE_MAKECHAR, not a disconnect
      auto r = cc->feed(g.bytes);
      CHECK(!r.disconnect && n.codec.decode_s2c(r.out.at(0)).name == "HC_REFUSE_MAKECHAR");
    }
    if (g.name == "CH_REQ_CHANGE_CHARNAME" || g.name == "CH_DELETE_CHAR3_RESERVED" || g.name == "CH_DELETE_CHAR3_CANCEL") {
      auto r = cc->feed(g.bytes);                      // unknown character ids: an answer, never a malformed-packet drop
      CHECK(!r.disconnect && r.out.size() >= 1);
    }
  }
}

// ---- the findings netproto's review made, each judged by the legacy header ----------------------------------------------
TEST("CAP-24", world_connect_refusal_is_the_23_byte_0x006a_of_clif_parse_wanttoconnection_and_authfail_is_sc_notify_ban) {
  // src/map/clif.cpp:10685: WFIFOW(fd,0)=0x6a; WFIFOB(fd,2)=err; WFIFOSET(fd, packet_len(0x6a));  packet_len(0x6a) = 23
  // (clif_packetdb.hpp:16). ZC_REFUSE_ENTER is 0x0074 / 3 bytes and the legacy server never sends it; clif_authfail_fd
  // (clif.cpp:829) sends PACKET_SC_NOTIFY_BAN, 0x0081 / 3 bytes.
  Net n;
  n.e.add_account();
  CHECK(n.codec.resolved("ZC_REFUSE_ENTER")->version->opcode == 0x0074 && n.codec.resolved("ZC_REFUSE_ENTER")->version->length.fixed_size == 3);
  CHECK(n.codec.resolved("SC_NOTIFY_BAN")->version->opcode == 0x0081 && n.codec.resolved("SC_NOTIFY_BAN")->version->length.fixed_size == 3);
  CHECK(n.codec.resolved("AC_REFUSE_LOGIN")->version->opcode == 0x083e && n.codec.resolved("AC_REFUSE_LOGIN")->version->length.fixed_size == 26);
  CHECK(n.codec.resolved(wire::kConnectRefusal)->version->opcode == 0x006a && n.codec.resolved(wire::kConnectRefusal)->version->length.fixed_size == 23);
  for (int bad_sex : {2, 7}) {
    WorldConnection w(n.codec, n.e.chr);
    auto r = w.feed(n.pkt("CZ_ENTER", [&](DecodedPacket& p) { set_int(p, "account_id", 2000001); set_int(p, "char_id", 1); set_int(p, "sex", bad_sex); }));
    CHECK(r.disconnect && r.out.size() == 1 && r.out[0].size() == 23 && r.out[0][0] == 0x6a && r.out[0][1] == 0 && r.out[0][2] == 6);
  }
  WorldConnection w2(n.codec, n.e.chr);
  auto r2 = w2.feed(n.pkt("CZ_ENTER", [&](DecodedPacket& p) { set_int(p, "account_id", 2000001); set_int(p, "char_id", 1); set_int(p, "login_id1", 5); set_int(p, "sex", 1); }));
  CHECK(r2.disconnect && r2.out.size() == 1 && r2.out[0].size() == 3 && r2.out[0][0] == 0x81 && r2.out[0][1] == 0 && r2.out[0][2] == 0);   // authfail(0)
}

TEST("CAP-24", character_info_level_and_bischangedcharname_are_signed_int16) {
  Net n;
  for (const char* name : {"HC_ACCEPT_ENTER", "HC_ACK_CHARINFO_PER_PAGE", "HC_ACCEPT_MAKECHAR"}) {
    const std::string prefix = std::string(name) == "HC_ACCEPT_MAKECHAR" ? "character." : "";
    int seen = 0;
    for (const auto& f : n.codec.fields_of(name))
      if (f.name == prefix + "level" || f.name == prefix + "bIsChangedCharName") {
        CHECK(f.type == netproto::FieldType::I16);
        ++seen;
      }
    CHECK(seen == 2);
  }
  DecodedPacket p = make("HC_ACK_CHARINFO_PER_PAGE");
  p.repeats.push_back({{"level", std::int64_t{-1}}, {"bIsChangedCharName", std::int64_t{-32768}}});
  p.repeats.push_back({{"level", std::int64_t{999}}, {"bIsChangedCharName", std::int64_t{1}}});
  const Bytes f = n.codec.encode(p);
  CHECK(f.size() == 4 + 2 * 175 && f[4 + 92] == 0xff && f[4 + 93] == 0xff && f[4 + 140] == 0x00 && f[4 + 141] == 0x80);
  const DecodedPacket q = n.codec.decode_s2c(f);
  CHECK(std::get<std::int64_t>(q.repeats[0].at("level")) == -1 && std::get<std::int64_t>(q.repeats[0].at("bIsChangedCharName")) == -32768);
  CHECK(std::get<std::int64_t>(q.repeats[1].at("level")) == 999);
}

TEST("CAP-24", sso_token_is_a_string_tail_and_the_md5_salt_is_the_legacy_unterminated_tail) {
  Net n;
  const auto* sso = n.codec.resolved("CA_SSO_LOGIN_REQ");
  const auto tok = std::find_if(sso->version->fields.begin(), sso->version->fields.end(), [](const auto& f) { return f.name == "token"; });
  CHECK(tok != sso->version->fields.end() && tok->type == netproto::FieldType::FixedString && tok->width == 0 && tok->offset == 92);
  // decode cuts at the NUL; text after it is not part of the token
  Bytes f = n.pkt("CA_SSO_LOGIN_REQ", [](DecodedPacket& p) { set_str(p, "token", "abcdef"); });
  f.push_back('z'); f[2] = static_cast<std::uint8_t>(f.size() & 0xff);
  CHECK(get_str(n.codec.decode(f), "token") == "abcdef");
  // loginclif.cpp:363-367: packetLength = sizeof(*p) + md5keylen, strncpy(salt, md5key, md5keylen): no terminator
  auto h = make("AC_ACK_HASH");
  set_bytes(h, "salt", Bytes{'a', 'b', 'c'});
  const Bytes hb = n.codec.encode(h);
  CHECK((hb == Bytes{0xdc, 0x01, 7, 0, 'a', 'b', 'c'}));
  CHECK(get_bytes(n.codec.decode_s2c(hb), "salt") == (Bytes{'a', 'b', 'c'}));
}

TEST("CAP-24", binary_and_unterminated_char_arrays_keep_every_byte_including_nul_and_full_width) {
  Net n;
  n.e.add_account();
  // CA_EXE_HASHCHECK.hash is an MD5 digest: a NUL byte in it must not cut it short (a `string` field would)
  Bytes digest;
  for (int i = 0; i < 16; ++i) digest.push_back(static_cast<std::uint8_t>(i % 3 == 0 ? 0 : 0xA0 + i));
  Bytes f = n.pkt("CA_EXE_HASHCHECK", [&](DecodedPacket& p) { set_bytes(p, "hash", digest); });
  CHECK(f.size() == 18 && std::equal(digest.begin(), digest.end(), f.begin() + 2));
  CHECK(get_bytes(n.codec.decode(f), "hash") == digest);
  n.e.cfg.client_hash_check = true;
  n.e.cfg.client_hash_nodes = {{0, hex_of(digest)}};
  auto lc = n.login_conn();
  lc->feed(f);
  auto acc = n.codec.decode_s2c(lc->feed(n.pkt("CA_LOGIN", [](DecodedPacket& p) { set_str(p, "username", "fakeuser"); set_str(p, "password", kFakePass); })).out.at(0));
  CHECK(acc.name == "AC_ACCEPT_LOGIN");
  // a PIN is exactly four bytes and a birthdate exactly six: no room for a NUL, so a string encode could never write them
  auto pin = make("CH_SECOND_PASSWD_ACK");
  set_bytes(pin, "pin", Bytes{'1', '2', '3', '4'});
  const Bytes pb = n.codec.encode(pin);
  CHECK(pb.size() == 10 && pb[6] == '1' && pb[9] == '4');
  auto bd = make("CH_DELETE_CHAR3");
  set_bytes(bd, "birthdate", Bytes{'9', '0', '0', '1', '0', '1'});
  const Bytes bb = n.codec.encode(bd);
  CHECK(bb.size() == 12 && bb[6] == '9' && bb[11] == '1');
  // too many bytes is still refused
  set_bytes(bd, "birthdate", Bytes(7, '1'));
  bool threw = false;
  try { (void)n.codec.encode(bd); } catch (const WireError&) { threw = true; }
  CHECK(threw);
}

TEST("CAP-24", the_gateway_catalog_is_netprotos_except_exactly_the_seven_raw_members) {
  const netproto::PacketCatalog real = netproto::generated::packets_re_20211103_catalog();
  const netproto::PacketCatalog mine = PacketCodec::gateway_catalog();
  CHECK(real.packets().size() == 1186 && mine.packets().size() == 1186);
  std::size_t different = 0;
  for (std::size_t i = 0; i < real.packets().size(); ++i) {
    const auto& a = real.packets()[i];
    const auto& b = mine.packets()[i];
    CHECK(a.name == b.name && a.direction == b.direction && a.gate == b.gate && a.versions.size() == b.versions.size());
    for (std::size_t v = 0; v < a.versions.size(); ++v) {
      const auto& x = a.versions[v];
      const auto& y = b.versions[v];
      CHECK(x.opcode == y.opcode && x.length.kind == y.length.kind && x.length.fixed_size == y.length.fixed_size &&
            x.length.header_size == y.length.header_size && x.fields.size() == y.fields.size());
      for (std::size_t k = 0; k < x.fields.size(); ++k) {
        CHECK(x.fields[k].name == y.fields[k].name && x.fields[k].offset == y.fields[k].offset && x.fields[k].array_len == y.fields[k].array_len);
        if (x.fields[k].type != y.fields[k].type) {
          CHECK(x.fields[k].type == netproto::FieldType::FixedString && y.fields[k].type == netproto::FieldType::Raw);
          CHECK(x.fields[k].width == y.fields[k].width);
          CHECK(raw_members().count({a.name, x.fields[k].name}) == 1);
          ++different;
        }
      }
    }
  }
  CHECK(different == 7);
}

TEST("CAP-24", field_names_are_the_legacy_struct_member_names) {
  Net n;
  auto has = [&](const char* packet, const std::string& field) {
    for (const auto& f : n.codec.fields_of(packet)) if (f.name == field) return true;
    return false;
  };
  CHECK(has("CZ_ENTER", "login_id1") && !has("CZ_ENTER", "auth_code"));              // clif_parse_WantToConnection
  CHECK(has("SC_NOTIFY_BAN", "errorCode") && !has("SC_NOTIFY_BAN", "result"));       // PACKET_SC_NOTIFY_BAN::errorCode
  CHECK(has("ZC_REFUSE_ENTER", "errorCode") && !has("ZC_REFUSE_ENTER", "error"));
  CHECK(has("AC_ACCEPT_LOGIN", "new_") && !has("AC_ACCEPT_LOGIN", "new"));           // `new` is a C++ keyword: the legacy member is new_
  CHECK(has("HC_ACCEPT_MAKECHAR", "character.name") && !has("HC_ACCEPT_MAKECHAR", "name"));   // the embedded CHARACTER_INFO
  CHECK(!has("CH_ENTER", "unknown"));                                                // the char server reads nothing at offset 14
}

// ---- strict encode, array_len, empty tails (also run under ASan/UBSan) --------------------------------------------------------
TEST("CAP-24", encoding_empty_raw_and_string_tails_is_defined_behaviour_under_ubsan) {
  // memcpy(dst, nullptr, 0) is undefined; the private codec did exactly that for an empty raw tail (wire.cpp:401)
  Net n;
  auto h = make("AC_ACK_HASH");
  set_bytes(h, "salt", Bytes{});                                      // an explicit empty raw tail
  const Bytes a = n.codec.encode(h);
  CHECK((a == Bytes{0xdc, 0x01, 4, 0}));
  CHECK(n.codec.encode(make("AC_ACK_HASH")) == a);                    // and a missing one
  CHECK(get_bytes(n.codec.decode_s2c(a), "salt").empty());
  auto chat = make("CZ_REQUEST_CHAT");                                // CZ_REQUEST_CHAT.tail is a catalog raw tail
  set_bytes(chat, "tail", Bytes{});
  CHECK((n.codec.encode(chat) == Bytes{0xf3, 0x00, 4, 0}));
  CHECK(get_bytes(n.codec.decode(n.codec.encode(chat)), "tail").empty());
  auto sso = make("CA_SSO_LOGIN_REQ");                                // an empty string tail is just its NUL
  set_str(sso, "token", "");
  const Bytes s = n.codec.encode(sso);
  CHECK(s.size() == 93 && s.back() == 0 && get_str(n.codec.decode(s), "token").empty());
  auto exe = make("CA_EXE_HASHCHECK");                                // an empty fixed raw field is zeros
  set_bytes(exe, "hash", Bytes{});
  CHECK(n.codec.encode(exe) == (Bytes{0x04, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}));
  auto ok = make("OPAQUE_006A");                                      // an empty payload of a fixed frame is refused, not read as zeros
  bool threw = false;
  try { (void)n.codec.encode(ok); } catch (const WireError&) { threw = true; }
  CHECK(threw);
}

TEST("CAP-24", array_len_is_honoured_bytes_and_positions_round_trip) {
  Net n;
  // AC_ACCEPT_LOGIN rows end in `uint8 unknown[128]`: the private interpreter ignored array_len
  auto acc = make("AC_ACCEPT_LOGIN");
  Bytes blob(128);
  for (std::size_t i = 0; i < blob.size(); ++i) blob[i] = static_cast<std::uint8_t>(i * 7 + 1);
  std::map<std::string, FieldValue> row;
  row["port"] = std::int64_t{6900};
  row["unknown"] = blob;
  acc.repeats.push_back(row);
  const Bytes f = n.codec.encode(acc);
  CHECK(f.size() == 64 + 160 && std::equal(blob.begin(), blob.end(), f.begin() + 64 + 32));
  CHECK(std::get<Bytes>(n.codec.decode_s2c(f).repeats[0].at("unknown")) == blob);
  acc.repeats[0]["unknown"] = Bytes(129, 1);                           // more elements than the array holds: refused
  bool threw = false;
  try { (void)n.codec.encode(acc); } catch (const WireError&) { threw = true; }
  CHECK(threw);
  // CT_AUTH: u8[66]
  auto ct = make("CT_AUTH");
  set_bytes(ct, "unknown", Bytes(66, 0x5a));
  CHECK(n.codec.encode(ct).size() == 68 && n.codec.decode(n.codec.encode(ct)).fields.at("unknown") == FieldValue(Bytes(66, 0x5a)));
  // positions are packed (WBUFPOS2), not six loose bytes
  auto mv = make("ZC_NOTIFY_PLAYERMOVE");
  mv.fields["moveStartTime"] = std::int64_t{1234};
  mv.fields["moveData"] = netproto::Pos6{100, 200, 300, 400, 8, 8};
  const Bytes mb = n.codec.encode(mv);
  CHECK(mb.size() == 12 && n.codec.decode_s2c(mb).fields.at("moveData") == FieldValue(netproto::Pos6{100, 200, 300, 400, 8, 8}));
}

TEST("CAP-24", encode_refuses_what_does_not_fit_and_never_reads_or_writes_out_of_bounds) {
  Net n;
  auto big = [&](const std::string& pkt, const std::string& field, FieldValue v) {
    DecodedPacket p = make(pkt);
    p.fields[field] = std::move(v);
    bool threw = false;
    try { (void)n.codec.encode(p); } catch (const WireError&) { threw = true; }
    return threw;
  };
  CHECK(big("CH_SELECT_CHAR", "slot", std::int64_t{256}));          // u8 overflow
  CHECK(big("CH_SELECT_CHAR", "slot", std::int64_t{-129}));
  CHECK(big("CA_LOGIN", "version", std::string("x")));              // wrong value kind
  CHECK(big("CA_LOGIN", "username", Bytes{1}));
  CHECK(big("CH_ENTER", "nope", std::int64_t{1}));                  // typo in a field name
  CHECK(big("HC_NOTIFY_ZONESVR", "mapname", std::string(16, 'm')));  // char[16] holds 15 + NUL
  CHECK(!big("HC_NOTIFY_ZONESVR", "mapname", std::string(15, 'm')));
  // a repeat group in a packet without one, and a frame over the 32,768-byte limit
  DecodedPacket p = make("HC_REFUSE_ENTER");
  p.repeats.push_back({});
  bool threw = false;
  try { (void)n.codec.encode(p); } catch (const WireError&) { threw = true; }
  CHECK(threw);
  DecodedPacket many = make("HC_BLOCK_CHARACTER");
  many.repeats.assign(32768 / 24 + 1, {});
  threw = false;
  try { (void)n.codec.encode(many); } catch (const WireError&) { threw = true; }
  CHECK(threw);
}

TEST("CAP-24", handler_owned_display_text_is_cut_by_the_handler_not_the_codec) {
  Net n;
  n.e.add_account();
  n.server.name = std::string(40, 'S');                              // operator text longer than char name[20]
  auto lc = n.login_conn();
  auto r = lc->feed(n.pkt("CA_LOGIN", [](DecodedPacket& p) { set_str(p, "username", "fakeuser"); set_str(p, "password", kFakePass); }));
  CHECK(!r.disconnect && r.out.size() == 1);
  CHECK(std::get<std::string>(n.codec.decode_s2c(r.out[0]).repeats.at(0).at("name")) == std::string(19, 'S'));
  CHECK(wire::fit("abc", 4) == "abc" && wire::fit("abcd", 4) == "abc" && wire::fit("abcdef", 1).empty() && wire::fit("x", 0).empty());
  // the codec itself refuses the same text
  auto direct = make("AC_ACCEPT_LOGIN");
  direct.repeats.push_back({{"name", std::string(20, 'S')}});
  bool threw = false;
  try { (void)n.codec.encode(direct); } catch (const WireError&) { threw = true; }
  CHECK(threw);
  // character names and map names on the char list are cut the same way
  Net m;
  auto& a = m.e.add_account();
  m.e.add_char(a.id, "Bobby", 0);
  CharConnection cc(m.codec, m.e.chr, m.e.cfg, m.e.rng, 1, "192.0.2.10", m.server, [](const Character&) { return std::string(30, 'm'); });
  auto acc = m.codec.decode_s2c(m.login_conn()->feed(m.pkt("CA_LOGIN", [](DecodedPacket& p) { set_str(p, "username", "fakeuser"); set_str(p, "password", kFakePass); })).out.at(0));
  auto er = cc.feed(m.pkt("CH_ENTER", [&](DecodedPacket& p) {
    set_int(p, "account_id", get_int(acc, "AID")); set_int(p, "login_id1", get_int(acc, "login_id1"));
    set_int(p, "login_id2", get_int(acc, "login_id2")); set_int(p, "sex", get_int(acc, "sex")); }));
  CHECK(!er.disconnect && m.codec.decode_s2c(er.out.at(2)).name == "HC_ACCEPT_ENTER");
  CHECK(std::get<std::string>(m.codec.decode_s2c(er.out.at(2)).repeats.at(0).at("mapName")) == std::string(15, 'm'));
}

TEST("CAP-24", a_client_version_the_catalog_does_not_cover_is_refused_with_no_fallback) {
  bool threw = false;
  try { PacketCodec c(netproto::ClientVersion{20150000, netproto::Flavour::Re}); } catch (const WireError&) { threw = true; }
  CHECK(threw);
  threw = false;
  try { PacketCodec c(netproto::ClientVersion{20211103, netproto::Flavour::Main}); } catch (const WireError&) { threw = true; }
  CHECK(threw);
  const PacketCodec a, b;
  CHECK(&a.set() == &b.set());                                        // one immutable codec set per version for the process
}

// ---- the gate table end to end -----------------------------------------------------------------------------------------------
TEST("BR-MAP-059", every_client_opcode_is_gated_by_the_catalog_on_all_three_listeners_in_the_pre_session_phase) {
  Net n;
  n.e.add_account();
  std::size_t mismatched = 0, pre_session = 0;
  for (const auto& [opcode, entry] : n.codec.set().frames()) {
    const Bytes frame = minimal_frame(opcode, entry);
    CHECK(n.codec.peek(frame).status == netproto::FrameStatus::kComplete);
    if (entry.gate != netproto::Phase::PreSession) {
      ++mismatched;
      auto lc = n.login_conn();
      auto r1 = lc->feed(frame);
      CHECK(r1.disconnect && r1.note == "packet not allowed in this phase" && r1.out.empty());
      auto cc = n.char_conn();
      auto r2 = cc->feed(frame);
      CHECK(r2.disconnect && r2.note == "packet not allowed in this phase" && r2.out.empty());
      WorldConnection w(n.codec, n.e.chr);
      auto r3 = w.feed(frame);
      CHECK(r3.disconnect && r3.note == "packet not allowed in this phase" && r3.out.empty() && !r3.world_entered);
    } else {
      ++pre_session;
      if (entry.name != "CZ_ENTER") {                                  // the one-packet rule: nothing but the connect packet before the session
        WorldConnection w(n.codec, n.e.chr);
        auto r = w.feed(frame);
        CHECK(r.disconnect && r.note == "unexpected packet before world connect");
      }
    }
  }
  CHECK(mismatched == 325 && pre_session == 13 && n.codec.set().frames().size() == 338);   // every client packet of the 1,186-packet catalog drives the decision
}

TEST("BR-MAP-059", unknown_opcodes_and_server_packets_from_a_client_disconnect_on_every_listener) {
  Net n;
  n.e.add_account();
  std::uint16_t unknown = 0x0fff;
  while (n.codec.set().frames().count(unknown)) --unknown;
  const Bytes junk = {static_cast<std::uint8_t>(unknown & 0xff), static_cast<std::uint8_t>(unknown >> 8), 0, 0};
  const Bytes server_packet = n.pkt("ZC_NOTIFY_TIME");                 // a real packet, but the other direction
  const Bytes length_only = {0x6a, 0x00, 1};                           // 0x006a: length-only (direction any), never a client packet
  for (const Bytes& b : {junk, server_packet, length_only}) {
    auto lc = n.login_conn();
    auto r1 = lc->feed(b);
    CHECK(r1.disconnect && r1.note == "unknown opcode" && r1.out.empty() && lc->closed());
    auto cc = n.char_conn();
    auto r2 = cc->feed(b);
    CHECK(r2.disconnect && r2.note == "unknown opcode");
    WorldConnection w(n.codec, n.e.chr);
    auto r3 = w.feed(b);
    CHECK(r3.disconnect && r3.note == "unknown opcode");
    CHECK(w.feed(n.pkt("CZ_ENTER")).out.empty());                      // a closed connection says nothing, whatever follows
  }
}

TEST("BR-MAP-059", the_char_listener_walks_pre_session_to_pre_map_load_and_gates_each_step) {
  Net n;
  n.e.add_account();
  {  // the first packet must be the connect packet
    auto cc = n.char_conn();
    auto r = cc->feed(n.pkt("CH_CHARLIST_REQ"));
    CHECK(r.disconnect && r.note == "packet not allowed in this phase");
  }
  {  // a pre-session login packet on the char listener passes the gate but not the PIN/auth rule
    auto cc = n.char_conn();
    auto r = cc->feed(n.pkt("CA_LOGIN"));
    CHECK(r.disconnect && r.note == "packet not allowed before PIN / auth");
  }
  auto cc = n.char_ready();                                            // CH_ENTER accepted: PreMapLoad
  CHECK(cc->session().phase == rules::SessionPhase::Ready);
  CHECK(!cc->feed(n.pkt("CH_CHARLIST_REQ")).disconnect);               // a pre-map-load packet now passes
  {
    auto again = n.char_ready();                                       // CH_ENTER a second time is the wrong phase
    auto r = again->feed(n.pkt("CH_ENTER"));
    CHECK(r.disconnect && r.note == "packet not allowed in this phase");
  }
  {
    auto world = n.char_ready();                                       // and an in-world packet never passes here
    auto r = world->feed(n.pkt("CZ_REQUEST_MOVE"));
    CHECK(r.disconnect && r.note == "packet not allowed in this phase");
  }
}

TEST("BR-MAP-059", the_world_listener_walks_pre_session_pre_map_load_in_world_and_gates_each_step) {
  Net n;
  auto& a = n.e.add_account();
  CharId c = n.e.add_char(a.id, "Bobby", 0);
  auto connect_ok = [&]() {
    n.e.dir.erase(a.id);                                                // one online session per account
    auto lc = n.login_conn();
    auto acc = n.codec.decode_s2c(lc->feed(n.pkt("CA_LOGIN", [](DecodedPacket& p) { set_str(p, "username", "fakeuser"); set_str(p, "password", kFakePass); })).out.at(0));
    auto cc = n.char_conn();
    cc->feed(n.pkt("CH_ENTER", [&](DecodedPacket& p) {
      set_int(p, "account_id", get_int(acc, "AID")); set_int(p, "login_id1", get_int(acc, "login_id1"));
      set_int(p, "login_id2", get_int(acc, "login_id2")); set_int(p, "sex", get_int(acc, "sex")); }));
    cc->feed(n.pkt("CH_SELECT_CHAR", [](DecodedPacket& p) { set_int(p, "slot", 0); }));
    return get_int(acc, "login_id1");
  };
  auto enter = [&](std::int64_t id1) {
    return n.pkt("CZ_ENTER", [&](DecodedPacket& p) { set_int(p, "account_id", a.id); set_int(p, "char_id", c); set_int(p, "login_id1", id1); set_int(p, "client_tick", 9); set_int(p, "sex", 1); });
  };
  {  // an in-world packet, and the load-end ack, before the session exists
    WorldConnection w(n.codec, n.e.chr);
    CHECK(w.feed(n.pkt("CZ_REQUEST_MOVE")).disconnect);
    WorldConnection w2(n.codec, n.e.chr);
    CHECK(w2.feed(n.pkt("CZ_NOTIFY_ACTORINIT")).disconnect);
    // the first packet decides: a bad one in front of a good connect packet ends the connection, the connect packet is never read
    WorldConnection w3(n.codec, n.e.chr);
    Bytes two = n.pkt("CZ_REQUEST_MOVE");
    const Bytes ok = enter(1);
    two.insert(two.end(), ok.begin(), ok.end());
    auto r = w3.feed(two);
    CHECK(r.disconnect && !r.world_entered && r.out.empty());
  }
  const std::int64_t id1 = connect_ok();
  WorldConnection w(n.codec, n.e.chr);
  auto r = w.feed(enter(id1));
  CHECK(!r.disconnect && r.world_entered && !r.map_loaded);            // PreSession -> PreMapLoad
  CHECK(w.feed(n.pkt("CZ_REQUEST_MOVE")).disconnect);                  // still loading the map: in-world packets are out of phase
  // a second world connection through the whole walk
  const std::int64_t id2 = connect_ok();
  WorldConnection w2(n.codec, n.e.chr);
  CHECK(w2.feed(enter(id2)).world_entered);
  auto loaded = w2.feed(n.pkt("CZ_NOTIFY_ACTORINIT"));
  CHECK(!loaded.disconnect && loaded.map_loaded);                      // PreMapLoad -> InWorld
  CHECK(!w2.feed(n.pkt("CZ_REQUEST_MOVE", [](DecodedPacket& p) { p.fields["pos"] = netproto::Pos3{10, 20, 3}; })).disconnect);   // now in-world packets pass the gate
  CHECK(!w2.feed(n.pkt("CZ_REQUEST_TIME")).disconnect);
  CHECK(w2.feed(n.pkt("CZ_NOTIFY_ACTORINIT")).disconnect);             // the load-end ack cannot be replayed in-world
  WorldConnection w3(n.codec, n.e.chr);
  w3.feed(enter(connect_ok()));
  w3.feed(n.pkt("CZ_NOTIFY_ACTORINIT"));
  CHECK(w3.feed(enter(1)).disconnect);                                 // nor the connect packet
}

TEST("BR-MAP-059", the_login_listener_is_pre_session_only_and_its_budget_still_delays_not_drops) {
  Net n;
  n.e.add_account();
  auto lc = n.login_conn();
  Bytes req = n.pkt("CA_REQ_HASH");
  Bytes six;
  for (int i = 0; i < 6; ++i) six.insert(six.end(), req.begin(), req.end());
  auto r1 = lc->feed(six);
  CHECK(r1.out.size() == 3 && lc->has_pending() && !r1.disconnect);   // three frames per cycle
  auto r2 = lc->pump();
  CHECK(r2.out.size() == 3 && !lc->has_pending());
  // a char-phase packet queued behind login packets is judged when its turn comes, not before
  Bytes mixed = req;
  const Bytes sel = n.pkt("CH_SELECT_CHAR");
  mixed.insert(mixed.end(), sel.begin(), sel.end());
  auto r3 = n.login_conn()->feed(mixed);
  CHECK(r3.disconnect && r3.out.size() == 1 && r3.note == "packet not allowed in this phase");
}
