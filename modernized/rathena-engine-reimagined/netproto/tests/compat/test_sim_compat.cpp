// Compatibility with the sim's private codec (tooling/src/sim_wire.cpp, included read-only): every packet
// the sim's table defines is built with the sim's own helpers and with netproto's codec over the full
// catalog; the bytes must be identical, the sim's decoder must read back what netproto's decoder reads,
// and the frame metadata (opcode, length, direction, gate) must agree. After this the sim can replace
// sim_wire.cpp by netproto::SimClient + the catalog. Disagreements are findings, pinned below.
#include <functional>
#include <iostream>
#include <random>
#include <set>

#include "../harness.hpp"
#include "../real_catalog.hpp"
#include "netproto/sim_client.hpp"
#include "tooling/sim_wire.hpp"

using namespace netproto;
using namespace fx;
namespace sw = tooling::sim::wire;

namespace {

using Rng = std::mt19937_64;
std::int64_t u32(Rng& g) { return static_cast<std::int64_t>(g() & 0xFFFFFFFFu); }
std::int64_t gid(Rng& g) { return static_cast<std::int64_t>(g() % 0x7FFFFFFFu); }  // the sim's ids are 31-bit
std::int64_t n16(Rng& g) { return static_cast<std::int64_t>(g() & 0xFFFF); }
std::int64_t n8(Rng& g) { return static_cast<std::int64_t>(g() & 0xFF); }
std::int64_t pos(Rng& g) { return static_cast<std::int64_t>(g() % 1024); }
std::string text(Rng& g, std::size_t max) {
  std::string s;
  for (std::size_t i = 0, n = g() % (max + 1); i < n; ++i) s += static_cast<char>('a' + g() % 26);
  return s;
}
FieldValue I(std::int64_t v) { return v; }
Bytes with_nul(const std::string& s) { Bytes b(s.begin(), s.end()); b.push_back(0); return b; }

struct Row {
  const char* sim_name;
  const char* np_name;
  sw::Dir dir;
  // draws random arguments; returns the sim's bytes and the same packet as netproto values
  std::function<Bytes(Rng&, DecodedPacket&)> make;
  // every decoded sim field must equal the corresponding netproto value
  std::function<void(const sw::Packet&, const DecodedPacket&)> same;
};

#define SAME(expr) do { if (!(expr)) throw CheckFailure(std::string("sim decode differs from netproto decode: ") + #expr); } while (0)

std::vector<Row> rows() {
  auto num = [](const DecodedPacket& p, const char* k) { return std::get<std::int64_t>(p.fields.at(k)); };
  std::vector<Row> r;
  r.push_back({"CZ_ENTER", "CZ_ENTER", sw::Dir::C2S, [](Rng& g, DecodedPacket& p) {
    const auto a = u32(g), c = u32(g), l = u32(g), t = u32(g), s = n8(g);
    p.fields = {{"account_id", I(a)}, {"char_id", I(c)}, {"login_id1", I(l)}, {"client_tick", I(t)}, {"sex", I(s)}};
    return sw::cz_enter(static_cast<uint32_t>(a), static_cast<uint32_t>(c), static_cast<uint32_t>(l), static_cast<uint32_t>(t), static_cast<uint8_t>(s)); },
    [=](const sw::Packet& s, const DecodedPacket& p) { SAME(s.num("aid") == num(p, "account_id") && s.num("cid") == num(p, "char_id") && s.num("login_id1") == num(p, "login_id1") && s.num("tick") == num(p, "client_tick") && s.num("sex") == num(p, "sex")); }});
  r.push_back({"CZ_NOTIFY_ACTORINIT", "CZ_NOTIFY_ACTORINIT", sw::Dir::C2S, [](Rng&, DecodedPacket&) { return sw::cz_notify_actorinit(); }, [](const sw::Packet&, const DecodedPacket&) {}});
  r.push_back({"CZ_REQUEST_MOVE", "CZ_REQUEST_MOVE", sw::Dir::C2S, [](Rng& g, DecodedPacket& p) {
    const Pos3 q{static_cast<uint16_t>(pos(g)), static_cast<uint16_t>(pos(g)), static_cast<uint8_t>(g() % 16)};
    p.fields["pos"] = q; return sw::cz_request_move(q.x, q.y, q.dir); },
    [](const sw::Packet& s, const DecodedPacket& p) { const auto q = std::get<Pos3>(p.fields.at("pos")); SAME(s.num("x") == q.x && s.num("y") == q.y && s.num("dir") == q.dir); }});
  r.push_back({"CZ_REQUEST_TIME", "CZ_REQUEST_TIME", sw::Dir::C2S, [](Rng& g, DecodedPacket& p) { const auto t = u32(g); p.fields["client_tick"] = I(t); return sw::cz_request_time(static_cast<uint32_t>(t)); },
    [=](const sw::Packet& s, const DecodedPacket& p) { SAME(s.num("tick") == num(p, "client_tick")); }});
  r.push_back({"CZ_REQUEST_CHAT", "CZ_REQUEST_CHAT", sw::Dir::C2S, [](Rng& g, DecodedPacket& p) { const auto t = text(g, 40); p.fields["tail"] = with_nul(t); return sw::cz_request_chat(t); },
    [](const sw::Packet& s, const DecodedPacket& p) { const auto& b = std::get<Bytes>(p.fields.at("tail")); SAME(s.text("text") == std::string(b.begin(), b.end() - 1)); }});
  r.push_back({"CZ_CONTACTNPC", "CZ_CONTACTNPC", sw::Dir::C2S, [](Rng& g, DecodedPacket& p) { const auto a = gid(g), t = n8(g); p.fields = {{"AID", I(a)}, {"type", I(t)}}; return sw::cz_contactnpc(static_cast<uint32_t>(a), static_cast<uint8_t>(t)); },
    [=](const sw::Packet& s, const DecodedPacket& p) { SAME(s.num("gid") == num(p, "AID") && s.num("type") == num(p, "type")); }});
  r.push_back({"CZ_REQ_NEXT_SCRIPT", "CZ_REQ_NEXT_SCRIPT", sw::Dir::C2S, [](Rng& g, DecodedPacket& p) { const auto a = gid(g); p.fields["f2"] = I(a); return sw::cz_req_next_script(static_cast<uint32_t>(a)); },
    [=](const sw::Packet& s, const DecodedPacket& p) { SAME(s.num("gid") == num(p, "f2")); }});
  r.push_back({"CZ_CHOOSE_MENU", "CZ_CHOOSE_MENU", sw::Dir::C2S, [](Rng& g, DecodedPacket& p) { const auto a = gid(g), c = n8(g); p.fields = {{"npc_id", I(a)}, {"select", I(c)}}; return sw::cz_choose_menu(static_cast<uint32_t>(a), static_cast<uint8_t>(c)); },
    [=](const sw::Packet& s, const DecodedPacket& p) { SAME(s.num("gid") == num(p, "npc_id") && s.num("choice") == num(p, "select")); }});
  r.push_back({"CZ_CLOSE_DIALOG", "CZ_CLOSE_DIALOG", sw::Dir::C2S, [](Rng& g, DecodedPacket& p) { const auto a = gid(g); p.fields["GID"] = I(a); return sw::cz_close_dialog(static_cast<uint32_t>(a)); },
    [=](const sw::Packet& s, const DecodedPacket& p) { SAME(s.num("gid") == num(p, "GID")); }});
  r.push_back({"CZ_INPUT_EDITDLG", "CZ_INPUT_EDITDLG", sw::Dir::C2S, [](Rng& g, DecodedPacket& p) {
    const auto a = gid(g); const auto v = static_cast<std::int32_t>(g()); p.fields = {{"GID", I(a)}, {"value", I(v)}}; return sw::cz_input_editdlg(static_cast<uint32_t>(a), v); },
    [=](const sw::Packet& s, const DecodedPacket& p) { SAME(s.num("gid") == num(p, "GID") && s.num("value") == num(p, "value")); }});
  r.push_back({"CZ_INPUT_EDITDLGSTR", "CZ_INPUT_EDITDLGSTR", sw::Dir::C2S, [](Rng& g, DecodedPacket& p) {
    const auto a = gid(g); const auto t = text(g, 30); p.fields = {{"GID", I(a)}, {"value", t}}; return sw::cz_input_editdlgstr(static_cast<uint32_t>(a), t); },
    [=](const sw::Packet& s, const DecodedPacket& p) { SAME(s.num("gid") == num(p, "GID") && s.text("text") == std::get<std::string>(p.fields.at("value"))); }});
  r.push_back({"CZ_REQUEST_ACT2", "CZ_REQUEST_ACT2", sw::Dir::C2S, [](Rng& g, DecodedPacket& p) { const auto a = gid(g), t = n8(g); p.fields = {{"f2", I(a)}, {"f6", I(t)}}; return sw::cz_request_act(static_cast<uint32_t>(a), static_cast<uint8_t>(t)); },
    [=](const sw::Packet& s, const DecodedPacket& p) { SAME(s.num("gid") == num(p, "f2") && s.num("action") == num(p, "f6")); }});
  r.push_back({"CZ_USE_SKILL", "CZ_USE_SKILL2", sw::Dir::C2S, [](Rng& g, DecodedPacket& p) {
    const auto l = n16(g), k = n16(g), t = gid(g); p.fields = {{"f2", I(l)}, {"f4", I(k)}, {"f6", I(t)}}; return sw::cz_use_skill(static_cast<uint16_t>(l), static_cast<uint16_t>(k), static_cast<uint32_t>(t)); },
    [=](const sw::Packet& s, const DecodedPacket& p) { SAME(s.num("level") == num(p, "f2") && s.num("skill") == num(p, "f4") && s.num("gid") == num(p, "f6")); }});
  // ---- server to client
  r.push_back({"ZC_ACCEPT_ENTER", "ZC_ACCEPT_ENTER", sw::Dir::S2C, [](Rng& g, DecodedPacket& p) {
    const auto t = u32(g); const Pos3 q{static_cast<uint16_t>(pos(g)), static_cast<uint16_t>(pos(g)), static_cast<uint8_t>(g() % 16)};
    p.fields = {{"startTime", I(t)}, {"posDir", q}, {"xSize", I(5)}, {"ySize", I(5)}, {"font", I(0)}};
    return sw::zc_accept_enter(static_cast<uint32_t>(t), q.x, q.y, q.dir); },
    [=](const sw::Packet& s, const DecodedPacket& p) { const auto q = std::get<Pos3>(p.fields.at("posDir")); SAME(s.num("tick") == num(p, "startTime") && s.num("x") == q.x && s.num("y") == q.y && s.num("dir") == q.dir); }});
  r.push_back({"ZC_REFUSE_ENTER", "ZC_REFUSE_ENTER", sw::Dir::S2C, [](Rng& g, DecodedPacket& p) { const auto c = n8(g); p.fields["errorCode"] = I(c); return sw::zc_refuse_enter(static_cast<uint8_t>(c)); },
    [=](const sw::Packet& s, const DecodedPacket& p) { SAME(s.num("code") == num(p, "errorCode")); }});
  r.push_back({"ZC_NOTIFY_TIME", "ZC_NOTIFY_TIME", sw::Dir::S2C, [](Rng& g, DecodedPacket& p) { const auto t = u32(g); p.fields["time"] = I(t); return sw::zc_notify_time(static_cast<uint32_t>(t)); },
    [=](const sw::Packet& s, const DecodedPacket& p) { SAME(s.num("tick") == num(p, "time")); }});
  r.push_back({"ZC_NOTIFY_PLAYERMOVE", "ZC_NOTIFY_PLAYERMOVE", sw::Dir::S2C, [](Rng& g, DecodedPacket& p) {
    const auto t = u32(g); const Pos6 q{static_cast<uint16_t>(pos(g)), static_cast<uint16_t>(pos(g)), static_cast<uint16_t>(pos(g)), static_cast<uint16_t>(pos(g)), 8, 8};
    p.fields = {{"moveStartTime", I(t)}, {"moveData", q}}; return sw::zc_notify_playermove(static_cast<uint32_t>(t), q.x0, q.y0, q.x1, q.y1); },
    [=](const sw::Packet& s, const DecodedPacket& p) { const auto q = std::get<Pos6>(p.fields.at("moveData")); SAME(s.num("tick") == num(p, "moveStartTime") && s.num("x0") == q.x0 && s.num("y0") == q.y0 && s.num("x1") == q.x1 && s.num("y1") == q.y1); }});
  r.push_back({"ZC_STOPMOVE", "ZC_STOPMOVE", sw::Dir::S2C, [](Rng& g, DecodedPacket& p) {
    const auto a = gid(g), x = n16(g), y = n16(g); p.fields = {{"AID", I(a)}, {"xPos", I(x)}, {"yPos", I(y)}}; return sw::zc_stopmove(static_cast<uint32_t>(a), static_cast<uint16_t>(x), static_cast<uint16_t>(y)); },
    [=](const sw::Packet& s, const DecodedPacket& p) { SAME(s.num("gid") == num(p, "AID") && s.num("x") == num(p, "xPos") && s.num("y") == num(p, "yPos")); }});
  r.push_back({"ZC_NOTIFY_VANISH", "ZC_NOTIFY_VANISH", sw::Dir::S2C, [](Rng& g, DecodedPacket& p) { const auto a = gid(g), t = n8(g); p.fields = {{"gid", I(a)}, {"type", I(t)}}; return sw::zc_notify_vanish(static_cast<uint32_t>(a), static_cast<uint8_t>(t)); },
    [=](const sw::Packet& s, const DecodedPacket& p) { SAME(s.num("gid") == num(p, "gid") && s.num("type") == num(p, "type")); }});
  r.push_back({"ZC_NOTIFY_STANDENTRY11", "LEGACY_IDLE_UNIT", sw::Dir::S2C, [](Rng& g, DecodedPacket& p) {
    sw::Actor a; a.gid = static_cast<uint32_t>(gid(g)); a.object_type = static_cast<uint8_t>(g() % 7); a.speed = static_cast<uint16_t>(n16(g)); a.job = static_cast<uint16_t>(g() % 8000);
    a.level = static_cast<uint16_t>(g() % 1000); a.max_hp = static_cast<uint32_t>(gid(g)); a.hp = static_cast<uint32_t>(gid(g)); a.sex = static_cast<uint8_t>(g() % 2);
    a.name = text(g, 23); a.x = static_cast<uint16_t>(pos(g)); a.y = static_cast<uint16_t>(pos(g)); a.dir = static_cast<uint16_t>(g() % 16);
    p.fields = {{"objecttype", I(a.object_type)}, {"AID", I(a.gid)}, {"GID", I(a.gid)}, {"speed", I(a.speed < 32768 ? a.speed : a.speed - 65536)}, {"job", I(a.job)}, {"PosDir", Pos3{a.x, a.y, static_cast<uint8_t>(a.dir)}},
                {"xSize", I(5)}, {"ySize", I(5)}, {"clevel", I(a.level)}, {"maxHP", I(a.max_hp)}, {"HP", I(a.hp)}, {"sex", I(a.sex)}, {"name", a.name}};
    return sw::zc_notify_standentry(a); },
    [=](const sw::Packet& s, const DecodedPacket& p) { const auto q = std::get<Pos3>(p.fields.at("PosDir")); SAME(s.num("gid") == num(p, "GID") && s.num("object_type") == num(p, "objecttype") && s.num("job") == num(p, "job") && s.num("x") == q.x && s.num("y") == q.y && s.num("dir") == q.dir && s.num("level") == num(p, "clevel") && s.num("max_hp") == num(p, "maxHP") && s.num("hp") == num(p, "HP") && s.text("actor") == std::get<std::string>(p.fields.at("name"))); }});
  r.push_back({"ZC_NOTIFY_MOVEENTRY11", "LEGACY_UNIT_WALKING", sw::Dir::S2C, [](Rng& g, DecodedPacket& p) {
    sw::Actor a; a.gid = static_cast<uint32_t>(gid(g)); a.object_type = static_cast<uint8_t>(g() % 7); a.speed = static_cast<uint16_t>(g() % 30000); a.job = static_cast<uint16_t>(g() % 8000);
    a.level = static_cast<uint16_t>(g() % 1000); a.max_hp = static_cast<uint32_t>(gid(g)); a.hp = static_cast<uint32_t>(gid(g)); a.sex = static_cast<uint8_t>(g() % 2);
    a.name = text(g, 23); a.x = static_cast<uint16_t>(pos(g)); a.y = static_cast<uint16_t>(pos(g)); a.to_x = static_cast<uint16_t>(pos(g)); a.to_y = static_cast<uint16_t>(pos(g)); a.move_start = static_cast<uint32_t>(u32(g));
    p.fields = {{"objecttype", I(a.object_type)}, {"AID", I(a.gid)}, {"GID", I(a.gid)}, {"speed", I(a.speed)}, {"job", I(a.job)}, {"moveStartTime", I(a.move_start)}, {"MoveData", Pos6{a.x, a.y, a.to_x, a.to_y, 8, 8}},
                {"xSize", I(5)}, {"ySize", I(5)}, {"clevel", I(a.level)}, {"maxHP", I(a.max_hp)}, {"HP", I(a.hp)}, {"sex", I(a.sex)}, {"name", a.name}};
    return sw::zc_notify_moveentry(a); },
    [=](const sw::Packet& s, const DecodedPacket& p) { const auto q = std::get<Pos6>(p.fields.at("MoveData")); SAME(s.num("gid") == num(p, "GID") && s.num("move_start") == num(p, "moveStartTime") && s.num("x") == q.x0 && s.num("y") == q.y0 && s.num("to_x") == q.x1 && s.num("to_y") == q.y1 && s.num("level") == num(p, "clevel") && s.text("actor") == std::get<std::string>(p.fields.at("name"))); }});
  r.push_back({"ZC_NOTIFY_CHAT", "ZC_NOTIFY_CHAT", sw::Dir::S2C, [](Rng& g, DecodedPacket& p) { const auto a = gid(g); const auto t = text(g, 50); p.fields = {{"GID", I(a)}, {"Message", t}}; return sw::zc_notify_chat(static_cast<uint32_t>(a), t); },
    [=](const sw::Packet& s, const DecodedPacket& p) { SAME(s.num("gid") == num(p, "GID") && s.text("text") == std::get<std::string>(p.fields.at("Message"))); }});
  r.push_back({"ZC_NOTIFY_PLAYERCHAT", "OPAQUE_008E", sw::Dir::S2C, [](Rng& g, DecodedPacket& p) { const auto t = text(g, 50); p.fields["payload"] = with_nul(t); return sw::zc_notify_playerchat(t); },
    [](const sw::Packet& s, const DecodedPacket& p) { const auto& b = std::get<Bytes>(p.fields.at("payload")); SAME(s.text("text") == std::string(b.begin(), b.end() - 1)); }});
  r.push_back({"ZC_SAY_DIALOG", "ZC_SAY_DIALOG", sw::Dir::S2C, [](Rng& g, DecodedPacket& p) { const auto a = gid(g); const auto t = text(g, 50); p.fields = {{"NpcID", I(a)}, {"message", t}}; return sw::zc_say_dialog(static_cast<uint32_t>(a), t); },
    [=](const sw::Packet& s, const DecodedPacket& p) { SAME(s.num("gid") == num(p, "NpcID") && s.text("text") == std::get<std::string>(p.fields.at("message"))); }});
  r.push_back({"ZC_WAIT_DIALOG", "ZC_WAIT_DIALOG", sw::Dir::S2C, [](Rng& g, DecodedPacket& p) { const auto a = gid(g); p.fields["NpcID"] = I(a); return sw::zc_wait_dialog(static_cast<uint32_t>(a)); },
    [=](const sw::Packet& s, const DecodedPacket& p) { SAME(s.num("gid") == num(p, "NpcID")); }});
  r.push_back({"ZC_CLOSE_DIALOG", "ZC_CLOSE_DIALOG", sw::Dir::S2C, [](Rng& g, DecodedPacket& p) { const auto a = gid(g); p.fields["npcId"] = I(a); return sw::zc_close_dialog(static_cast<uint32_t>(a)); },
    [=](const sw::Packet& s, const DecodedPacket& p) { SAME(s.num("gid") == num(p, "npcId")); }});
  r.push_back({"ZC_MENU_LIST", "ZC_MENU_LIST", sw::Dir::S2C, [](Rng& g, DecodedPacket& p) {
    const auto a = gid(g); std::vector<std::string> o; std::string joined; for (unsigned n = 1 + g() % 4; n--;) { o.push_back(text(g, 8)); joined += o.back() + ":"; }
    p.fields = {{"npcId", I(a)}, {"menu", joined}}; return sw::zc_menu_list(static_cast<uint32_t>(a), o); },
    [=](const sw::Packet& s, const DecodedPacket& p) { SAME(s.num("gid") == num(p, "npcId") && s.text("menu") == std::get<std::string>(p.fields.at("menu"))); }});
  r.push_back({"ZC_OPEN_EDITDLGSTR", "ZC_OPEN_EDITDLGSTR", sw::Dir::S2C, [](Rng& g, DecodedPacket& p) { const auto a = gid(g); p.fields["npcId"] = I(a); return sw::zc_open_editdlgstr(static_cast<uint32_t>(a)); },
    [=](const sw::Packet& s, const DecodedPacket& p) { SAME(s.num("gid") == num(p, "npcId")); }});
  r.push_back({"ZC_NOTIFY_ACT", "ZC_NOTIFY_ACT", sw::Dir::S2C, [](Rng& g, DecodedPacket& p) {
    const auto s = gid(g), d = gid(g), t = u32(g); const auto ss = static_cast<std::int32_t>(g()), ds = static_cast<std::int32_t>(g()), dm = static_cast<std::int32_t>(g()), dm2 = static_cast<std::int32_t>(g());
    const auto dv = n16(g), ty = n8(g);
    p.fields = {{"srcID", I(s)}, {"targetID", I(d)}, {"serverTick", I(static_cast<std::int32_t>(t))}, {"srcSpeed", I(ss)}, {"dmgSpeed", I(ds)}, {"damage", I(dm)}, {"isSPDamage", I(0)}, {"div", I(dv)}, {"type", I(ty)}, {"damage2", I(dm2)}};
    return sw::zc_notify_act(static_cast<uint32_t>(s), static_cast<uint32_t>(d), static_cast<uint32_t>(t), ss, ds, dm, static_cast<uint16_t>(dv), static_cast<uint8_t>(ty), dm2); },
    [=](const sw::Packet& s, const DecodedPacket& p) { SAME(s.num("src") == num(p, "srcID") && s.num("dst") == num(p, "targetID") && s.num("src_speed") == num(p, "srcSpeed") && s.num("dmg_speed") == num(p, "dmgSpeed") && s.num("damage") == num(p, "damage") && s.num("div") == num(p, "div") && s.num("type") == num(p, "type") && s.num("damage2") == num(p, "damage2")); }});
  return r;
}

const ResolvedPacket* np(const char* name) {
  for (const auto& rp : full_resolved()) if (rp.def->name == name) return &rp;
  return nullptr;
}
}  // namespace

TEST("CAP-E02", sim_table_covers_the_29_packets_and_all_are_in_the_netproto_catalog) {
  CHECK(sw::defs().size() == 29);
  const auto table = rows();
  CHECK(table.size() == 29);
  std::set<std::string> seen;
  for (const auto& row : table) { CHECK(seen.insert(row.sim_name).second); CHECK(sw::find_name(row.sim_name) != nullptr); CHECK(np(row.np_name) != nullptr); }
}

TEST("CAP-E02", sim_frame_metadata_agrees_with_the_netproto_catalog) {
  // opcode, fixed length / variable, direction and gate of every sim packet against the catalog
  std::vector<std::string> findings;
  for (const auto& row : rows()) {
    const sw::Def* d = sw::find_name(row.sim_name);
    const ResolvedPacket* n = np(row.np_name);
    CHECK(d && n);
    if (n->version->opcode != d->opcode) findings.push_back(std::string(row.sim_name) + ": opcode");
    if (d->len >= 0 ? (n->version->length.kind != LengthKind::Fixed || n->version->length.fixed_size != d->len)
                    : (n->version->length.kind != LengthKind::Variable || n->version->length.prefix_offset != 2))
      findings.push_back(std::string(row.sim_name) + ": length");
    const bool c2s = d->dir == sw::Dir::C2S;
    if (c2s != (n->def->direction == Direction::ClientToServer)) findings.push_back(std::string(row.sim_name) + ": direction");
    if (n->def->direction != Direction::Any && d->gate != n->def->gate) findings.push_back(std::string(row.sim_name) + ": gate");
    if (n->def->direction == Direction::Any && d->gate != Phase::InWorld) findings.push_back(std::string(row.sim_name) + ": gate (length-only)");
  }
  for (const auto& f : findings) std::cerr << "FINDING " << f << "\n";
  CHECK(findings.empty());
}

TEST("CAP-E02", sim_frames_are_byte_identical_with_netproto_encode_and_decode_to_the_same_values) {
  Rng g(2024);
  std::size_t frames = 0;
  for (const auto& row : rows()) {
    for (int i = 0; i < 300; ++i) {
      DecodedPacket p;
      p.name = row.np_name;
      const Bytes sim_bytes = row.make(g, p);
      const Bytes np_bytes = full_set().encode(p);
      if (sim_bytes != np_bytes) throw CheckFailure(std::string(row.sim_name) + ": the sim's frame differs from netproto's encoding of the same values");
      const Direction d = row.dir == sw::Dir::C2S ? Direction::ClientToServer : Direction::ServerToClient;
      const DecodedPacket q = full_set().decode(sim_bytes, d);
      for (const auto& [k, v] : p.fields)
        if (!(q.fields.at(k) == v)) throw CheckFailure(std::string(row.sim_name) + ": netproto decode differs for field " + k);
      const auto sp = sw::decode(sim_bytes, row.dir);
      CHECK(sp.has_value());
      row.same(*sp, q);
      ++frames;
    }
  }
  CHECK(frames == 29 * 300);
}

TEST("CAP-E02", sim_frame_table_and_netproto_frame_tables_split_the_same_streams) {
  // the sim's split_frames over its own table and netproto's peek_frame over the catalog agree on a mixed stream
  Rng g(7);
  Bytes c2s, s2c;
  std::size_t n_c2s = 0, n_s2c = 0;
  for (int i = 0; i < 200; ++i) {
    for (const auto& row : rows()) {
      if (g() % 3 || std::string(row.np_name) == "ZC_REFUSE_ENTER") continue;   // a refusal ends the client's session
      DecodedPacket p; p.name = row.np_name;
      const Bytes b = row.make(g, p);
      (row.dir == sw::Dir::C2S ? c2s : s2c).insert((row.dir == sw::Dir::C2S ? c2s : s2c).end(), b.begin(), b.end());
      (row.dir == sw::Dir::C2S ? n_c2s : n_s2c)++;
    }
  }
  const auto sim_c = sw::split_frames(c2s, sw::frame_table(sw::Dir::C2S, full_set()));
  const auto sim_s = sw::split_frames(s2c, sw::frame_table(sw::Dir::S2C, full_set()));
  CHECK(sim_c.error.empty() && sim_c.frames.size() == n_c2s && sim_c.consumed == c2s.size());
  CHECK(sim_s.error.empty() && sim_s.frames.size() == n_s2c && sim_s.consumed == s2c.size());
  SimClient client(std::make_shared<const CodecSet>(full_set()), std::nullopt);
  CHECK(client.receive(s2c).size() == n_s2c && !client.closed());
  std::size_t pos = 0, count = 0;
  while (pos < c2s.size()) {
    const auto r = peek_frame(ByteView(c2s).subspan(pos), full_set().frames());
    CHECK(r.status == FrameStatus::kComplete);
    pos += r.length;
    ++count;
  }
  CHECK(count == n_c2s && pos == c2s.size());
}

TEST("CAP-E02", sim_vs_legacy_findings_are_the_documented_ones) {
  // 1. names: the sim names two packets after the real client (ZC_NOTIFY_STANDENTRY11 / MOVEENTRY11) and one
  //    with a different suffix (CZ_USE_SKILL for the legacy CZ_USE_SKILL2); the legacy tree never names
  //    them, so the catalog keeps LEGACY_<struct> names (see README, "decisions")
  CHECK(std::string(np("LEGACY_IDLE_UNIT")->def->name) == "LEGACY_IDLE_UNIT" && np("LEGACY_IDLE_UNIT")->version->opcode == 0x09ff);
  // 2. 0x008e (the sim's ZC_NOTIFY_PLAYERCHAT) is length-only in the legacy tables: no struct and no handler
  CHECK(np("OPAQUE_008E")->version->source == "length-only");
  // 3. the sim writes 5 into xSize/ySize of ZC_ACCEPT_ENTER / entry packets: values, not layout
  // 4. the sim's CZ_REQUEST_ACT / CZ_USE_SKILL fields are handler-derived in the catalog and keep their derived
  //    names (f2, f6 ...): the handler assigns them straight into calls, there is no variable to name them after
  CHECK(np("CZ_REQUEST_ACT2")->version->source == "handler-derived" && np("CZ_USE_SKILL2")->version->source == "handler-derived");
  // 5. CZ_INPUT_EDITDLGSTR: the legacy struct types GID as int32; the sim reads it unsigned (same bytes)
  for (const auto& f : np("CZ_INPUT_EDITDLGSTR")->version->fields) if (f.name == "GID") CHECK(f.type == FieldType::I32);
}
