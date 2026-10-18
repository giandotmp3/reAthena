#include "gateway/wire.hpp"

#include <cstring>
#include <map>

#include "generated/catalog_client_packets.hpp"

namespace gateway::wire {

using namespace netproto;

namespace {

struct B {  // field layout builder: offsets are computed, so a layout cannot overlap or leave a hole by mistake
  std::vector<FieldSpec> f;
  std::uint16_t off;
  explicit B(std::uint16_t start = 2) : off(start) {}
  B& add(const char* n, FieldType t, std::uint16_t w) {
    FieldSpec s;
    s.name = n; s.offset = off; s.type = t; s.width = w;
    f.push_back(s);
    off = static_cast<std::uint16_t>(off + w);
    return *this;
  }
  B& u8(const char* n) { return add(n, FieldType::U8, 1); }
  B& u16(const char* n) { return add(n, FieldType::U16, 2); }
  B& u32(const char* n) { return add(n, FieldType::U32, 4); }
  B& i32(const char* n) { return add(n, FieldType::I32, 4); }
  B& i64(const char* n) { return add(n, FieldType::I64, 8); }
  B& str(const char* n, std::uint16_t w) { return add(n, FieldType::FixedString, w); }
  B& raw(const char* n, std::uint16_t w) { return add(n, FieldType::Raw, w); }
  B& skip(std::uint16_t w) { off = static_cast<std::uint16_t>(off + w); return *this; }
};

LengthRule fixed(std::uint16_t n) {
  LengthRule r;
  r.kind = LengthKind::Fixed;
  r.fixed_size = n;
  return r;
}
LengthRule var(std::uint16_t header, std::uint16_t stride = 0) {
  LengthRule r;
  r.kind = LengthKind::Variable;
  r.prefix_offset = 2;
  r.header_size = header;
  if (stride) r.repeat = RepeatGroup{header, stride};
  return r;
}

PacketDef mk(const char* name, Direction d, Phase gate, const char* cat, std::uint16_t opcode, LengthRule len,
             std::vector<FieldSpec> fields) {
  PacketDef p;
  p.name = name; p.direction = d; p.category = cat; p.gate = gate;
  PacketVersion v;
  v.applies = *VersionPredicate::parse("RE>=20211103 && RE<20211104");
  v.opcode = opcode; v.length = len; v.fields = std::move(fields); v.fields_known = true;
  p.versions.push_back(std::move(v));
  return p;
}

constexpr auto C2S = Direction::ClientToServer;
constexpr auto S2C = Direction::ServerToClient;

// CHARACTER_INFO row (175 bytes at RE 20211103). Fields the gateway does not own stay zero; the world
// fills them from Character::extra when it wires its own list.
std::vector<FieldSpec> char_info_fields() {
  // offsets taken from the g++ computation of the legacy struct, asserted in tests
  std::vector<FieldSpec> f;
  auto put = [&](const char* n, FieldType t, std::uint16_t off, std::uint16_t w) {
    FieldSpec s; s.name = n; s.offset = off; s.type = t; s.width = w; f.push_back(s);
  };
  put("[].GID", FieldType::U32, 0, 4);
  put("[].money", FieldType::I32, 12, 4);
  put("[].hp", FieldType::I64, 50, 8);
  put("[].maxhp", FieldType::I64, 58, 8);
  put("[].sp", FieldType::I64, 66, 8);
  put("[].maxsp", FieldType::I64, 74, 8);
  put("[].level", FieldType::U16, 92, 2);
  put("[].name", FieldType::FixedString, 108, 24);
  put("[].Str", FieldType::U8, 132, 1);
  put("[].Agi", FieldType::U8, 133, 1);
  put("[].Vit", FieldType::U8, 134, 1);
  put("[].Int", FieldType::U8, 135, 1);
  put("[].Dex", FieldType::U8, 136, 1);
  put("[].Luk", FieldType::U8, 137, 1);
  put("[].CharNum", FieldType::U8, 138, 1);
  put("[].hairColor", FieldType::U8, 139, 1);
  put("[].bIsChangedCharName", FieldType::U16, 140, 2);
  put("[].mapName", FieldType::FixedString, 142, 16);
  put("[].DelRevDate", FieldType::I32, 158, 4);
  put("[].chr_slot_changeCnt", FieldType::I32, 166, 4);
  put("[].chr_name_changeCnt", FieldType::I32, 170, 4);
  put("[].sex", FieldType::U8, 174, 1);
  return f;
}

// Overlay layouts for packets of the REAL generated catalog that have fields_known = false.
const std::map<std::string, std::vector<FieldSpec>>& overlays() {
  static const std::map<std::string, std::vector<FieldSpec>> m = {
      {"CA_LOGIN", B().u32("version").str("username", 24).str("password", 24).u8("clienttype").f},
      {"CZ_ENTER", B().u32("account_id").u32("char_id").u32("auth_code").u32("client_tick").skip(4).u8("sex").f},
  };
  return m;
}

PacketCatalog build_catalog() {
  PacketCatalog c;
  const PacketCatalog real = generated::client_packets_catalog();
  for (const PacketDef& d : real.packets()) c.add(d);

  // ---- login phase, client -> server (src/common/packets.hpp, loginclif.cpp) ----
  c.add(mk("CA_LOGIN2", C2S, Phase::PreSession, "login", 0x01dd, fixed(47),
           B().u32("version").str("username", 24).raw("passwordMD5", 16).u8("clienttype").f));
  c.add(mk("CA_LOGIN3", C2S, Phase::PreSession, "login", 0x01fa, fixed(48),
           B().u32("version").str("username", 24).raw("passwordMD5", 16).u8("clienttype").u8("clientinfo").f));
  c.add(mk("CA_LOGIN4", C2S, Phase::PreSession, "login", 0x027c, fixed(60),
           B().u32("version").str("username", 24).raw("passwordMD5", 16).u8("clienttype").str("mac", 13).f));
  c.add(mk("CA_LOGIN_PCBANG", C2S, Phase::PreSession, "login", 0x0277, fixed(84),
           B().u32("version").str("username", 24).str("password", 24).u8("clienttype").str("ip", 16).str("mac", 13).f));
  c.add(mk("CA_LOGIN_CHANNEL", C2S, Phase::PreSession, "login", 0x02b0, fixed(85),
           B().u32("version").str("username", 24).str("password", 24).u8("clienttype").str("ip", 16).str("mac", 13)
               .u8("is_gravity").f));
  {
    B b(4);
    b.u32("version").u8("clienttype").str("username", 24).str("password", 27).str("mac", 17).str("ip", 15).raw("token", 0);
    c.add(mk("CA_SSO_LOGIN_REQ", C2S, Phase::PreSession, "login", 0x0825, var(92), b.f));
  }
  c.add(mk("CA_REQ_HASH", C2S, Phase::PreSession, "login", 0x01db, fixed(2), {}));
  c.add(mk("CA_EXE_HASHCHECK", C2S, Phase::PreSession, "login", 0x0204, fixed(18), B().raw("hash", 16).f));
  c.add(mk("CA_CONNECT_INFO_CHANGED", C2S, Phase::PreSession, "login", 0x0200, fixed(26), B().str("name", 24).f));
  c.add(mk("CT_AUTH", C2S, Phase::PreSession, "login", 0x0acf, fixed(68), B().raw("unknown", 66).f));

  // ---- login phase, server -> client ----
  c.add(mk("AC_REFUSE_LOGIN", S2C, Phase::PreSession, "login", 0x083e, fixed(26), B().u32("error").str("unblock_time", 20).f));
  c.add(mk("SC_NOTIFY_BAN", S2C, Phase::PreSession, "login", 0x0081, fixed(3), B().u8("result").f));
  c.add(mk("AC_ACK_HASH", S2C, Phase::PreSession, "login", 0x01dc, var(4), B(4).raw("salt", 0).f));
  c.add(mk("TC_RESULT", S2C, Phase::PreSession, "login", 0x0ae3, fixed(34),
           B(4).u32("type").str("unknown1", 20).str("unknown2", 6).f));
  {
    // header: type, length, login_id1, AID, login_id2, last_ip, last_login[26], sex, token[17] = 64; rows of 160
    B h(4);
    h.u32("login_id1").u32("AID").u32("login_id2").u32("last_ip").str("last_login", 26).u8("sex").str("token", 17);
    std::vector<FieldSpec> f = h.f;
    B r(0);
    r.u32("ip").u16("port").str("name", 20).u16("users").u16("type").u16("new").raw("unknown", 128);
    for (FieldSpec s : r.f) { s.name = "[]." + s.name; f.push_back(s); }
    c.add(mk("AC_ACCEPT_LOGIN", S2C, Phase::PreSession, "login", 0x0ac4, var(64, 160), f));
  }

  // ---- char phase, client -> server ----
  c.add(mk("CH_ENTER", C2S, Phase::PreSession, "char", 0x0065, fixed(17),
           B().u32("account_id").u32("login_id1").u32("login_id2").u16("unknown").u8("sex").f));
  c.add(mk("PING", C2S, Phase::PreMapLoad, "char", 0x0187, fixed(6), B().u32("AID").f));
  c.add(mk("CH_CHARLIST_REQ", C2S, Phase::PreMapLoad, "char", 0x09a1, fixed(2), {}));
  c.add(mk("CH_SELECT_CHAR", C2S, Phase::PreMapLoad, "char", 0x0066, fixed(3), B().u8("slot").f));
  c.add(mk("CH_MAKE_CHAR", C2S, Phase::PreMapLoad, "char", 0x0a39, fixed(36),
           B().str("name", 24).u8("slot").u16("hair_color").u16("hair_style").u32("job").u8("sex").f));
  c.add(mk("CH_DELETE_CHAR", C2S, Phase::PreMapLoad, "char", 0x01fb, fixed(56), B().u32("CID").str("key", 50).f));
  c.add(mk("CH_DELETE_CHAR3_RESERVED", C2S, Phase::PreMapLoad, "char", 0x0827, fixed(6), B().u32("CID").f));
  c.add(mk("CH_DELETE_CHAR3", C2S, Phase::PreMapLoad, "char", 0x0829, fixed(12), B().u32("CID").raw("birthdate", 6).f));
  c.add(mk("CH_DELETE_CHAR3_CANCEL", C2S, Phase::PreMapLoad, "char", 0x082b, fixed(6), B().u32("CID").f));
  c.add(mk("CH_SECOND_PASSWD_ACK", C2S, Phase::PreMapLoad, "char", 0x08b8, fixed(10), B().u32("AID").raw("pin", 4).f));
  c.add(mk("CH_MAKE_SECOND_PASSWD", C2S, Phase::PreMapLoad, "char", 0x08ba, fixed(10), B().u32("AID").raw("pin", 4).f));
  c.add(mk("CH_EDIT_SECOND_PASSWD", C2S, Phase::PreMapLoad, "char", 0x08be, fixed(14),
           B().u32("AID").raw("old_pin", 4).raw("new_pin", 4).f));
  c.add(mk("CH_AVAILABLE_SECOND_PASSWD", C2S, Phase::PreMapLoad, "char", 0x08c5, fixed(6), B().u32("AID").f));
  c.add(mk("CH_REQ_IS_VALID_CHARNAME", C2S, Phase::PreMapLoad, "char", 0x028d, fixed(34),
           B().u32("AID").u32("CID").str("new_name", 24).f));
  c.add(mk("CH_REQ_CHANGE_CHARNAME", C2S, Phase::PreMapLoad, "char", 0x08fc, fixed(30), B().u32("CID").str("new_name", 24).f));
  c.add(mk("CH_REQ_CHANGE_CHARACTER_SLOT", C2S, Phase::PreMapLoad, "char", 0x08d4, fixed(8),
           B().u16("slot_before").u16("slot_after").u16("remaining").f));

  // ---- char phase, server -> client ----
  c.add(mk("HC_REFUSE_ENTER", S2C, Phase::PreSession, "char", 0x006c, fixed(3), B().u8("error").f));
  {
    std::vector<FieldSpec> f = B(4).u8("total").u8("premium_start").u8("premium_end").str("extension", 20).f;
    for (FieldSpec s : char_info_fields()) f.push_back(s);
    c.add(mk("HC_ACCEPT_ENTER", S2C, Phase::PreSession, "char", 0x006b, var(27, kCharInfoSize), f));
  }
  c.add(mk("HC_ACK_CHARINFO_PER_PAGE", S2C, Phase::PreSession, "char", 0x0b72, var(4, kCharInfoSize), char_info_fields()));
  c.add(mk("HC_ACCEPT_ENTER2", S2C, Phase::PreSession, "char", 0x082d, var(29),
           B(4).u8("normal").u8("premium").u8("billing").u8("producible").u8("total").str("extension", 20).f));
  c.add(mk("HC_CHARLIST_NOTIFY", S2C, Phase::PreSession, "char", 0x09a0, fixed(6), B().u32("total").f));
  {
    B r(0);
    r.u32("CID").str("unblock_time", 20);
    std::vector<FieldSpec> f;
    for (FieldSpec s : r.f) { s.name = "[]." + s.name; f.push_back(s); }
    c.add(mk("HC_BLOCK_CHARACTER", S2C, Phase::PreSession, "char", 0x020d, var(4, 24), f));
  }
  {
    std::vector<FieldSpec> f = char_info_fields();
    for (FieldSpec& s : f) { s.name = s.name.substr(3); s.offset = static_cast<std::uint16_t>(s.offset + 2); }
    c.add(mk("HC_ACCEPT_MAKECHAR", S2C, Phase::PreMapLoad, "char", 0x0b6f, fixed(static_cast<std::uint16_t>(2 + kCharInfoSize)), f));
  }
  c.add(mk("HC_REFUSE_MAKECHAR", S2C, Phase::PreMapLoad, "char", 0x006e, fixed(3), B().u8("error").f));
  c.add(mk("HC_ACCEPT_DELETECHAR", S2C, Phase::PreMapLoad, "char", 0x006f, fixed(2), {}));
  c.add(mk("HC_REFUSE_DELETECHAR", S2C, Phase::PreMapLoad, "char", 0x0070, fixed(3), B().u8("error").f));
  c.add(mk("HC_DELETE_CHAR3_RESERVED", S2C, Phase::PreMapLoad, "char", 0x0828, fixed(14),
           B().u32("CID").i32("result").u32("date").f));
  c.add(mk("HC_DELETE_CHAR3", S2C, Phase::PreMapLoad, "char", 0x082a, fixed(10), B().u32("CID").i32("result").f));
  c.add(mk("HC_DELETE_CHAR3_CANCEL", S2C, Phase::PreMapLoad, "char", 0x082c, fixed(10), B().u32("CID").i32("result").f));
  c.add(mk("HC_SECOND_PASSWD_LOGIN", S2C, Phase::PreMapLoad, "char", 0x08b9, fixed(12),
           B().u32("seed").u32("AID").u16("result").f));
  c.add(mk("HC_NOTIFY_ZONESVR", S2C, Phase::PreMapLoad, "char", 0x0ac5, fixed(156),
           B().u32("CID").str("mapname", 16).u32("ip").u16("port").str("domain", 128).f));
  c.add(mk("HC_ACK_IS_VALID_CHARNAME", S2C, Phase::PreMapLoad, "char", 0x028e, fixed(4), B().u16("result").f));
  c.add(mk("HC_ACK_CHANGE_CHARNAME", S2C, Phase::PreMapLoad, "char", 0x08fd, fixed(6), B().u32("result").f));
  c.add(mk("HC_ACK_CHANGE_CHARACTER_SLOT", S2C, Phase::PreMapLoad, "char", 0x0b70, var(8),
           B(4).u16("reason").u16("moves").f));

  // ---- world connect: refusal of the map-side connect packet (clif_parse_WantToConnection) ----
  c.add(mk("ZC_REFUSE_ENTER", S2C, Phase::PreSession, "connect", 0x006a, fixed(3), B().u8("error").f));
  return c;
}

std::size_t width_of(const FieldSpec& f) {
  switch (f.type) {
    case FieldType::U8: case FieldType::I8: return 1;
    case FieldType::U16: case FieldType::I16: return 2;
    case FieldType::U32: case FieldType::I32: return 4;
    case FieldType::I64: return 8;
    case FieldType::FixedString: case FieldType::Raw: return f.width;
    default: throw WireError("unsupported field type");
  }
}

bool is_row(const FieldSpec& f) { return f.name.rfind("[].", 0) == 0; }

std::int64_t read_int(const std::uint8_t* p, std::size_t n, bool is_signed) {
  std::uint64_t v = 0;
  for (std::size_t i = 0; i < n; ++i) v |= static_cast<std::uint64_t>(p[i]) << (8 * i);
  if (is_signed && n < 8 && (v >> (8 * n - 1)) & 1) v |= ~std::uint64_t{0} << (8 * n);
  return static_cast<std::int64_t>(v);
}
void write_int(std::uint8_t* p, std::size_t n, std::int64_t val) {
  const std::uint64_t v = static_cast<std::uint64_t>(val);
  for (std::size_t i = 0; i < n; ++i) p[i] = static_cast<std::uint8_t>((v >> (8 * i)) & 0xff);
}
bool is_signed(FieldType t) { return t == FieldType::I8 || t == FieldType::I16 || t == FieldType::I32 || t == FieldType::I64; }

void decode_field(const FieldSpec& f, ByteView buf, std::size_t base, std::size_t limit, const std::string& key,
                  std::map<std::string, FieldValue>& out) {
  const std::size_t off = base + f.offset;
  if (f.type == FieldType::Raw && f.width == 0) {  // rest of the packet
    if (off > limit) throw WireError("tail offset beyond packet");
    out[key] = Bytes(buf.begin() + static_cast<std::ptrdiff_t>(off), buf.begin() + static_cast<std::ptrdiff_t>(limit));
    return;
  }
  const std::size_t w = width_of(f);
  if (off + w > limit) throw WireError("field '" + f.name + "' beyond packet");
  const std::uint8_t* p = buf.data() + off;
  switch (f.type) {
    case FieldType::FixedString: {
      std::size_t n = 0;
      while (n < w && p[n] != 0) ++n;
      out[key] = std::string(reinterpret_cast<const char*>(p), n);
      break;
    }
    case FieldType::Raw: out[key] = Bytes(p, p + w); break;
    default: out[key] = read_int(p, w, is_signed(f.type)); break;
  }
}

void encode_field(const FieldSpec& f, Bytes& buf, std::size_t base, const std::map<std::string, FieldValue>& vals,
                  const std::string& key) {
  const auto it = vals.find(key);
  if (it == vals.end()) return;  // missing = zero
  const std::size_t off = base + f.offset;
  if (f.type == FieldType::Raw && f.width == 0) return;  // tails are appended by the caller
  const std::size_t w = width_of(f);
  if (off + w > buf.size()) throw WireError("encode: field '" + f.name + "' beyond packet");
  std::uint8_t* p = buf.data() + off;
  switch (f.type) {
    case FieldType::FixedString: {
      const auto* s = std::get_if<std::string>(&it->second);
      if (!s) throw WireError("encode: '" + f.name + "' needs a string");
      const std::size_t n = std::min(s->size(), w == 0 ? 0 : w - 1);  // always NUL terminated
      std::memcpy(p, s->data(), n);
      break;
    }
    case FieldType::Raw: {
      const auto* b = std::get_if<Bytes>(&it->second);
      if (!b) throw WireError("encode: '" + f.name + "' needs bytes");
      std::memcpy(p, b->data(), std::min(b->size(), w));
      break;
    }
    default: {
      const auto* v = std::get_if<std::int64_t>(&it->second);
      if (!v) throw WireError("encode: '" + f.name + "' needs an integer");
      write_int(p, w, *v);
    }
  }
}

}  // namespace

PacketCodec::PacketCodec(netproto::ClientVersion v) : version_(v) {
  catalog_ = std::make_shared<const PacketCatalog>(build_catalog());
  set_ = std::make_shared<const CodecSet>(CodecSet::build(*catalog_, v));
  resolved_ = catalog_->resolve(v);
}

netproto::FrameResult PacketCodec::peek(ByteView buf, const FrameLimits& lim) const {
  return peek_frame(buf, set_->frames(), lim);
}

const ResolvedPacket* PacketCodec::resolved(const std::string& name) const {
  for (const auto& r : resolved_) if (r.def->name == name) return &r;
  return nullptr;
}

bool PacketCodec::has_packet(const std::string& name) const { return resolved(name) != nullptr; }

const std::vector<FieldSpec>& PacketCodec::fields_of(const std::string& name) const {
  const ResolvedPacket* r = resolved(name);
  if (!r) throw WireError("unknown packet " + name);
  if (r->version->fields_known) return r->version->fields;
  const auto it = overlays().find(name);
  static const std::vector<FieldSpec> kOpaque;   // layout not captured yet (netproto: fields unknown): header only
  return it == overlays().end() ? kOpaque : it->second;
}

std::optional<Phase> PacketCodec::gate_of(std::uint16_t opcode) const {
  const auto it = set_->frames().find(opcode);
  if (it == set_->frames().end()) return std::nullopt;
  return it->second.gate;
}

DecodedPacket PacketCodec::decode(ByteView frame) const {
  const FrameResult fr = peek(frame);
  if (fr.status != FrameStatus::kComplete || fr.length != frame.size()) throw WireError("not exactly one complete frame");
  const auto name = set_->name_of(fr.opcode);
  if (!name) throw WireError("unknown opcode");
  return decode_with(*resolved(*name), frame);
}

DecodedPacket PacketCodec::decode_s2c(ByteView frame) const {
  if (frame.size() < 2) throw WireError("short frame");
  const std::uint16_t op = static_cast<std::uint16_t>(frame[0] | (frame[1] << 8));
  for (const auto& r : resolved_) {
    if (r.def->direction != Direction::ServerToClient || r.version->opcode != op) continue;
    const LengthRule& lr = r.version->length;
    std::size_t want = lr.fixed_size;
    if (lr.kind == LengthKind::Variable) {
      if (frame.size() < static_cast<std::size_t>(lr.prefix_offset) + 2) throw WireError("short frame");
      want = static_cast<std::size_t>(frame[lr.prefix_offset] | (frame[lr.prefix_offset + 1] << 8));
      if (want < lr.header_size) throw WireError("declared length below header");
    }
    if (want != frame.size()) throw WireError("length does not match the frame");
    return decode_with(r, frame);
  }
  throw WireError("unknown server opcode");
}

DecodedPacket PacketCodec::decode_with(const ResolvedPacket& r, ByteView frame) const {
  DecodedPacket p;
  p.name = r.def->name;
  const std::vector<FieldSpec> fields = fields_of(p.name);
  for (const FieldSpec& f : fields) {
    if (is_row(f)) continue;
    decode_field(f, frame, 0, frame.size(), f.name, p.fields);
  }
  if (r.version->length.repeat) {
    const RepeatGroup& g = *r.version->length.repeat;
    const std::size_t body = frame.size() - g.offset;
    if (g.stride == 0 || body % g.stride != 0) throw WireError("repeat group does not tile the packet");
    for (std::size_t i = 0; i < body / g.stride; ++i) {
      std::map<std::string, FieldValue> row;
      for (const FieldSpec& f : fields) {
        if (!is_row(f)) continue;
        decode_field(f, frame, g.offset + i * g.stride, g.offset + (i + 1) * g.stride, f.name.substr(3), row);
      }
      p.repeats.push_back(std::move(row));
    }
  }
  return p;
}

Bytes PacketCodec::encode(const DecodedPacket& p) const {
  const ResolvedPacket* r = resolved(p.name);
  if (!r) throw WireError("unknown packet " + p.name);
  const LengthRule& lr = r->version->length;
  const auto& fields = fields_of(p.name);
  std::size_t size = lr.kind == LengthKind::Fixed ? lr.fixed_size : lr.header_size;
  // variable tail: a Raw/width-0 field extends the packet after the header
  const FieldSpec* tail = nullptr;
  for (const FieldSpec& f : fields) if (f.type == FieldType::Raw && f.width == 0 && !is_row(f)) tail = &f;
  Bytes tail_bytes;
  if (tail) {
    if (auto it = p.fields.find(tail->name); it != p.fields.end())
      if (const auto* b = std::get_if<Bytes>(&it->second)) tail_bytes = *b;
    size = std::max<std::size_t>(size, tail->offset) + tail_bytes.size();
  }
  if (lr.repeat) size = lr.repeat->offset + p.repeats.size() * lr.repeat->stride + (tail ? tail_bytes.size() : 0);
  if (size > 32768) throw WireError("encode: packet too large");
  Bytes out(size, 0);
  write_int(out.data(), 2, r->version->opcode);
  if (lr.kind == LengthKind::Variable) write_int(out.data() + lr.prefix_offset, 2, static_cast<std::int64_t>(size));
  for (const FieldSpec& f : fields) if (!is_row(f)) encode_field(f, out, 0, p.fields, f.name);
  if (tail) std::memcpy(out.data() + tail->offset, tail_bytes.data(), tail_bytes.size());
  if (lr.repeat) {
    for (std::size_t i = 0; i < p.repeats.size(); ++i)
      for (const FieldSpec& f : fields)
        if (is_row(f)) encode_field(f, out, lr.repeat->offset + i * lr.repeat->stride, p.repeats[i], f.name.substr(3));
  }
  return out;
}

// ---- helpers --------------------------------------------------------------------------------------
std::int64_t get_int(const DecodedPacket& p, const std::string& name) {
  const auto it = p.fields.find(name);
  if (it == p.fields.end()) throw WireError("missing field " + name);
  const auto* v = std::get_if<std::int64_t>(&it->second);
  if (!v) throw WireError("field " + name + " is not an integer");
  return *v;
}
std::string get_str(const DecodedPacket& p, const std::string& name) {
  const auto it = p.fields.find(name);
  if (it == p.fields.end()) throw WireError("missing field " + name);
  const auto* v = std::get_if<std::string>(&it->second);
  if (!v) throw WireError("field " + name + " is not a string");
  return *v;
}
Bytes get_bytes(const DecodedPacket& p, const std::string& name) {
  const auto it = p.fields.find(name);
  if (it == p.fields.end()) throw WireError("missing field " + name);
  const auto* v = std::get_if<Bytes>(&it->second);
  if (!v) throw WireError("field " + name + " is not bytes");
  return *v;
}
void set_int(DecodedPacket& p, const std::string& n, std::int64_t v) { p.fields[n] = v; }
void set_str(DecodedPacket& p, const std::string& n, const std::string& v) { p.fields[n] = v; }
void set_bytes(DecodedPacket& p, const std::string& n, const Bytes& v) { p.fields[n] = v; }
DecodedPacket make(const std::string& name) {
  DecodedPacket p;
  p.name = name;
  return p;
}

std::string hex_of(const Bytes& b) {
  static const char hex[] = "0123456789abcdef";
  std::string s;
  for (std::uint8_t c : b) { s.push_back(hex[c >> 4]); s.push_back(hex[c & 15]); }
  return s;
}

namespace {
std::array<int, 10> pin_tab(std::uint32_t seed) {
  std::array<int, 10> tab = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
  for (int i = 1; i < 10; ++i) {
    seed = 0x881234u + seed * 0x3498u;
    const int pos = static_cast<int>(seed % static_cast<std::uint32_t>(i + 1));
    if (i != pos) std::swap(tab[static_cast<std::size_t>(i)], tab[static_cast<std::size_t>(pos)]);
  }
  return tab;
}
}  // namespace

std::string pin_untransport(std::uint32_t seed, const std::string& wire) {
  if (wire.size() != 4) return {};
  const auto tab = pin_tab(seed);
  std::string out;
  for (char c : wire) {
    if (c < '0' || c > '9') return {};
    out.push_back(static_cast<char>('0' + tab[static_cast<std::size_t>(c - '0')]));
  }
  return out;
}

std::string pin_transport(std::uint32_t seed, const std::string& real) {
  if (real.size() != 4) return {};
  const auto tab = pin_tab(seed);
  std::array<int, 10> inv{};
  for (int i = 0; i < 10; ++i) inv[static_cast<std::size_t>(tab[static_cast<std::size_t>(i)])] = i;
  std::string out;
  for (char c : real) {
    if (c < '0' || c > '9') return {};
    out.push_back(static_cast<char>('0' + inv[static_cast<std::size_t>(c - '0')]));
  }
  return out;
}

int sex_to_wire(Sex s) {
  switch (s) {
    case Sex::Female: return 0;
    case Sex::Male: return 1;
    case Sex::Server: return 2;
  }
  return 1;
}

}  // namespace gateway::wire
