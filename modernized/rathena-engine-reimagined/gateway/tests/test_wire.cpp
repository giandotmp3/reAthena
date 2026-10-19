// Packet level: netproto's real catalog, framing and gating driving the gateway's login, char-select
// and world-connect handlers; replies are decoded with the same codec.
#include <algorithm>
#include <cstring>
#include <fstream>
#include <functional>
#include <sstream>

#include "fixture.hpp"
#include "gateway/crypto.hpp"
#include "gateway/hasher.hpp"
#include "gateway/packet_handlers.hpp"

using namespace gt;
using namespace gateway::wire;

namespace {

Bytes u32le(std::uint32_t v) { return {std::uint8_t(v), std::uint8_t(v >> 8), std::uint8_t(v >> 16), std::uint8_t(v >> 24)}; }
std::int64_t I(const DecodedPacket& p, const char* k) { return get_int(p, k); }

// First "len:" after "opcode: 0xNNNN" in the generated legacy table of the pinned netproto unit.
int legacy_len(std::uint16_t opcode) {
  std::ifstream f(std::string(GATEWAY_NETPROTO_DIR) + "/proto/legacy_20211103.yaml");
  std::string line;
  char want[40];
  std::snprintf(want, sizeof want, "  - opcode: 0x%04x", opcode);
  while (std::getline(f, line)) {
    if (line.rfind(want, 0) != 0) continue;
    std::getline(f, line);
    return std::atoi(line.c_str() + line.find("len:") + 4);
  }
  return -999;
}

struct Net {
  Env e;
  PacketCodec codec;
  ServerListEntry server;
  CharId c0 = 0;
  Net() { server.name = "fake-server"; server.port = 6121; server.ip[0] = 192; server.ip[1] = 0; server.ip[2] = 2; server.ip[3] = 1; }
  Bytes pkt(const std::string& name, const std::function<void(DecodedPacket&)>& fill = {}) {
    DecodedPacket p = make(name);
    if (fill) fill(p);
    return codec.encode(p);
  }
  Bytes login_pkt(const std::string& user, const std::string& pass) {
    return pkt("CA_LOGIN", [&](DecodedPacket& p) { set_int(p, "version", 20211103); set_str(p, "username", user); set_str(p, "password", pass); });
  }
  std::unique_ptr<LoginConnection> login_conn(const std::string& ip = "192.0.2.10") {
    return std::make_unique<LoginConnection>(codec, e.login, e.cfg, ip, server, [this] { return e.dir.user_count(); });
  }
  std::unique_ptr<CharConnection> char_conn(std::uint64_t id = 1) {
    return std::make_unique<CharConnection>(codec, e.chr, e.cfg, e.rng, id, "192.0.2.10", server,
                                            [](const Character&) { return std::string("fake_map"); });
  }
  std::vector<DecodedPacket> replies(const HandlerResult& r, std::size_t skip_raw = 0) {
    std::vector<DecodedPacket> out;
    for (std::size_t i = skip_raw; i < r.out.size(); ++i) out.push_back(codec.decode_s2c(r.out[i]));
    return out;
  }
  // Log in through the login connection, return the accept packet.
  DecodedPacket do_login(const std::string& user = "fakeuser", const std::string& pass = kFakePass) {
    auto lc = login_conn();
    auto r = lc->feed(login_pkt(user, pass));
    CHECK(r.out.size() == 1);
    return codec.decode_s2c(r.out[0]);
  }
  // Enter char select with the ids from an accept packet; returns the replies after the raw 4-byte ack.
  std::vector<DecodedPacket> enter(CharConnection& cc, const DecodedPacket& acc, HandlerResult* raw = nullptr) {
    auto r = cc.feed(pkt("CH_ENTER", [&](DecodedPacket& p) {
      set_int(p, "account_id", I(acc, "AID")); set_int(p, "login_id1", I(acc, "login_id1"));
      set_int(p, "login_id2", I(acc, "login_id2")); set_int(p, "sex", I(acc, "sex")); }));
    if (raw) *raw = r;
    CHECK(!r.out.empty());
    return replies(r, 1);
  }
};

}  // namespace

// ---- netproto facts the gateway builds on -------------------------------------------------------------------
TEST("CAP-24", real_catalog_login_and_connect_packets_are_typed_and_read_the_offsets_the_legacy_handler_reads) {
  Net n;
  const auto* login = n.codec.resolved("CA_LOGIN");
  CHECK(login && login->version->opcode == 0x0064 && login->version->length.fixed_size == 55 && login->version->fields_known &&
        login->version->source == "common-struct");                            // a typed legacy struct, no gateway overlay any more
  const auto* enter = n.codec.resolved("CZ_ENTER");
  CHECK(enter && enter->version->opcode == 0x0436 && enter->version->length.fixed_size == 23);   // RE flavour, not 19
  CHECK(enter->version->fields_known && enter->version->source == "handler-derived");
  std::vector<std::uint16_t> offs;                                              // the catalog's field offsets are the offsets the handler reads
  for (const auto& f : n.codec.fields_of("CZ_ENTER")) offs.push_back(f.offset);
  CHECK(offs == enter->version->handler_offsets);
  CHECK(n.codec.version().to_string() == "re/20211103");
  CHECK(*n.codec.set().opcode_of("CZ_ENTER", netproto::Direction::ClientToServer) == 0x0436);
  CHECK(n.codec.set().name_of(0x0064) == std::string("CA_LOGIN"));
  CHECK(n.codec.set().packet_count() == 1186);                                  // the whole catalog, not a subset
  CHECK(n.codec.has_packet("ZC_NOTIFY_PLAYERMOVE") && !n.codec.has_packet("NO_SUCH_PACKET"));
}

TEST("CAP-24", declared_lengths_match_the_legacy_packet_table_where_it_lists_the_packet) {
  Net n;
  struct Row { std::uint16_t op; const char* name; };
  for (const Row& r : {Row{0x0064, "CA_LOGIN"}, Row{0x0065, "CH_ENTER"}, Row{0x0066, "CH_SELECT_CHAR"},
                       Row{0x0187, "PING"}, Row{0x01fb, "CH_DELETE_CHAR"}, Row{0x0436, "CZ_ENTER"}}) {
    const auto* res = n.codec.resolved(r.name);
    CHECK(res && res->version->opcode == r.op);
    CHECK(legacy_len(r.op) == static_cast<int>(res->version->length.fixed_size));
  }
}

namespace {
// every packet the three listeners send or receive
const std::vector<std::string>& gateway_packets() {
  static const std::vector<std::string> v = {
      "CA_LOGIN", "CA_LOGIN2", "CA_LOGIN3", "CA_LOGIN4", "CA_LOGIN_PCBANG", "CA_LOGIN_CHANNEL", "CA_SSO_LOGIN_REQ", "CA_REQ_HASH",
      "CA_EXE_HASHCHECK", "CA_CONNECT_INFO_CHANGED", "CT_AUTH", "AC_REFUSE_LOGIN", "AC_ACK_HASH", "TC_RESULT", "AC_ACCEPT_LOGIN",
      "SC_NOTIFY_BAN", "CH_ENTER", "PING", "CH_CHARLIST_REQ", "CH_SELECT_CHAR", "CH_MAKE_CHAR", "CH_DELETE_CHAR",
      "CH_DELETE_CHAR3_RESERVED", "CH_DELETE_CHAR3", "CH_DELETE_CHAR3_CANCEL", "CH_SECOND_PASSWD_ACK", "CH_MAKE_SECOND_PASSWD",
      "CH_EDIT_SECOND_PASSWD", "CH_AVAILABLE_SECOND_PASSWD", "CH_REQ_IS_VALID_CHARNAME", "CH_REQ_CHANGE_CHARNAME",
      "CH_REQ_CHANGE_CHARACTER_SLOT", "HC_REFUSE_ENTER", "HC_ACCEPT_ENTER", "HC_ACK_CHARINFO_PER_PAGE", "HC_ACCEPT_ENTER2",
      "HC_CHARLIST_NOTIFY", "HC_BLOCK_CHARACTER", "HC_ACCEPT_MAKECHAR", "HC_REFUSE_MAKECHAR", "HC_ACCEPT_DELETECHAR",
      "HC_REFUSE_DELETECHAR", "HC_DELETE_CHAR3_RESERVED", "HC_DELETE_CHAR3", "HC_DELETE_CHAR3_CANCEL", "HC_SECOND_PASSWD_LOGIN",
      "HC_NOTIFY_ZONESVR", "HC_ACK_IS_VALID_CHARNAME", "HC_ACK_CHANGE_CHARNAME", "HC_ACK_CHANGE_CHARACTER_SLOT",
      "CZ_ENTER", "CZ_NOTIFY_ACTORINIT", "OPAQUE_006A"};
  return v;
}
std::size_t extent(const netproto::FieldSpec& f) {
  using netproto::FieldType;
  switch (f.type) {
    case FieldType::U8: case FieldType::I8: return 1u * f.array_len;
    case FieldType::U16: case FieldType::I16: return 2u * f.array_len;
    case FieldType::U32: case FieldType::I32: case FieldType::F32: return 4u * f.array_len;
    case FieldType::U64: case FieldType::I64: return 8u * f.array_len;
    case FieldType::Pos3: return 3;
    case FieldType::Pos6: return 6;
    default: return f.width;   // string / raw (0 = a tail)
  }
}
}  // namespace

TEST("CAP-24", every_gateway_layout_fits_its_length_without_overlap_and_row_geometry_is_legacys) {
  Net n;
  CHECK(gateway_packets().size() == 53);
  for (const std::string& name : gateway_packets()) {
    const auto* rp = n.codec.resolved(name);
    CHECK(rp != nullptr);
    const auto& lr = rp->version->length;
    const std::size_t limit = lr.kind == netproto::LengthKind::Fixed ? lr.fixed_size : lr.header_size;
    std::vector<std::pair<std::size_t, std::size_t>> spans;
    for (const auto& f : n.codec.fields_of(name)) {
      if (netproto::is_row_field(f)) continue;
      const std::size_t w = extent(f);
      if (w == 0) { CHECK(f.offset == limit); continue; }   // a tail starts where the header ends
      spans.push_back({f.offset, f.offset + w});
      CHECK(f.offset + w <= limit);
    }
    std::sort(spans.begin(), spans.end());
    for (std::size_t i = 1; i < spans.size(); ++i) CHECK(spans[i].first >= spans[i - 1].second);
  }
  // CHARACTER_INFO at RE 20211103: sizeof 175 and these offsets, from g++ on src/common/packets.hpp
  const auto* ent = n.codec.resolved("HC_ACCEPT_ENTER");
  CHECK(ent->version->length.header_size == 27 && ent->version->length.repeat->stride == 175 && wire::kCharInfoSize == 175);
  std::map<std::string, int> off;
  for (const auto& f : n.codec.fields_of("HC_ACCEPT_ENTER")) if (netproto::is_row_field(f)) off[f.name] = f.offset;
  CHECK(off["GID"] == 0 && off["hp"] == 50 && off["level"] == 92 && off["name"] == 108 && off["Str"] == 132 &&
        off["CharNum"] == 138 && off["mapName"] == 142 && off["DelRevDate"] == 158 && off["sex"] == 174);
  const auto* acc = n.codec.resolved("AC_ACCEPT_LOGIN");
  CHECK(acc->version->length.header_size == 64 && acc->version->length.repeat->stride == 160);
}

TEST("CAP-24", decode_encode_roundtrip_and_malformed_input) {
  Net n;
  Bytes f = n.login_pkt("fakeuser", "Fake-Pass-1");
  CHECK(f.size() == 55 && f[0] == 0x64 && f[1] == 0x00);
  auto d = n.codec.decode(f);
  CHECK(d.name == "CA_LOGIN" && get_str(d, "username") == "fakeuser" && get_str(d, "password") == "Fake-Pass-1" && I(d, "version") == 20211103);
  CHECK(n.codec.encode(d) == f);
  Bytes enter = n.pkt("CZ_ENTER", [](DecodedPacket& p) { set_int(p, "account_id", 2000001); set_int(p, "char_id", 150001);
                                                          set_int(p, "login_id1", 0xdeadbeef); set_int(p, "client_tick", 77); set_int(p, "sex", 1); });
  CHECK(enter.size() == 23 && enter[0] == 0x36 && enter[1] == 0x04);
  auto de = n.codec.decode(enter);
  CHECK(I(de, "login_id1") == 0xdeadbeef && I(de, "sex") == 1 && I(de, "account_id") == 2000001);
  bool threw = false;
  try { n.codec.decode(Bytes(f.begin(), f.end() - 1)); } catch (const WireError&) { threw = true; }
  CHECK(threw);                                                           // truncated frame
  threw = false;
  try { n.codec.decode(Bytes{0xff, 0xff, 0, 0}); } catch (const WireError&) { threw = true; }
  CHECK(threw);                                                           // unknown opcode
  threw = false;
  try { (void)n.codec.encode(make("NO_SUCH_PACKET")); } catch (const WireError&) { threw = true; }
  CHECK(threw);
  auto s = make("AC_REFUSE_LOGIN");
  set_int(s, "error", 6); set_str(s, "unblock_time", std::string(19, 'x'));   // the longest text that still has room for its NUL
  Bytes sb = n.codec.encode(s);
  CHECK(sb.size() == 26 && sb[25] == 0 && sb[24] == 'x' && sb[0] == 0x3e && sb[1] == 0x08);
  // an over-long string is REFUSED, never cut (the legacy safestrncpy cut silently; the private codec copied that)
  for (std::size_t len : {20u, 21u, 40u}) {
    set_str(s, "unblock_time", std::string(len, 'x'));
    threw = false;
    try { (void)n.codec.encode(s); } catch (const WireError& e) { threw = std::string(e.what()).find("does not fit") != std::string::npos; }
    CHECK(threw);
  }
  {
    auto l = make("CA_LOGIN");
    set_str(l, "username", std::string(24, 'u'));
    threw = false;
    try { (void)n.codec.encode(l); } catch (const WireError&) { threw = true; }
    CHECK(threw);
    set_str(l, "username", std::string(23, 'u'));
    CHECK(n.codec.encode(l)[6 + 22] == 'u' && n.codec.encode(l)[6 + 23] == 0);
  }
  // a NUL inside a string would end it early on the wire: refused as well
  {
    auto l = make("CA_LOGIN");
    set_str(l, "username", std::string("a\0b", 3));
    threw = false;
    try { (void)n.codec.encode(l); } catch (const WireError&) { threw = true; }
    CHECK(threw);
  }
  // an unknown field name is an error, not a silent drop
  {
    auto l = make("CA_LOGIN");
    set_int(l, "nope", 1);
    threw = false;
    try { (void)n.codec.encode(l); } catch (const WireError&) { threw = true; }
    CHECK(threw);
  }
}

// ---- login listener ------------------------------------------------------------------------------------------------
TEST("BR-LOGIN-001", login_accept_packet_carries_ticket_ids_server_entry_and_web_token) {
  Net n;
  n.e.add_account();
  auto lc = n.login_conn();
  auto r = lc->feed(n.login_pkt("fakeuser", kFakePass));
  CHECK(!r.disconnect && r.out.size() == 1 && r.out[0].size() == 64 + 160);
  CHECK(r.out[0][0] == 0xc4 && r.out[0][1] == 0x0a && (r.out[0][2] | r.out[0][3] << 8) == 224);
  auto a = n.codec.decode_s2c(r.out[0]);
  auto acc = n.e.accounts.find(2000001);
  CHECK(I(a, "AID") == 2000001 && I(a, "login_id1") == acc->login_id1 && I(a, "login_id2") == acc->login_id2);
  CHECK(I(a, "sex") == 1 && get_str(a, "token") == acc->web_token && get_str(a, "token").size() == 16);
  CHECK(a.repeats.size() == 1);
  const auto& row = a.repeats[0];
  CHECK(std::get<std::string>(row.at("name")) == "fake-server" && std::get<std::int64_t>(row.at("port")) == 6121);
  CHECK(std::get<std::int64_t>(row.at("ip")) == (192 | 0 << 8 | 2 << 16 | 1 << 24));    // octets in wire order
  CHECK(std::get<std::int64_t>(row.at("users")) == 0);                                   // green
}

TEST("BR-LOGIN-021", population_code_follows_user_count_and_can_be_disabled) {
  Net n;
  n.e.add_account();
  for (auto [users, want] : {std::pair{0, 0}, {200, 0}, {201, 1}, {500, 1}, {501, 2}, {1000, 2}, {1001, 3}}) {
    n.e.dir.erase(2000001);
    int u = users;
    LoginConnection lc(n.codec, n.e.login, n.e.cfg, "192.0.2.10", n.server, [u] { return u; });
    auto a = n.codec.decode_s2c(lc.feed(n.login_pkt("fakeuser", kFakePass)).out[0]);
    CHECK(std::get<std::int64_t>(a.repeats[0].at("users")) == want);
  }
  n.e.dir.erase(2000001);
  UserCountThresholds off; off.disable = true;
  LoginConnection lc(n.codec, n.e.login, n.e.cfg, "192.0.2.10", n.server, [] { return 5; }, off);
  CHECK(std::get<std::int64_t>(n.codec.decode_s2c(lc.feed(n.login_pkt("fakeuser", kFakePass)).out[0]).repeats[0].at("users")) == 4);
}

TEST("BR-LOGIN-020", lan_subnet_clients_get_the_lan_address) {
  Net n;
  n.e.add_account();
  LanSubnet lan; lan.net = 0xC0A80000; lan.mask = 0xFFFF0000; lan.ip[0] = 192; lan.ip[1] = 168; lan.ip[2] = 1; lan.ip[3] = 5;
  CHECK(lan_subnet_for(0xC0A80101, {lan}) != nullptr && lan_subnet_for(0x08080808, {lan}) == nullptr);
  LoginConnection inside(n.codec, n.e.login, n.e.cfg, "192.168.1.9", n.server, [] { return 0; }, {}, {lan}, 0xC0A80109);
  auto a = n.codec.decode_s2c(inside.feed(n.login_pkt("fakeuser", kFakePass)).out[0]);
  CHECK(std::get<std::int64_t>(a.repeats[0].at("ip")) == (192 | 168 << 8 | 1 << 16 | 5 << 24));
  n.e.dir.erase(2000001);
  LoginConnection outside(n.codec, n.e.login, n.e.cfg, "8.8.8.8", n.server, [] { return 0; }, {}, {lan}, 0x08080808);
  auto b = n.codec.decode_s2c(outside.feed(n.login_pkt("fakeuser", kFakePass)).out[0]);
  CHECK(std::get<std::int64_t>(b.repeats[0].at("ip")) == (192 | 0 << 8 | 2 << 16 | 1 << 24));
}

namespace {
struct RealMd5Hasher : FakeHasher {   // the fixture hasher with a REAL md5_hex, because the wire carries real digests
  std::string md5_hex(const std::string& d) override { return crypto::md5_hex(d); }
};
}  // namespace

TEST("BR-LOGIN-003", md5_challenge_over_the_wire_both_digest_orders) {
  Net n;
  n.e.add_account();
  RealMd5Hasher hasher;
  GatewayDeps d{n.e.cfg, n.e.clock, n.e.rng, n.e.accounts, n.e.chars, n.e.dir, n.e.bus, n.e.tickets, hasher, n.e.bans, n.e.fails, n.e.policy};
  LoginService svc(d);
  for (bool salt_first : {true, false}) {
    n.e.dir.erase(2000001);
    LoginConnection lc(n.codec, svc, n.e.cfg, "192.0.2.10", n.server, [] { return 0; });
    auto kr = lc.feed(n.pkt("CA_REQ_HASH"));
    CHECK(kr.out.size() == 1 && kr.out[0][0] == 0xdc && kr.out[0][1] == 0x01);
    const Bytes sb = get_bytes(n.codec.decode_s2c(kr.out[0]), "salt");
    CHECK(sb.size() == 20);
    for (auto c : sb) CHECK(c != 0);                                          // BR-LOGIN-005 over the wire
    const std::string salt(sb.begin(), sb.end());
    const std::string dg = crypto::md5(salt_first ? salt + kFakePass : std::string(kFakePass) + salt);
    auto r = lc.feed(n.pkt("CA_LOGIN2", [&](DecodedPacket& p) {
      set_int(p, "version", 20211103); set_str(p, "username", "fakeuser"); set_bytes(p, "passwordMD5", Bytes(dg.begin(), dg.end())); }));
    CHECK(r.out.size() == 1 && r.out[0][0] == 0xc4);                          // accepted in either order
    n.e.dir.erase(2000001);
    const std::string wrong = crypto::md5(salt + "Wrong-Pass-9");
    auto r2 = lc.feed(n.pkt("CA_LOGIN2", [&](DecodedPacket& p) {
      set_str(p, "username", "fakeuser"); set_bytes(p, "passwordMD5", Bytes(wrong.begin(), wrong.end())); }));
    CHECK(I(n.codec.decode_s2c(r2.out[0]), "error") == 1);
  }
  // without CA_REQ_HASH there is no session salt: the challenge cannot succeed
  LoginConnection lc2(n.codec, svc, n.e.cfg, "192.0.2.10", n.server, [] { return 0; });
  const std::string dg = crypto::md5(std::string(kFakePass));
  auto r3 = lc2.feed(n.pkt("CA_LOGIN3", [&](DecodedPacket& p) { set_str(p, "username", "fakeuser"); set_bytes(p, "passwordMD5", Bytes(dg.begin(), dg.end())); }));
  CHECK(I(n.codec.decode_s2c(r3.out[0]), "error") == 1);
}

TEST("BR-LOGIN-004", challenge_logins_are_refused_with_3_when_the_shim_is_disabled) {
  Net n;
  n.e.add_account();
  n.e.cfg.md5_challenge_enabled = false;
  auto lc = n.login_conn();
  auto r = lc->feed(n.pkt("CA_LOGIN2", [](DecodedPacket& p) { set_str(p, "username", "fakeuser"); set_bytes(p, "passwordMD5", Bytes(16, 1)); }));
  CHECK(r.disconnect && n.codec.decode_s2c(r.out[0]).name == "AC_REFUSE_LOGIN" && I(n.codec.decode_s2c(r.out[0]), "error") == 3);
}

TEST("BR-LOGIN-002", sso_and_otp_packets) {
  Net n;
  n.e.add_account();
  auto lc = n.login_conn();
  // the token is a `char token[]` string tail: the encoder puts the NUL after it, the decoder cuts at it
  const std::size_t toklen = std::strlen(kFakePass) + 1;
  auto sso = n.pkt("CA_SSO_LOGIN_REQ", [&](DecodedPacket& p) {
    set_int(p, "version", 20211103); set_str(p, "username", "fakeuser"); set_str(p, "token", kFakePass); });
  CHECK(sso.size() == 92 + toklen && (sso[2] | sso[3] << 8) == static_cast<int>(sso.size()) && sso.back() == 0);
  CHECK(get_str(n.codec.decode(sso), "token") == kFakePass);
  auto r = lc->feed(sso);
  CHECK(r.out.size() == 1 && r.out[0][0] == 0xc4);
  // wrong token => 1
  n.e.dir.erase(2000001);
  auto r2 = lc->feed(n.pkt("CA_SSO_LOGIN_REQ", [&](DecodedPacket& p) { set_str(p, "username", "fakeuser"); set_str(p, "token", "xxxxx"); }));
  CHECK(I(n.codec.decode_s2c(r2.out[0]), "error") == 1);
  // OTP: fixed "normal login" answer, grants nothing
  auto r3 = lc->feed(n.pkt("CT_AUTH"));
  auto t = n.codec.decode_s2c(r3.out[0]);
  CHECK(t.name == "TC_RESULT" && I(t, "type") == 0 && get_str(t, "unknown1") == "S1000");
  // PACKET_TC_RESULT has a packetLength word (variable in the legacy header): 34 bytes, the length at offset 2
  CHECK(r3.out[0].size() == 34 && (r3.out[0][2] | r3.out[0][3] << 8) == 34 && n.codec.resolved("TC_RESULT")->version->length.kind == netproto::LengthKind::Variable);
  CHECK(!n.e.dir.find(2000001).has_value());
}

TEST("BR-LOGIN-013", exe_hash_packet_is_used_by_the_client_hash_check) {
  Net n;
  n.e.add_account();
  n.e.cfg.client_hash_check = true;
  n.e.cfg.client_hash_nodes = {{0, "00112233445566778899aabbccddeeff"}};
  auto lc = n.login_conn();
  CHECK(I(n.codec.decode_s2c(lc->feed(n.login_pkt("fakeuser", kFakePass)).out[0]), "error") == 5);
  Bytes h; for (int i = 0; i < 16; ++i) h.push_back(static_cast<std::uint8_t>(i * 0x11));
  lc->feed(n.pkt("CA_EXE_HASHCHECK", [&](DecodedPacket& p) { set_bytes(p, "hash", h); }));
  CHECK(n.codec.decode_s2c(lc->feed(n.login_pkt("fakeuser", kFakePass)).out[0]).name == "AC_ACCEPT_LOGIN");
}

TEST("BR-LOGIN-011", ban_refusal_carries_the_unblock_date_and_other_refusal_packets) {
  Net n;
  auto& a = n.e.add_account();
  Account u = a; u.unban_time = 1700003600; n.e.accounts.update(u);            // now = 2023-11-14 22:13:20 UTC, +1 h
  auto lc = n.login_conn();
  auto b = n.codec.decode_s2c(lc->feed(n.login_pkt("fakeuser", kFakePass)).out[0]);
  CHECK(b.name == "AC_REFUSE_LOGIN" && I(b, "error") == 6 && get_str(b, "unblock_time") == "2023-11-14 23:13:20");
  u.unban_time = 0; n.e.accounts.update(u);
  n.e.bus.up = false;
  auto c = n.codec.decode_s2c(lc->feed(n.login_pkt("fakeuser", kFakePass)).out[0]);
  CHECK(c.name == "SC_NOTIFY_BAN" && I(c, "errorCode") == 1);                       // world down: "server closed"
  n.e.bus.up = true;
  n.e.dir.mark({2000001, 0, 5, n.e.clock.now()});
  auto d = n.codec.decode_s2c(lc->feed(n.login_pkt("fakeuser", kFakePass)).out[0]);
  CHECK(d.name == "SC_NOTIFY_BAN" && I(d, "errorCode") == 8 && n.e.bus.kicks.size() == 1);
  auto e = n.codec.decode_s2c(lc->feed(n.login_pkt("nobody", "x")).out[0]);
  CHECK(e.name == "AC_REFUSE_LOGIN" && I(e, "error") == 0 && get_str(e, "unblock_time").empty());
}

TEST("BR-LOGIN-022", ip_banned_connection_gets_3_and_is_dropped) {
  Net n;
  n.e.add_account();
  n.e.bans.add("192.0.2.*", n.e.clock.now() + 60);
  auto lc = n.login_conn();
  auto r = lc->feed(n.login_pkt("fakeuser", kFakePass));
  CHECK(r.disconnect && I(n.codec.decode_s2c(r.out[0]), "error") == 3 && lc->closed());
  CHECK(lc->feed(n.login_pkt("fakeuser", kFakePass)).out.empty());               // closed connections say nothing
}

TEST("BR-MAP-059", framing_gating_and_budget_apply_to_the_gateway_listeners) {
  Net n;
  n.e.add_account();
  {  // unknown opcode => disconnect
    auto lc = n.login_conn();
    CHECK(lc->feed(Bytes{0xee, 0x0e, 1, 2, 3}).disconnect);
  }
  {  // a char-phase packet on the login listener is not in the login table's phase: dropped
    auto lc = n.login_conn();
    auto r = lc->feed(n.pkt("CH_SELECT_CHAR", [](DecodedPacket& p) { set_int(p, "slot", 0); }));
    CHECK(r.disconnect);
  }
  {  // declared length below the header => disconnect before the payload arrives (S4)
    auto lc = n.login_conn();
    Bytes f = {0x25, 0x08, 0x03, 0x00};
    CHECK(lc->feed(f).disconnect);
  }
  {  // byte-by-byte delivery reassembles one frame
    auto lc = n.login_conn();
    Bytes f = n.login_pkt("fakeuser", kFakePass);
    HandlerResult last;
    std::size_t outs = 0;
    for (std::uint8_t b : f) { last = lc->feed(Bytes{b}); outs += last.out.size(); CHECK(!last.disconnect); }
    CHECK(outs == 1);
  }
  {  // 5 requests in one read: 3 are processed now, 2 stay buffered (delayed, not dropped)
    auto lc = n.login_conn();
    Bytes all;
    for (int i = 0; i < 5; ++i) { Bytes f = n.pkt("CA_CONNECT_INFO_CHANGED"); all.insert(all.end(), f.begin(), f.end()); }
    Bytes req = n.pkt("CA_REQ_HASH");
    Bytes mix;
    for (int i = 0; i < 5; ++i) mix.insert(mix.end(), req.begin(), req.end());
    auto r1 = lc->feed(mix);
    CHECK(r1.out.size() == 3 && lc->has_pending());
    auto r2 = lc->pump();
    CHECK(r2.out.size() == 2 && !lc->has_pending());
    (void)all;
  }
  {  // a receive buffer over the bound is a disconnect (BR-INTER-012)
    auto lc = n.login_conn();
    Bytes junk(70000, 0x00);
    CHECK(lc->feed(junk).disconnect);
  }
}

// ---- char-select listener ---------------------------------------------------------------------------------------
TEST("BR-CHAR-026", enter_packet_sequence_ack_list_notify_and_pin_state) {
  Net n;
  auto& a = n.e.add_account();
  n.c0 = n.e.add_char(a.id, "Bobby", 0);
  { auto ch = *n.e.chars.find(n.c0); ch.max_hp = 40; ch.max_sp = 11; ch.slot_moves = 2; ch.rename_counter = 1; n.e.chars.update(ch); }
  auto acc = n.do_login();
  auto cc = n.char_conn();
  HandlerResult raw;
  auto rep = n.enter(*cc, acc, &raw);
  CHECK(raw.out[0] == u32le(2000001));                                           // legacy: bare account id first
  CHECK(rep.size() == 4 && rep[0].name == "HC_ACCEPT_ENTER2" && rep[1].name == "HC_ACCEPT_ENTER" &&
        rep[2].name == "HC_CHARLIST_NOTIFY" && rep[3].name == "HC_SECOND_PASSWD_LOGIN");
  CHECK(I(rep[0], "producible") == 9 && I(rep[0], "total") == 15 && I(rep[0], "normal") == 3);
  CHECK(rep[1].repeats.size() == 1);
  const auto& row = rep[1].repeats[0];
  CHECK(std::get<std::string>(row.at("name")) == "Bobby" && std::get<std::int64_t>(row.at("GID")) == static_cast<std::int64_t>(n.c0) &&
        std::get<std::int64_t>(row.at("maxhp")) == 40 && std::get<std::int64_t>(row.at("chr_slot_changeCnt")) == 2 &&
        std::get<std::string>(row.at("mapName")) == "fake_map" && std::get<std::int64_t>(row.at("sex")) == 1 &&
        std::get<std::int64_t>(row.at("bIsChangedCharName")) == 0);
  CHECK(I(rep[2], "total") == 3);
  CHECK(I(rep[3], "result") == 0 && I(rep[3], "AID") == 2000001);                // PIN: forced=false & none set => passed (0)
  CHECK(cc->session().phase == rules::SessionPhase::Ready);
}

TEST("BR-CHAR-022", pin_gate_over_the_wire_with_keypad_transport) {
  Net n;
  auto& a = n.e.add_account();
  n.c0 = n.e.add_char(a.id, "Bobby", 0);
  Account u = a; u.pin = n.e.hasher.hash("1357"); u.pin_changed = n.e.clock.now(); n.e.accounts.update(u);
  auto acc = n.do_login();
  auto cc = n.char_conn();
  auto rep = n.enter(*cc, acc);
  CHECK(rep.back().name == "HC_SECOND_PASSWD_LOGIN" && I(rep.back(), "result") == 1);         // ask
  // select before the PIN => disconnect
  auto early = cc->feed(n.pkt("CH_SELECT_CHAR", [](DecodedPacket& p) { set_int(p, "slot", 0); }));
  CHECK(early.disconnect && early.note.find("PIN") != std::string::npos);
}

TEST("BR-CHAR-021", pin_check_success_wrong_and_lockout_over_the_wire) {
  Net n;
  auto& a = n.e.add_account();
  n.c0 = n.e.add_char(a.id, "Bobby", 0);
  Account u = a; u.pin = n.e.hasher.hash("1357"); u.pin_changed = n.e.clock.now(); n.e.accounts.update(u);
  auto acc = n.do_login();
  auto cc = n.char_conn();
  auto rep = n.enter(*cc, acc);
  std::uint32_t seed = static_cast<std::uint32_t>(I(rep.back(), "seed"));
  auto send_pin = [&](const std::string& real) {
    const std::string wire = pin_transport(seed, real);
    auto r = cc->feed(n.pkt("CH_SECOND_PASSWD_ACK", [&](DecodedPacket& p) {
      set_int(p, "AID", 2000001); set_bytes(p, "pin", Bytes(wire.begin(), wire.end())); }));
    if (!r.out.empty()) seed = static_cast<std::uint32_t>(I(n.codec.decode_s2c(r.out.back()), "seed"));   // every state packet re-seeds
    return r;
  };
  auto bad = send_pin("0000");
  CHECK(!bad.disconnect && I(n.codec.decode_s2c(bad.out[0]), "result") == 8);                // PINCODE_WRONG
  auto good = send_pin("1357");
  CHECK(I(n.codec.decode_s2c(good.out[0]), "result") == 0 && cc->session().phase == rules::SessionPhase::Ready);
  // now selecting works
  auto sel = cc->feed(n.pkt("CH_SELECT_CHAR", [](DecodedPacket& p) { set_int(p, "slot", 0); }));
  auto z = n.codec.decode_s2c(sel.out[0]);
  CHECK(z.name == "HC_NOTIFY_ZONESVR" && I(z, "CID") == static_cast<std::int64_t>(n.c0) && get_str(z, "mapname") == "fake_map" &&
        I(z, "port") == 6121 && I(z, "ip") == (192 | 0 << 8 | 2 << 16 | 1 << 24));
  // lockout on a fresh session
  n.e.dir.erase(2000001);
  n.e.accounts.update([&] { Account x = *n.e.accounts.find(2000001); x.pin_failures = 0; return x; }());
  auto acc2 = n.do_login();
  auto cc2 = n.char_conn(5);
  auto rep2 = n.enter(*cc2, acc2);
  seed = static_cast<std::uint32_t>(I(rep2.back(), "seed"));
  cc = std::move(cc2);
  send_pin("0000"); send_pin("0000");
  auto last = send_pin("0000");
  CHECK(last.disconnect && last.note == "pin lockout" && n.e.bus.kicks.size() >= 1);
}

TEST("BR-CHAR-020", make_and_edit_pin_packets) {
  Net n;
  auto& a = n.e.add_account();
  n.c0 = n.e.add_char(a.id, "Bobby", 0);
  n.e.cfg.pin_forced = true;
  auto acc = n.do_login();
  auto cc = n.char_conn();
  auto rep = n.enter(*cc, acc);
  CHECK(I(rep.back(), "result") == 4);                                            // NEW
  std::uint32_t seed = static_cast<std::uint32_t>(I(rep.back(), "seed"));
  auto set_pin = [&](const char* name, const std::string& oldp, const std::string& newp) {
    const std::string wo = pin_transport(seed, oldp), wn = pin_transport(seed, newp);
    auto r = cc->feed(n.pkt(name, [&](DecodedPacket& p) {
      set_int(p, "AID", 2000001);
      if (std::string(name) == "CH_MAKE_SECOND_PASSWD") set_bytes(p, "pin", Bytes(wn.begin(), wn.end()));
      else { set_bytes(p, "old_pin", Bytes(wo.begin(), wo.end())); set_bytes(p, "new_pin", Bytes(wn.begin(), wn.end())); } }));
    CHECK(r.out.size() == 1);
    auto d = n.codec.decode_s2c(r.out[0]);
    seed = static_cast<std::uint32_t>(I(d, "seed"));
    return I(d, "result");
  };
  CHECK(set_pin("CH_MAKE_SECOND_PASSWD", "", "1234") == 5);                       // illegal
  CHECK(set_pin("CH_MAKE_SECOND_PASSWD", "", "1357") == 0);                       // passed
  CHECK(cc->session().phase == rules::SessionPhase::Ready);
  CHECK(set_pin("CH_MAKE_SECOND_PASSWD", "", "2468") == 8);                       // cannot overwrite without the old one
  CHECK(set_pin("CH_EDIT_SECOND_PASSWD", "9999", "2468") == 8);
  CHECK(set_pin("CH_EDIT_SECOND_PASSWD", "1357", "1111") == 5);
  CHECK(set_pin("CH_EDIT_SECOND_PASSWD", "1357", "2468") == 0);
  // window request
  auto w = cc->feed(n.pkt("CH_AVAILABLE_SECOND_PASSWD", [](DecodedPacket& p) { set_int(p, "AID", 2000001); }));
  CHECK(I(n.codec.decode_s2c(w.out[0]), "result") == 1);                          // ask (a PIN exists now)
  // a foreign account id is a disconnect
  auto bad = cc->feed(n.pkt("CH_AVAILABLE_SECOND_PASSWD", [](DecodedPacket& p) { set_int(p, "AID", 5); }));
  CHECK(bad.disconnect);
}

TEST("BR-CHAR-001", make_char_packet_accept_and_refuse_codes) {
  Net n;
  auto& a = n.e.add_account();
  auto acc = n.do_login();
  auto cc = n.char_conn();
  n.enter(*cc, acc);
  auto make_pkt = [&](const std::string& name, int slot, int job = 0, int sex = 1) {
    return cc->feed(n.pkt("CH_MAKE_CHAR", [&](DecodedPacket& p) {
      set_str(p, "name", name); set_int(p, "slot", slot); set_int(p, "hair_color", 3); set_int(p, "hair_style", 4);
      set_int(p, "job", job); set_int(p, "sex", sex); }));
  };
  auto ok = n.codec.decode_s2c(make_pkt("Bobby", 0).out[0]);
  CHECK(ok.name == "HC_ACCEPT_MAKECHAR" && get_str(ok, "character.name") == "Bobby" && I(ok, "character.maxhp") == 40 &&
        I(ok, "character.maxsp") == 11 && I(ok, "character.CharNum") == 0);
  auto stored = n.e.chars.list_by_account(a.id).at(0);
  CHECK(stored.extra.at("client.hair_style") == 4 && stored.extra.at("client.hair_color") == 3);
  CHECK(I(n.codec.decode_s2c(make_pkt("Bobby", 1).out[0]), "error") == 0x00);       // exists (-1)
  CHECK(I(n.codec.decode_s2c(make_pkt("Carol", 0).out[0]), "error") == 0xFF);       // slot used (-2)
  CHECK(I(n.codec.decode_s2c(make_pkt("Dave", 99).out[0]), "error") == 0x03);       // slot not available (-4)
  CHECK(I(n.codec.decode_s2c(make_pkt("Erin", 2, 999).out[0]), "error") == 0xFF);   // unknown job
  auto frank = n.codec.decode_s2c(make_pkt("Frank", 3, 4218).out[0]);                 // the second allowed start job
  CHECK(frank.name == "HC_ACCEPT_MAKECHAR");
  CHECK(n.e.chars.list_by_account(a.id).at(1).extra.at("client.job") == 4218 && n.e.chars.list_by_account(a.id).at(1).extra.at("start_job_flag") == 2);
  n.e.cfg.char_new = false;
  CHECK(I(n.codec.decode_s2c(make_pkt("Hank", 5).out[0]), "error") == 0xFF);
}

TEST("BR-CHAR-009", delete_flows_over_the_wire) {
  Net n;
  auto& a = n.e.add_account();
  CharId c = n.e.add_char(a.id, "Bobby", 0), g = n.e.add_char(a.id, "Guilded", 1);
  n.e.bus.members[g] = {0, 9};
  auto acc = n.do_login();
  auto cc = n.char_conn();
  n.enter(*cc, acc);
  auto req = [&](CharId id) { return n.codec.decode_s2c(cc->feed(n.pkt("CH_DELETE_CHAR3_RESERVED", [&](DecodedPacket& p) { set_int(p, "CID", id); })).out[0]); };
  auto r1 = req(c);
  CHECK(r1.name == "HC_DELETE_CHAR3_RESERVED" && I(r1, "result") == 1 && I(r1, "date") == 86400);         // remaining seconds
  CHECK(I(req(c), "result") == 0);                                                                       // already queued
  CHECK(I(req(g), "result") == 4);                                                                       // in a guild
  CHECK(I(req(99999), "result") == 3);                                                                   // not found
  auto confirm = [&](CharId id, const char* birth) {
    auto r = cc->feed(n.pkt("CH_DELETE_CHAR3", [&](DecodedPacket& p) { set_int(p, "CID", id); set_bytes(p, "birthdate", Bytes(birth, birth + 6)); }));
    return n.codec.decode_s2c(r.out.back());
  };
  CHECK(I(confirm(c, "000000"), "result") == 5);
  CHECK(I(confirm(c, "900101"), "result") == 4);                                                         // delay not over
  n.e.clock.advance(86401);
  auto done = cc->feed(n.pkt("CH_DELETE_CHAR3", [&](DecodedPacket& p) { set_int(p, "CID", c); set_bytes(p, "birthdate", Bytes{'9', '0', '0', '1', '0', '1'}); }));
  CHECK(done.out.size() >= 2 && n.codec.decode_s2c(done.out.back()).name == "HC_DELETE_CHAR3" && I(n.codec.decode_s2c(done.out.back()), "result") == 1);
  CHECK(!n.e.chars.find(c));
  // cancel
  CHECK(I(n.codec.decode_s2c(cc->feed(n.pkt("CH_DELETE_CHAR3_CANCEL", [&](DecodedPacket& p) { set_int(p, "CID", g); })).out[0]), "result") == 1);
  CHECK(I(n.codec.decode_s2c(cc->feed(n.pkt("CH_DELETE_CHAR3_CANCEL", [&](DecodedPacket& p) { set_int(p, "CID", 5); })).out[0]), "result") == 2);
}

TEST("BR-CHAR-011", legacy_delete_packet_maps_results_to_refuse_codes) {
  Net n;
  auto& a = n.e.add_account();
  CharId c = n.e.add_char(a.id, "Bobby", 0);
  auto acc = n.do_login();
  auto cc = n.char_conn();
  n.enter(*cc, acc);
  n.e.cfg.char_del_option = 3;
  n.e.cfg.char_del_delay_s = 0;
  auto del = [&](CharId id, const char* key) {
    return n.codec.decode_s2c(cc->feed(n.pkt("CH_DELETE_CHAR", [&](DecodedPacket& p) { set_int(p, "CID", id); set_str(p, "key", key); })).out[0]);
  };
  CHECK(I(del(c, "wrong@example.invalid"), "error") == 0 && n.codec.decode_s2c(cc->feed(n.pkt("CH_DELETE_CHAR", [&](DecodedPacket& p) { set_int(p, "CID", 77); set_str(p, "key", "900101"); })).out[0]).name == "HC_REFUSE_DELETECHAR");
  CHECK(I(del(424242, "900101"), "error") == 1);                                  // not found
  CHECK(del(c, "user@example.invalid").name == "HC_ACCEPT_DELETECHAR");
}

TEST("BR-CHAR-017", rename_slot_move_and_name_check_packets) {
  Net n;
  auto& a = n.e.add_account();
  CharId c = n.e.add_char(a.id, "Bobby", 0);
  { auto ch = *n.e.chars.find(c); ch.rename_counter = 1; ch.slot_moves = 1; n.e.chars.update(ch); }
  auto acc = n.do_login();
  auto cc = n.char_conn();
  n.enter(*cc, acc);
  auto ok1 = n.codec.decode_s2c(cc->feed(n.pkt("CH_REQ_IS_VALID_CHARNAME", [&](DecodedPacket& p) {
    set_int(p, "AID", a.id); set_int(p, "CID", c); set_str(p, "new_name", "Robert"); })).out[0]);
  CHECK(ok1.name == "HC_ACK_IS_VALID_CHARNAME" && I(ok1, "result") == 1);
  auto rn = cc->feed(n.pkt("CH_REQ_CHANGE_CHARNAME", [&](DecodedPacket& p) { set_int(p, "CID", c); set_str(p, "new_name", "Robert"); }));
  CHECK(rn.out.size() == 4 && I(n.codec.decode_s2c(rn.out[0]), "result") == 0 && n.e.chars.find(c)->name == "Robert");   // + the refreshed list
  auto mv = cc->feed(n.pkt("CH_REQ_CHANGE_CHARACTER_SLOT", [&](DecodedPacket& p) { set_int(p, "slot_before", 0); set_int(p, "slot_after", 4); }));
  auto ack = n.codec.decode_s2c(mv.out[0]);
  CHECK(ack.name == "HC_ACK_CHANGE_CHARACTER_SLOT" && I(ack, "reason") == 0 && I(ack, "moves") == 0 && n.e.chars.find(c)->slot == 4);
  auto mv2 = cc->feed(n.pkt("CH_REQ_CHANGE_CHARACTER_SLOT", [&](DecodedPacket& p) { set_int(p, "slot_before", 4); set_int(p, "slot_after", 5); }));
  CHECK(I(n.codec.decode_s2c(mv2.out[0]), "reason") == 1);                        // no moves left
  auto bad = cc->feed(n.pkt("CH_REQ_IS_VALID_CHARNAME", [&](DecodedPacket& p) { set_int(p, "AID", 4); set_int(p, "CID", c); set_str(p, "new_name", "x"); }));
  CHECK(bad.disconnect);
}

TEST("BR-CHAR-026", enter_refusals_over_the_wire_use_the_right_packets) {
  Net n;
  auto& a = n.e.add_account();
  auto acc = n.do_login();
  auto enter_with = [&](std::int64_t aid, std::int64_t id1, std::int64_t id2, std::int64_t sex, CharConnection& cc) {
    return cc.feed(n.pkt("CH_ENTER", [&](DecodedPacket& p) { set_int(p, "account_id", aid); set_int(p, "login_id1", id1); set_int(p, "login_id2", id2); set_int(p, "sex", sex); }));
  };
  auto cc = n.char_conn();
  auto bad = enter_with(a.id, I(acc, "login_id1"), I(acc, "login_id2") + 1, 1, *cc);
  CHECK(bad.disconnect && bad.out.size() == 2 && n.codec.decode_s2c(bad.out[1]).name == "HC_REFUSE_ENTER" && I(n.codec.decode_s2c(bad.out[1]), "error") == 0);
  auto cc2 = n.char_conn(2);
  auto badsex = enter_with(a.id, I(acc, "login_id1"), I(acc, "login_id2"), 2, *cc2);
  CHECK(badsex.disconnect);                                                       // sex 2 is the server account marker
  // world session in progress: SC_NOTIFY_BAN(8)
  n.e.dir.mark({a.id, 0, 5, n.e.clock.now()});
  auto cc3 = n.char_conn(3);
  auto dup = enter_with(a.id, I(acc, "login_id1"), I(acc, "login_id2"), 1, *cc3);
  CHECK(dup.disconnect && n.codec.decode_s2c(dup.out[1]).name == "SC_NOTIFY_BAN" && I(n.codec.decode_s2c(dup.out[1]), "errorCode") == 8);
}

TEST("BR-CHAR-015", select_answers_for_missing_and_closed_cases) {
  Net n;
  auto& a = n.e.add_account();
  CharId c = n.e.add_char(a.id, "Bobby", 0);
  auto acc = n.do_login();
  auto cc = n.char_conn();
  n.enter(*cc, acc);
  auto sel = [&](int slot) { return cc->feed(n.pkt("CH_SELECT_CHAR", [&](DecodedPacket& p) { set_int(p, "slot", slot); })); };
  auto none = n.codec.decode_s2c(sel(5).out[0]);
  CHECK(none.name == "HC_REFUSE_ENTER" && I(none, "error") == 0);
  n.e.bus.up = false;
  auto closed = n.codec.decode_s2c(sel(0).out[0]);
  CHECK(closed.name == "SC_NOTIFY_BAN" && I(closed, "errorCode") == 1);
  n.e.bus.up = true;
  CHECK(n.codec.decode_s2c(sel(0).out[0]).name == "HC_NOTIFY_ZONESVR");
  (void)c;
}

TEST("BR-CHAR-027", disconnecting_a_char_select_connection_frees_the_online_slot) {
  Net n;
  n.e.add_account();
  auto acc = n.do_login();
  {
    auto cc = n.char_conn(7);
    n.enter(*cc, acc);
    CHECK(n.e.dir.find(2000001) && n.e.dir.find(2000001)->conn_id == 7);
  }
  CHECK(!n.e.dir.find(2000001).has_value());
}

// ---- world connect ---------------------------------------------------------------------------------------------------------
TEST("BR-CHAR-031", world_connect_packet_full_handoff_and_refusals) {
  Net n;
  auto& a = n.e.add_account();
  CharId c = n.e.add_char(a.id, "Bobby", 0);
  auto acc = n.do_login();
  auto cc = n.char_conn();
  n.enter(*cc, acc);
  CHECK(cc->feed(n.pkt("CH_SELECT_CHAR", [](DecodedPacket& p) { set_int(p, "slot", 0); })).out.size() == 1);
  auto connect = [&](WorldConnection& w, std::int64_t aid, std::int64_t cid, std::int64_t auth, std::int64_t sex) {
    return w.feed(n.pkt("CZ_ENTER", [&](DecodedPacket& p) { set_int(p, "account_id", aid); set_int(p, "char_id", cid); set_int(p, "login_id1", auth); set_int(p, "client_tick", 5); set_int(p, "sex", sex); }));
  };
  {  // BR-MAP-060 checks come first
    WorldConnection w(n.codec, n.e.chr);
    auto r = connect(w, 5, c, I(acc, "login_id1"), 1);
    // clif_parse_WantToConnection answers on opcode 0x006a with packet_len(0x6a) = 23 bytes and the code at byte 2
    // (src/map/clif.cpp:10685): neither ZC_REFUSE_ENTER (0x0074, 3 bytes) nor a 3-byte 0x006a
    CHECK(r.disconnect && r.out.size() == 1 && r.out[0].size() == 23 && r.out[0][0] == 0x6a && r.out[0][1] == 0x00 && r.out[0][2] == 2);
    CHECK(std::all_of(r.out[0].begin() + 3, r.out[0].end(), [](std::uint8_t b) { return b == 0; }));   // the legacy left these unset; we send zeros
    CHECK(n.codec.decode_s2c(r.out[0]).name == std::string(wire::kConnectRefusal));
    WorldConnection w2(n.codec, n.e.chr);
    CHECK(connect(w2, a.id, 0, I(acc, "login_id1"), 1).out.at(0).at(2) == 3);
    WorldConnection w3(n.codec, n.e.chr);
    CHECK(connect(w3, a.id, c, I(acc, "login_id1"), 7).out.at(0).at(2) == 6);
  }
  {  // phase gate: the load-end ack before the connect packet is out of phase
    WorldConnection w(n.codec, n.e.chr);
    CHECK(w.feed(Bytes{0x7d, 0x00}).disconnect);                                     // CZ_NOTIFY_ACTORINIT, gate PreMapLoad
  }
  {  // wrong auth code: SC_NOTIFY_BAN(0) and disconnect; the right code afterwards still works (ticket not burnt)
    WorldConnection w(n.codec, n.e.chr);
    auto r = connect(w, a.id, c, I(acc, "login_id1") + 1, 1);
    CHECK(r.disconnect && n.codec.decode_s2c(r.out[0]).name == "SC_NOTIFY_BAN" && I(n.codec.decode_s2c(r.out[0]), "errorCode") == 0);
  }
  WorldConnection w(n.codec, n.e.chr);
  auto ok = connect(w, a.id, c, I(acc, "login_id1"), 1);
  CHECK(!ok.disconnect && ok.world_entered && ok.account_id == a.id && ok.char_id == c);
  CHECK(connect(w, a.id, c, I(acc, "login_id1"), 1).out.empty());                    // second connect packet: ignored
  CHECK(n.e.dir.find(a.id)->state == 0);
  WorldConnection replay(n.codec, n.e.chr);
  CHECK(connect(replay, a.id, c, I(acc, "login_id1"), 1).disconnect);                 // single use
}

TEST("BR-CHAR-031", login_ids_presented_straight_to_the_world_do_not_bypass_char_select) {
  Net n;
  auto& a = n.e.add_account();
  CharId c = n.e.add_char(a.id, "Bobby", 0);
  auto acc = n.do_login();
  WorldConnection w(n.codec, n.e.chr);
  auto r = w.feed(n.pkt("CZ_ENTER", [&](DecodedPacket& p) { set_int(p, "account_id", a.id); set_int(p, "char_id", c);
                                                         set_int(p, "login_id1", I(acc, "login_id1")); set_int(p, "sex", 1); }));
  CHECK(r.disconnect && !r.world_entered);
}

TEST("BR-CHAR-020", keypad_transport_matches_the_legacy_table_and_is_invertible) {
  // tables computed from char_pincode_decrypt (char.cpp:2156) for these seeds
  struct Row { std::uint32_t seed; const char* tab; };
  for (const Row& r : {Row{0, "3061984527"}, Row{1, "4081729653"}, Row{12345, "9051826473"}, Row{65534, "5091734268"}}) {
    std::string wire = "0123", real;
    for (char c : wire) real.push_back(r.tab[c - '0']);
    CHECK(pin_untransport(r.seed, wire) == real);
    CHECK(pin_transport(r.seed, real) == wire);
  }
  CHECK(pin_untransport(5, "12a4").empty() && pin_untransport(5, "123").empty() && pin_transport(5, "12345").empty());
  for (std::uint32_t seed : {7u, 999u, 40000u})
    for (const char* pin : {"0000", "1357", "9876"}) CHECK(pin_untransport(seed, pin_transport(seed, pin)) == pin);
}

TEST("BR-CHAR-008", sex_to_wire_and_error_mapping_table) {
  CHECK(sex_to_wire(Sex::Female) == 0 && sex_to_wire(Sex::Male) == 1 && sex_to_wire(Sex::Server) == 2);
}
