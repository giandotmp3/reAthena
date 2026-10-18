#include "tooling/sim_wire.hpp"

#include <algorithm>
#include <cstring>

namespace tooling::sim::wire {

using netproto::Phase;

int64_t Packet::num(const std::string& k, int64_t dflt) const {
  auto it = fields.find(k);
  if (it == fields.end()) return dflt;
  if (auto* i = std::get_if<int64_t>(&it->second)) return *i;
  return dflt;
}
std::string Packet::text(const std::string& k) const {
  auto it = fields.find(k);
  if (it == fields.end()) return {};
  if (auto* s = std::get_if<std::string>(&it->second)) return *s;
  return {};
}

const std::vector<Def>& defs() {
  static const std::vector<Def> d = {
      // ---- client to server
      {0x0436, "CZ_ENTER", 23, Dir::C2S, Phase::PreSession, "spec"},
      {0x007d, "CZ_NOTIFY_ACTORINIT", 2, Dir::C2S, Phase::PreMapLoad, "spec"},
      {0x035f, "CZ_REQUEST_MOVE", 5, Dir::C2S, Phase::InWorld, "spec"},
      {0x0360, "CZ_REQUEST_TIME", 6, Dir::C2S, Phase::InWorld, "spec"},
      {0x00f3, "CZ_REQUEST_CHAT", -1, Dir::C2S, Phase::InWorld, "spec"},
      {0x0090, "CZ_CONTACTNPC", 7, Dir::C2S, Phase::InWorld, "spec"},
      {0x00b9, "CZ_REQ_NEXT_SCRIPT", 6, Dir::C2S, Phase::InWorld, "legacy-db"},
      {0x00b8, "CZ_CHOOSE_MENU", 7, Dir::C2S, Phase::InWorld, "legacy-db"},
      {0x0146, "CZ_CLOSE_DIALOG", 6, Dir::C2S, Phase::InWorld, "legacy-struct"},
      {0x0143, "CZ_INPUT_EDITDLG", 10, Dir::C2S, Phase::InWorld, "legacy-struct"},
      {0x01d5, "CZ_INPUT_EDITDLGSTR", -1, Dir::C2S, Phase::InWorld, "legacy-struct"},
      {0x0437, "CZ_REQUEST_ACT2", 7, Dir::C2S, Phase::InWorld, "legacy-db"},
      {0x0438, "CZ_USE_SKILL", 10, Dir::C2S, Phase::InWorld, "legacy-db"},
      // ---- server to client
      {0x02eb, "ZC_ACCEPT_ENTER", 13, Dir::S2C, Phase::PreSession, "legacy-struct"},
      {0x0074, "ZC_REFUSE_ENTER", 3, Dir::S2C, Phase::PreSession, "legacy-struct"},
      {0x007f, "ZC_NOTIFY_TIME", 6, Dir::S2C, Phase::InWorld, "spec"},
      {0x0087, "ZC_NOTIFY_PLAYERMOVE", 12, Dir::S2C, Phase::InWorld, "spec"},
      {0x0088, "ZC_STOPMOVE", 10, Dir::S2C, Phase::InWorld, "legacy-struct"},
      {0x0080, "ZC_NOTIFY_VANISH", 7, Dir::S2C, Phase::InWorld, "legacy-struct"},
      {0x09ff, "ZC_NOTIFY_STANDENTRY11", -1, Dir::S2C, Phase::InWorld, "legacy-db"},
      {0x09fd, "ZC_NOTIFY_MOVEENTRY11", -1, Dir::S2C, Phase::InWorld, "legacy-db"},
      {0x008d, "ZC_NOTIFY_CHAT", -1, Dir::S2C, Phase::InWorld, "legacy-struct"},
      {0x008e, "ZC_NOTIFY_PLAYERCHAT", -1, Dir::S2C, Phase::InWorld, "legacy-db"},
      {0x00b4, "ZC_SAY_DIALOG", -1, Dir::S2C, Phase::InWorld, "legacy-struct"},
      {0x00b5, "ZC_WAIT_DIALOG", 6, Dir::S2C, Phase::InWorld, "legacy-struct"},
      {0x00b6, "ZC_CLOSE_DIALOG", 6, Dir::S2C, Phase::InWorld, "legacy-struct"},
      {0x00b7, "ZC_MENU_LIST", -1, Dir::S2C, Phase::InWorld, "legacy-struct"},
      {0x01d4, "ZC_OPEN_EDITDLGSTR", 6, Dir::S2C, Phase::InWorld, "legacy-struct"},
      {0x08c8, "ZC_NOTIFY_ACT", 34, Dir::S2C, Phase::InWorld, "legacy-struct"},
  };
  return d;
}

const Def* find(uint16_t opcode, Dir d) {
  for (const auto& e : defs())
    if (e.opcode == opcode && e.dir == d) return &e;
  return nullptr;
}
const Def* find_name(std::string_view name) {
  for (const auto& e : defs())
    if (name == e.name) return &e;
  return nullptr;
}

netproto::FrameTable frame_table(Dir d, const netproto::CodecSet& spec) {
  netproto::FrameTable t;
  const bool want_zc = d == Dir::S2C;
  for (const auto& [op, e] : spec.frames()) {
    const bool is_zc = e.name.rfind("ZC_", 0) == 0;
    if (is_zc == want_zc) t.emplace(op, e);
  }
  for (const auto& e : defs()) {
    if (e.dir != d || t.count(e.opcode)) continue;
    netproto::FrameEntry fe;
    fe.name = e.name;
    fe.gate = e.gate;
    if (e.len < 0) {
      fe.length.kind = netproto::LengthKind::Variable;
      fe.length.prefix_offset = 2;
      fe.length.header_size = 4;
    } else {
      fe.length.kind = netproto::LengthKind::Fixed;
      fe.length.fixed_size = static_cast<uint16_t>(e.len);
    }
    t.emplace(e.opcode, std::move(fe));
  }
  return t;
}

void put16(Bytes& b, size_t at, uint32_t v) {
  b[at] = static_cast<uint8_t>(v & 0xff);
  b[at + 1] = static_cast<uint8_t>((v >> 8) & 0xff);
}
void put32(Bytes& b, size_t at, uint32_t v) {
  put16(b, at, v & 0xffff);
  put16(b, at + 2, v >> 16);
}
uint32_t get16(ByteView b, size_t at) { return static_cast<uint32_t>(b[at]) | (static_cast<uint32_t>(b[at + 1]) << 8); }
uint32_t get32(ByteView b, size_t at) { return get16(b, at) | (get16(b, at + 2) << 16); }

namespace {
Bytes fixed(uint16_t opcode, size_t len) {
  Bytes b(len, 0);
  put16(b, 0, opcode);
  return b;
}
// variable frame: opcode, u16 total length, body
Bytes variable(uint16_t opcode, size_t head, size_t body) {
  Bytes b(head + body, 0);
  put16(b, 0, opcode);
  put16(b, 2, static_cast<uint32_t>(b.size()));
  return b;
}
void put_str(Bytes& b, size_t at, size_t width, std::string_view s) {
  size_t n = std::min(s.size(), width);
  if (n) std::memcpy(b.data() + at, s.data(), n);
}
Bytes with_text(uint16_t opcode, size_t head, std::string_view text, bool nul) {
  Bytes b = variable(opcode, head, text.size() + (nul ? 1 : 0));
  if (!text.empty()) std::memcpy(b.data() + head, text.data(), text.size());
  return b;
}
std::string read_text(ByteView f, size_t from) {
  std::string s;
  for (size_t i = from; i < f.size() && f[i] != 0; ++i) s += static_cast<char>(f[i]);
  return s;
}
}  // namespace

// ---- client to server -------------------------------------------------------------------------
Bytes cz_enter(uint32_t aid, uint32_t cid, uint32_t login_id1, uint32_t tick, uint8_t sex) {
  Bytes b = fixed(0x0436, 23);
  put32(b, 2, aid);
  put32(b, 6, cid);
  put32(b, 10, login_id1);
  put32(b, 14, tick);
  b[22] = sex;
  return b;
}
Bytes cz_notify_actorinit() { return fixed(0x007d, 2); }
Bytes cz_request_move(uint16_t x, uint16_t y, uint8_t dir) {
  Bytes b = fixed(0x035f, 5);
  auto p = netproto::pack_pos3({x, y, dir});
  std::memcpy(b.data() + 2, p.data(), 3);
  return b;
}
Bytes cz_request_time(uint32_t tick) {
  Bytes b = fixed(0x0360, 6);
  put32(b, 2, tick);
  return b;
}
Bytes cz_request_chat(std::string_view text) { return with_text(0x00f3, 4, text, true); }
Bytes cz_contactnpc(uint32_t gid, uint8_t type) {
  Bytes b = fixed(0x0090, 7);
  put32(b, 2, gid);
  b[6] = type;
  return b;
}
Bytes cz_req_next_script(uint32_t gid) {
  Bytes b = fixed(0x00b9, 6);
  put32(b, 2, gid);
  return b;
}
Bytes cz_choose_menu(uint32_t gid, uint8_t choice) {
  Bytes b = fixed(0x00b8, 7);
  put32(b, 2, gid);
  b[6] = choice;
  return b;
}
Bytes cz_close_dialog(uint32_t gid) {
  Bytes b = fixed(0x0146, 6);
  put32(b, 2, gid);
  return b;
}
Bytes cz_input_editdlg(uint32_t gid, int32_t value) {
  Bytes b = fixed(0x0143, 10);
  put32(b, 2, gid);
  put32(b, 6, static_cast<uint32_t>(value));
  return b;
}
Bytes cz_input_editdlgstr(uint32_t gid, std::string_view text) {
  Bytes b = with_text(0x01d5, 8, text, true);
  put32(b, 4, gid);
  return b;
}
Bytes cz_request_act(uint32_t target, uint8_t action) {
  Bytes b = fixed(0x0437, 7);
  put32(b, 2, target);
  b[6] = action;
  return b;
}
Bytes cz_use_skill(uint16_t level, uint16_t skill_id, uint32_t target) {
  Bytes b = fixed(0x0438, 10);
  put16(b, 2, level);
  put16(b, 4, skill_id);
  put32(b, 6, target);
  return b;
}

// ---- server to client -------------------------------------------------------------------------
Bytes zc_accept_enter(uint32_t tick, uint16_t x, uint16_t y, uint8_t dir) {
  Bytes b = fixed(0x02eb, 13);
  put32(b, 2, tick);
  auto p = netproto::pack_pos3({x, y, dir});
  std::memcpy(b.data() + 6, p.data(), 3);
  b[9] = 5;  // xSize
  b[10] = 5;  // ySize
  put16(b, 11, 0);  // font
  return b;
}
Bytes zc_refuse_enter(uint8_t code) {
  Bytes b = fixed(0x0074, 3);
  b[2] = code;
  return b;
}
Bytes zc_notify_time(uint32_t tick) {
  Bytes b = fixed(0x007f, 6);
  put32(b, 2, tick);
  return b;
}
Bytes zc_notify_playermove(uint32_t tick, uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
  Bytes b = fixed(0x0087, 12);
  put32(b, 2, tick);
  auto p = netproto::pack_pos6({x0, y0, x1, y1, 8, 8});
  std::memcpy(b.data() + 6, p.data(), 6);
  return b;
}
Bytes zc_stopmove(uint32_t gid, uint16_t x, uint16_t y) {
  Bytes b = fixed(0x0088, 10);
  put32(b, 2, gid);
  put16(b, 6, x);
  put16(b, 8, y);
  return b;
}
Bytes zc_notify_vanish(uint32_t gid, uint8_t type) {
  Bytes b = fixed(0x0080, 7);
  put32(b, 2, gid);
  b[6] = type;
  return b;
}

// struct packet_idle_unit / packet_unit_walking of src/map/packets_struct.hpp at PACKETVER 20211103
// (RE flavour: the `shield` word is present). Offsets are computed from the struct, packed.
Bytes zc_notify_standentry(const Actor& a) {
  Bytes b = fixed(0x09ff, 108);
  put16(b, 2, 108);
  b[4] = a.object_type;
  put32(b, 5, a.gid);   // AID
  put32(b, 9, a.gid);   // GID
  put16(b, 13, a.speed);
  put16(b, 23, a.job);
  b[62] = a.sex;
  auto p = netproto::pack_pos3({a.x, a.y, static_cast<uint8_t>(a.dir)});
  std::memcpy(b.data() + 63, p.data(), 3);
  b[66] = 5;
  b[67] = 5;
  put16(b, 69, a.level);
  put32(b, 73, a.max_hp);
  put32(b, 77, a.hp);
  put_str(b, 84, 24, a.name);
  return b;
}
Bytes zc_notify_moveentry(const Actor& a) {
  Bytes b = fixed(0x09fd, 114);
  put16(b, 2, 114);
  b[4] = a.object_type;
  put32(b, 5, a.gid);
  put32(b, 9, a.gid);
  put16(b, 13, a.speed);
  put16(b, 23, a.job);
  put32(b, 37, a.move_start);
  b[66] = a.sex;
  auto p = netproto::pack_pos6({a.x, a.y, a.to_x, a.to_y, 8, 8});
  std::memcpy(b.data() + 67, p.data(), 6);
  b[73] = 5;
  b[74] = 5;
  put16(b, 75, a.level);
  put32(b, 79, a.max_hp);
  put32(b, 83, a.hp);
  put_str(b, 90, 24, a.name);
  return b;
}
Bytes zc_notify_chat(uint32_t gid, std::string_view msg) {
  Bytes b = with_text(0x008d, 8, msg, true);
  put32(b, 4, gid);
  return b;
}
Bytes zc_notify_playerchat(std::string_view msg) { return with_text(0x008e, 4, msg, true); }
Bytes zc_say_dialog(uint32_t gid, std::string_view text) {
  Bytes b = with_text(0x00b4, 8, text, true);
  put32(b, 4, gid);
  return b;
}
Bytes zc_wait_dialog(uint32_t gid) {
  Bytes b = fixed(0x00b5, 6);
  put32(b, 2, gid);
  return b;
}
Bytes zc_close_dialog(uint32_t gid) {
  Bytes b = fixed(0x00b6, 6);
  put32(b, 2, gid);
  return b;
}
Bytes zc_menu_list(uint32_t gid, const std::vector<std::string>& options) {
  std::string joined;
  for (const auto& o : options) joined += o + ":";
  Bytes b = with_text(0x00b7, 8, joined, true);
  put32(b, 4, gid);
  return b;
}
Bytes zc_open_editdlgstr(uint32_t gid) {
  Bytes b = fixed(0x01d4, 6);
  put32(b, 2, gid);
  return b;
}
Bytes zc_notify_act(uint32_t src, uint32_t dst, uint32_t tick, int32_t src_speed, int32_t dmg_speed, int32_t damage,
                    uint16_t div, uint8_t type, int32_t damage2) {
  Bytes b = fixed(0x08c8, 34);
  put32(b, 2, src);
  put32(b, 6, dst);
  put32(b, 10, tick);
  put32(b, 14, static_cast<uint32_t>(src_speed));
  put32(b, 18, static_cast<uint32_t>(dmg_speed));
  put32(b, 22, static_cast<uint32_t>(damage));
  b[26] = 0;
  put16(b, 27, div);
  b[29] = type;
  put32(b, 30, static_cast<uint32_t>(damage2));
  return b;
}

// ---- decoding ----------------------------------------------------------------------------------
std::optional<Packet> decode(ByteView f, Dir d) {
  if (f.size() < 2) return std::nullopt;
  const uint16_t op = static_cast<uint16_t>(get16(f, 0));
  const Def* def = find(op, d);
  if (!def) return std::nullopt;
  if (def->len >= 0 && f.size() != static_cast<size_t>(def->len)) return std::nullopt;
  if (def->len < 0 && (f.size() < 4 || get16(f, 2) != f.size())) return std::nullopt;
  Packet p;
  p.name = def->name;
  p.opcode = op;
  p.raw.assign(f.begin(), f.end());
  auto I = [&](const char* k, int64_t v) { p.fields[k] = v; };
  auto S = [&](const char* k, std::string v) { p.fields[k] = std::move(v); };
  switch (op) {
    case 0x0436:
      I("aid", get32(f, 2)); I("cid", get32(f, 6)); I("login_id1", get32(f, 10)); I("tick", get32(f, 14)); I("sex", f[22]);
      break;
    case 0x035f: {
      auto q = netproto::unpack_pos3({f[2], f[3], f[4]});
      I("x", q.x); I("y", q.y); I("dir", q.dir);
      break;
    }
    case 0x0360: I("tick", get32(f, 2)); break;
    case 0x00f3: S("text", read_text(f, 4)); break;
    case 0x0090: I("gid", get32(f, 2)); I("type", f[6]); break;
    case 0x00b9: I("gid", get32(f, 2)); break;
    case 0x00b8: I("gid", get32(f, 2)); I("choice", f[6]); break;
    case 0x0146: I("gid", get32(f, 2)); break;
    case 0x0143: I("gid", get32(f, 2)); I("value", static_cast<int32_t>(get32(f, 6))); break;
    case 0x01d5: I("gid", get32(f, 4)); S("text", read_text(f, 8)); break;
    case 0x0437: I("gid", get32(f, 2)); I("action", f[6]); break;
    case 0x0438: I("level", get16(f, 2)); I("skill", get16(f, 4)); I("gid", get32(f, 6)); break;
    case 0x02eb: {
      auto q = netproto::unpack_pos3({f[6], f[7], f[8]});
      I("tick", get32(f, 2)); I("x", q.x); I("y", q.y); I("dir", q.dir);
      break;
    }
    case 0x0074: I("code", f[2]); break;
    case 0x007f: I("tick", get32(f, 2)); break;
    case 0x0087: {
      auto q = netproto::unpack_pos6({f[6], f[7], f[8], f[9], f[10], f[11]});
      I("tick", get32(f, 2)); I("x0", q.x0); I("y0", q.y0); I("x1", q.x1); I("y1", q.y1);
      break;
    }
    case 0x0088: I("gid", get32(f, 2)); I("x", get16(f, 6)); I("y", get16(f, 8)); break;
    case 0x0080: I("gid", get32(f, 2)); I("type", f[6]); break;
    case 0x09ff: {
      auto q = netproto::unpack_pos3({f[63], f[64], f[65]});
      I("object_type", f[4]); I("gid", get32(f, 9)); I("speed", get16(f, 13)); I("job", get16(f, 23));
      I("x", q.x); I("y", q.y); I("dir", q.dir); I("level", get16(f, 69)); I("max_hp", get32(f, 73)); I("hp", get32(f, 77));
      std::string n;
      for (size_t i = 84; i < 108 && f[i]; ++i) n += static_cast<char>(f[i]);
      S("actor", n);
      break;
    }
    case 0x09fd: {
      auto q = netproto::unpack_pos6({f[67], f[68], f[69], f[70], f[71], f[72]});
      I("object_type", f[4]); I("gid", get32(f, 9)); I("speed", get16(f, 13)); I("job", get16(f, 23));
      I("move_start", get32(f, 37));
      I("x", q.x0); I("y", q.y0); I("to_x", q.x1); I("to_y", q.y1); I("level", get16(f, 75));
      I("max_hp", get32(f, 79)); I("hp", get32(f, 83));
      std::string n;
      for (size_t i = 90; i < 114 && f[i]; ++i) n += static_cast<char>(f[i]);
      S("actor", n);
      break;
    }
    case 0x008d: I("gid", get32(f, 4)); S("text", read_text(f, 8)); break;
    case 0x008e: S("text", read_text(f, 4)); break;
    case 0x00b4: I("gid", get32(f, 4)); S("text", read_text(f, 8)); break;
    case 0x00b5: case 0x00b6: case 0x01d4: I("gid", get32(f, 2)); break;
    case 0x00b7: {
      I("gid", get32(f, 4));
      std::string joined = read_text(f, 8);
      S("menu", joined);
      int64_t n = 0;
      for (char c : joined) if (c == ':') ++n;
      I("count", n);
      break;
    }
    case 0x08c8:
      I("src", get32(f, 2)); I("dst", get32(f, 6)); I("tick", get32(f, 10)); I("src_speed", static_cast<int32_t>(get32(f, 14)));
      I("dmg_speed", static_cast<int32_t>(get32(f, 18))); I("damage", static_cast<int32_t>(get32(f, 22)));
      I("div", get16(f, 27)); I("type", f[29]); I("damage2", static_cast<int32_t>(get32(f, 30)));
      break;
    default: break;
  }
  return p;
}

Split split_frames(ByteView stream, const netproto::FrameTable& table) {
  Split out;
  size_t pos = 0;
  while (pos < stream.size()) {
    auto r = netproto::peek_frame(stream.subspan(pos), table);
    if (r.status == netproto::FrameStatus::kNeedMore) break;
    if (r.status != netproto::FrameStatus::kComplete) {
      out.error = r.status == netproto::FrameStatus::kUnknownOpcode ? "unknown opcode" : "invalid declared length";
      break;
    }
    out.frames.emplace_back(stream.begin() + static_cast<std::ptrdiff_t>(pos), stream.begin() + static_cast<std::ptrdiff_t>(pos + r.length));
    pos += r.length;
  }
  out.consumed = pos;
  return out;
}

}  // namespace tooling::sim::wire
