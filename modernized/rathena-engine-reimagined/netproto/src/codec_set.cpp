#include "netproto/codec_set.hpp"

#include <algorithm>
#include <cstring>
#include <limits>

#include "codec_internal.hpp"

namespace netproto {

const char* to_string(CodecErrc e) noexcept {
  switch (e) {
    case CodecErrc::kOk: return "ok";
    case CodecErrc::kShortFrame: return "short frame";
    case CodecErrc::kUnknownOpcode: return "unknown opcode";
    case CodecErrc::kBadLength: return "bad declared length";
    case CodecErrc::kLengthMismatch: return "length does not match the buffer";
    case CodecErrc::kFieldOutOfBounds: return "field beyond the frame";
    case CodecErrc::kRepeatMismatch: return "repeat group does not tile the frame";
    case CodecErrc::kUnknownPacket: return "unknown packet";
    case CodecErrc::kUnknownField: return "unknown field";
    case CodecErrc::kBadValue: return "bad value";
    case CodecErrc::kTooLarge: return "frame too large";
    case CodecErrc::kWrongDirection: return "wrong direction";
  }
  return "?";
}

CodecError::CodecError(CodecErrc c, const std::string& what)
    : std::runtime_error(std::string(to_string(c)) + ": " + what), code(c) {}

namespace {

[[noreturn]] void spec_fail(const std::string& packet, const std::string& what) {
  throw SpecError("layout of " + packet + ": " + what);
}

bool is_tail(const FieldSpec& f) {
  return (f.type == FieldType::Raw || f.type == FieldType::FixedString) && f.width == 0;
}

// Validates one field against the frame it lives in and returns its extent in bytes.
std::size_t check_field(const std::string& pkt, const FieldSpec& f) {
  const std::size_t n = detail::field_bytes(f);
  if (f.array_len == 0) spec_fail(pkt, "field '" + f.name + "' has array_len 0");
  if (f.array_len > 1 && (f.type == FieldType::FixedString || f.type == FieldType::Raw || f.type == FieldType::Pos3 ||
                          f.type == FieldType::Pos6))
    spec_fail(pkt, "field '" + f.name + "': array_len > 1 is for integer types only");
  if (f.type == FieldType::F32 && f.array_len != 1) spec_fail(pkt, "field '" + f.name + "': f32 arrays are not supported");
  if (f.bit_width) {
    if (detail::is_float(f.type) || !detail::is_int(f.type) || f.array_len != 1)
      spec_fail(pkt, "field '" + f.name + "': bit fields need a scalar integer storage type");
    if (static_cast<unsigned>(f.bit_shift) + f.bit_width > 8 * detail::elem_size(f))
      spec_fail(pkt, "field '" + f.name + "': bits run past the storage integer");
  }
  return n;
}

}  // namespace

CodecSet CodecSet::build(const PacketCatalog& catalog, const ClientVersion& v) {
  CodecSet s;
  s.version_ = v;
  const auto resolved = catalog.resolve(v);
  s.frames_ = make_frame_table(resolved);
  for (const auto& rp : resolved) {
    s.by_name_.emplace(std::make_pair(rp.def->direction, rp.def->name), rp.version->opcode);
    s.known_.insert(rp.def->name);
    const std::string& pkt = rp.def->name;
    if (s.by_name_idx_.count(pkt)) throw SpecError("duplicate packet name " + pkt + " on " + v.to_string());

    Layout l;
    l.name = pkt;
    l.dir = rp.def->direction;
    l.gate = rp.def->gate;
    l.opcode = rp.version->opcode;
    l.length = rp.version->length;
    l.fields_known = rp.version->fields_known;
    const bool fixed = l.length.kind == LengthKind::Fixed;
    const std::size_t min_len = fixed ? l.length.fixed_size : std::max<std::size_t>(l.length.header_size, 4);
    if (!fixed && l.length.header_size < static_cast<std::size_t>(l.length.prefix_offset) + 2)
      spec_fail(pkt, "header smaller than the length word");
    if (fixed && l.length.fixed_size < 2) spec_fail(pkt, "fixed length below the 2-byte opcode");
    if (l.length.repeat) {
      if (fixed) spec_fail(pkt, "a repeat group needs a variable-length packet");
      if (l.length.repeat->stride == 0) spec_fail(pkt, "repeat stride 0");
      if (l.length.repeat->offset != l.length.header_size) spec_fail(pkt, "repeat group must start at the header size");
    }
    if (l.fields_known) {
      std::set<std::string> head_names, row_names;
      for (const FieldSpec& f : rp.version->fields) {
        if (!f.present_when.matches(v)) continue;
        FieldSpec g = f;
        const std::size_t n = check_field(pkt, g);
        if (is_row_field(g)) {
          g.name = field_key(g);
          g.row = true;
          if (!l.length.repeat) spec_fail(pkt, "row field '" + g.name + "' without a repeat group");
          if (static_cast<std::size_t>(g.offset) + n > l.length.repeat->stride)
            spec_fail(pkt, "row field '" + g.name + "' runs past the row stride");
          if (!row_names.insert(g.name).second) spec_fail(pkt, "duplicate row field '" + g.name + "'");
          l.row.push_back(std::move(g));
        } else if (is_tail(g)) {
          if (fixed) spec_fail(pkt, "tail field '" + g.name + "' in a fixed-length packet");
          if (l.length.repeat) spec_fail(pkt, "tail field together with a repeat group");
          if (l.tail) spec_fail(pkt, "two tail fields");
          if (g.offset != l.length.header_size) spec_fail(pkt, "tail field '" + g.name + "' must start at the header size");
          if (!head_names.insert(g.name).second) spec_fail(pkt, "duplicate field '" + g.name + "'");
          l.tail = std::move(g);
        } else {
          const std::size_t first_free = fixed ? 2 : static_cast<std::size_t>(l.length.prefix_offset) + 2;
          if (g.offset < first_free) spec_fail(pkt, "field '" + g.name + "' overlaps the opcode / length word");
          if (static_cast<std::size_t>(g.offset) + n > min_len)
            spec_fail(pkt, "field '" + g.name + "' runs past the " + (fixed ? "fixed length" : "header"));
          if (!head_names.insert(g.name).second) spec_fail(pkt, "duplicate field '" + g.name + "'");
          l.head.push_back(std::move(g));
        }
      }
    }
    const std::size_t idx = s.layouts_.size();
    switch (l.dir) {
      case Direction::ClientToServer: s.c2s_.emplace(l.opcode, idx); break;
      case Direction::ServerToClient: s.s2c_.emplace(l.opcode, idx); break;
      case Direction::Inter: s.inter_.emplace(l.opcode, idx); break;
      case Direction::Any: s.any_.emplace(l.opcode, idx); break;
    }
    s.by_name_idx_.emplace(pkt, idx);
    s.layouts_.push_back(std::move(l));
  }
  return s;
}

FrameTable CodecSet::server_frames() const {
  FrameTable t;
  for (const Layout& l : layouts_) {
    if (l.dir != Direction::ServerToClient && l.dir != Direction::Any) continue;
    t.emplace(l.opcode, FrameEntry{l.name, l.length, l.gate});
  }
  return t;
}

std::optional<std::string> CodecSet::name_of(std::uint16_t opcode) const {
  const auto it = frames_.find(opcode);
  if (it == frames_.end()) return std::nullopt;
  return it->second.name;
}

std::optional<std::uint16_t> CodecSet::opcode_of(const std::string& name, Direction d) const {
  const auto it = by_name_.find({d, name});
  if (it == by_name_.end()) return std::nullopt;
  return it->second;
}

const CodecSet::Layout* CodecSet::find_name(const std::string& name) const {
  const auto it = by_name_idx_.find(name);
  return it == by_name_idx_.end() ? nullptr : &layouts_[it->second];
}

std::optional<std::uint16_t> CodecSet::opcode_of(const std::string& name) const {
  const Layout* l = find_name(name);
  if (!l) return std::nullopt;
  return l->opcode;
}
std::optional<Direction> CodecSet::direction_of(const std::string& name) const {
  const Layout* l = find_name(name);
  if (!l) return std::nullopt;
  return l->dir;
}
std::optional<Phase> CodecSet::gate_of(const std::string& name) const {
  const Layout* l = find_name(name);
  if (!l) return std::nullopt;
  return l->gate;
}
bool CodecSet::fields_known(const std::string& name) const {
  const Layout* l = find_name(name);
  return l && l->fields_known;
}

const CodecSet::Layout* CodecSet::find_op(std::uint16_t opcode, Direction d) const {
  auto look = [&](const std::unordered_map<std::uint16_t, std::size_t>& m) -> const Layout* {
    const auto it = m.find(opcode);
    return it == m.end() ? nullptr : &layouts_[it->second];
  };
  switch (d) {
    case Direction::ClientToServer: return look(c2s_);
    case Direction::ServerToClient:
      if (const Layout* l = look(s2c_)) return l;
      return look(any_);
    case Direction::Inter: return look(inter_);
    case Direction::Any: return look(any_);
  }
  return nullptr;
}

std::shared_ptr<const CodecSet> CodecRegistry::negotiate(const ClientVersion& v) {
  if (!supported_.count(v)) return nullptr;
  if (auto it = cache_.find(v); it != cache_.end()) return it->second;
  auto set = std::make_shared<const CodecSet>(CodecSet::build(catalog_, v));
  cache_.emplace(v, set);
  return set;
}


// ---- field codecs (decode / encode over FieldSpec) -------------------------------------------------


namespace {

constexpr std::size_t kMaxFrame = 32768;  // FrameLimits::max_variable

std::uint64_t rd(const std::uint8_t* p, std::size_t n) {
  std::uint64_t v = 0;
  for (std::size_t i = 0; i < n; ++i) v |= static_cast<std::uint64_t>(p[i]) << (8 * i);
  return v;
}
void wr(std::uint8_t* p, std::size_t n, std::uint64_t v) {
  for (std::size_t i = 0; i < n; ++i) p[i] = static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF);
}
std::int64_t sign_extend(std::uint64_t v, std::size_t n) {
  if (n >= 8) return static_cast<std::int64_t>(v);
  const std::uint64_t sign = std::uint64_t{1} << (8 * n - 1);
  return static_cast<std::int64_t>((v ^ sign) - sign);
}

std::int64_t read_scalar(const FieldSpec& f, const std::uint8_t* p, std::size_t n) {
  std::uint64_t v = rd(p, n);
  if (f.bit_width) {
    const std::uint64_t mask = f.bit_width >= 64 ? ~std::uint64_t{0} : ((std::uint64_t{1} << f.bit_width) - 1);
    return static_cast<std::int64_t>((v >> f.bit_shift) & mask);
  }
  return detail::is_signed(f.type) ? sign_extend(v, n) : static_cast<std::int64_t>(v);
}

// Decodes one field at `base + f.offset` of `buf` (already bounds-checked by the caller's contract,
// re-checked here anyway: defence in depth costs one compare).
bool decode_field(const FieldSpec& f, ByteView buf, std::size_t base, std::size_t limit, const std::string& key,
                  std::map<std::string, FieldValue>& out) {
  const std::size_t off = base + f.offset;
  if (off > limit) return false;
  const bool tail = (f.type == FieldType::Raw || f.type == FieldType::FixedString) && f.width == 0;
  const std::size_t n = tail ? limit - off : detail::field_bytes(f);
  if (n > limit - off) return false;
  const std::uint8_t* p = buf.data() + off;
  switch (f.type) {
    case FieldType::FixedString: {
      const std::size_t len = static_cast<std::size_t>(std::find(p, p + n, std::uint8_t{0}) - p);
      out[key] = std::string(reinterpret_cast<const char*>(p), len);
      return true;
    }
    case FieldType::Raw:
      out[key] = Bytes(p, p + n);
      return true;
    case FieldType::Pos3: {
      std::array<std::uint8_t, 3> a{p[0], p[1], p[2]};
      out[key] = unpack_pos3(a);
      return true;
    }
    case FieldType::Pos6: {
      std::array<std::uint8_t, 6> a{p[0], p[1], p[2], p[3], p[4], p[5]};
      out[key] = unpack_pos6(a);
      return true;
    }
    case FieldType::F32: {
      if (f.array_len != 1) return false;
      const std::uint32_t bits = static_cast<std::uint32_t>(rd(p, 4));
      float fl;
      std::memcpy(&fl, &bits, sizeof fl);
      out[key] = static_cast<double>(fl);
      return true;
    }
    default: break;
  }
  const std::size_t es = detail::elem_size(f);
  if (f.array_len == 1) {
    out[key] = read_scalar(f, p, es);
  } else if (es == 1) {
    out[key] = Bytes(p, p + f.array_len);
  } else {
    std::vector<std::int64_t> v;
    v.reserve(f.array_len);
    for (std::size_t i = 0; i < f.array_len; ++i) v.push_back(read_scalar(f, p + i * es, es));
    out[key] = std::move(v);
  }
  return true;
}

// ---- encode ----------------------------------------------------------------------------------------
[[noreturn]] void bad_value(const std::string& pkt, const std::string& field, const std::string& why) {
  throw CodecError(CodecErrc::kBadValue, pkt + "." + field + ": " + why);
}

void check_range(const std::string& pkt, const FieldSpec& f, std::int64_t v, std::size_t n, unsigned bits) {
  if (bits == 0) bits = static_cast<unsigned>(8 * n);
  if (bits >= 64) return;  // every int64 is representable as 64 bits (either signedness)
  const std::int64_t hi = static_cast<std::int64_t>((std::uint64_t{1} << bits) - 1);
  // Accept the union of the signed and the unsigned range of the width: -2^(b-1) .. 2^b - 1. A field
  // typed i16 therefore takes 40000 as the bit pattern it is, and a u16 takes -1; the decoder always
  // returns the canonical value of the field's own signedness.
  const std::int64_t lo = f.bit_width ? 0 : -(std::int64_t{1} << (bits - 1));
  if (v < lo || v > hi) bad_value(pkt, f.name, "value " + std::to_string(v) + " does not fit " + std::to_string(bits) + " bits");
}

const FieldValue* find_value(const std::map<std::string, FieldValue>& m, const std::string& key) {
  const auto it = m.find(key);
  return it == m.end() ? nullptr : &it->second;
}

void write_tail_or_field(const std::string& pkt, const FieldSpec& f, std::uint8_t* base_ptr, std::size_t frame_size,
                         std::size_t base, const std::map<std::string, FieldValue>& vals, const std::string& key) {
  const FieldValue* fv = find_value(vals, key);
  if (!fv) return;  // missing = zero
  const std::size_t off = base + f.offset;
  const std::size_t n = detail::field_bytes(f);
  if (off + n > frame_size) throw CodecError(CodecErrc::kFieldOutOfBounds, pkt + "." + f.name);
  std::uint8_t* p = base_ptr + off;
  const std::size_t es = detail::elem_size(f);
  switch (f.type) {
    case FieldType::FixedString: {
      const auto* s = std::get_if<std::string>(fv);
      if (!s) bad_value(pkt, f.name, "needs a string");
      if (s->size() + 1 > n) bad_value(pkt, f.name, "string of " + std::to_string(s->size()) + " bytes does not fit " + std::to_string(n) + "-byte field with its NUL");
      if (s->find('\0') != std::string::npos) bad_value(pkt, f.name, "embedded NUL");
      std::memcpy(p, s->data(), s->size());
      return;
    }
    case FieldType::Raw: {
      const auto* b = std::get_if<Bytes>(fv);
      if (!b) bad_value(pkt, f.name, "needs bytes");
      if (b->size() > n) bad_value(pkt, f.name, "more bytes than the field holds");
      if (!b->empty()) std::memcpy(p, b->data(), b->size());
      return;
    }
    case FieldType::Pos3: {
      const auto* v = std::get_if<Pos3>(fv);
      if (!v) bad_value(pkt, f.name, "needs a Pos3");
      if (v->x > 1023 || v->y > 1023 || v->dir > 15) bad_value(pkt, f.name, "position out of range (x,y <= 1023, dir <= 15)");
      const auto a = pack_pos3(*v);
      std::memcpy(p, a.data(), 3);
      return;
    }
    case FieldType::Pos6: {
      const auto* v = std::get_if<Pos6>(fv);
      if (!v) bad_value(pkt, f.name, "needs a Pos6");
      if (v->x0 > 1023 || v->y0 > 1023 || v->x1 > 1023 || v->y1 > 1023 || v->sx0 > 15 || v->sy0 > 15)
        bad_value(pkt, f.name, "position out of range (x,y <= 1023, sub-cell <= 15)");
      const auto a = pack_pos6(*v);
      std::memcpy(p, a.data(), 6);
      return;
    }
    case FieldType::F32: {
      const auto* v = std::get_if<double>(fv);
      if (!v) bad_value(pkt, f.name, "needs a double");
      const float fl = static_cast<float>(*v);
      std::uint32_t bits;
      std::memcpy(&bits, &fl, sizeof bits);
      wr(p, 4, bits);
      return;
    }
    default: break;
  }
  if (f.array_len == 1) {
    const auto* v = std::get_if<std::int64_t>(fv);
    if (!v) bad_value(pkt, f.name, "needs an integer");
    check_range(pkt, f, *v, es, f.bit_width);
    if (f.bit_width) {
      const std::uint64_t mask = f.bit_width >= 64 ? ~std::uint64_t{0} : ((std::uint64_t{1} << f.bit_width) - 1);
      const std::uint64_t cur = rd(p, es);
      wr(p, es, cur | ((static_cast<std::uint64_t>(*v) & mask) << f.bit_shift));
    } else {
      wr(p, es, static_cast<std::uint64_t>(*v));
    }
    return;
  }
  if (es == 1) {
    const auto* b = std::get_if<Bytes>(fv);
    if (!b) bad_value(pkt, f.name, "needs bytes");
    if (b->size() > f.array_len) bad_value(pkt, f.name, "more elements than the array holds");
    if (!b->empty()) std::memcpy(p, b->data(), b->size());
    return;
  }
  const auto* v = std::get_if<std::vector<std::int64_t>>(fv);
  if (!v) bad_value(pkt, f.name, "needs an integer array");
  if (v->size() > f.array_len) bad_value(pkt, f.name, "more elements than the array holds");
  for (std::size_t i = 0; i < v->size(); ++i) {
    check_range(pkt, f, (*v)[i], es, 0);
    wr(p + i * es, es, static_cast<std::uint64_t>((*v)[i]));
  }
}

void reject_unknown(const std::string& pkt, const std::map<std::string, FieldValue>& vals,
                    const std::vector<FieldSpec>& specs, const FieldSpec* tail, bool payload_ok, const char* where) {
  for (const auto& [k, _] : vals) {
    bool ok = (payload_ok && k == "payload") || (tail && tail->name == k);
    for (std::size_t i = 0; !ok && i < specs.size(); ++i) ok = specs[i].name == k;
    if (!ok) throw CodecError(CodecErrc::kUnknownField, pkt + ": no " + where + " field '" + k + "'");
  }
}

}  // namespace

CodecErrc CodecSet::decode_layout(const Layout& l, ByteView f, DecodedPacket& out) const {
  const LengthRule& lr = l.length;
  std::size_t header;  // bytes before the payload of an opaque packet
  if (lr.kind == LengthKind::Fixed) {
    if (f.size() != lr.fixed_size) return CodecErrc::kLengthMismatch;
    header = 2;
  } else {
    if (f.size() < static_cast<std::size_t>(lr.prefix_offset) + 2) return CodecErrc::kShortFrame;
    const std::size_t declared = static_cast<std::size_t>(rd(f.data() + lr.prefix_offset, 2));
    const std::size_t floor_len = std::max<std::size_t>({4, lr.header_size, static_cast<std::size_t>(lr.prefix_offset) + 2});
    if (declared < floor_len || declared > kMaxFrame) return CodecErrc::kBadLength;
    if (declared != f.size()) return CodecErrc::kLengthMismatch;
    header = static_cast<std::size_t>(lr.prefix_offset) + 2;
  }
  out = DecodedPacket{};
  out.name = l.name;
  if (!l.fields_known) {
    out.fields["payload"] = Bytes(f.begin() + static_cast<std::ptrdiff_t>(header), f.end());
    return CodecErrc::kOk;
  }
  for (const FieldSpec& fs : l.head)
    if (!decode_field(fs, f, 0, f.size(), fs.name, out.fields)) return CodecErrc::kFieldOutOfBounds;
  if (l.tail && !decode_field(*l.tail, f, 0, f.size(), l.tail->name, out.fields)) return CodecErrc::kFieldOutOfBounds;
  if (lr.repeat) {
    const RepeatGroup& g = *lr.repeat;
    if (g.offset > f.size()) return CodecErrc::kFieldOutOfBounds;
    const std::size_t body = f.size() - g.offset;
    if (g.stride == 0 || body % g.stride != 0) return CodecErrc::kRepeatMismatch;
    const std::size_t rows = body / g.stride;
    out.repeats.reserve(rows);
    for (std::size_t i = 0; i < rows; ++i) {
      std::map<std::string, FieldValue> row;
      const std::size_t base = g.offset + i * g.stride;
      for (const FieldSpec& fs : l.row)
        if (!decode_field(fs, f, base, base + g.stride, fs.name, row)) return CodecErrc::kFieldOutOfBounds;
      out.repeats.push_back(std::move(row));
    }
  }
  return CodecErrc::kOk;
}

CodecErrc CodecSet::try_decode(ByteView f, Direction d, DecodedPacket& out) const noexcept {
  try {
    if (f.size() < 2) return CodecErrc::kShortFrame;
    const Layout* l = find_op(static_cast<std::uint16_t>(rd(f.data(), 2)), d);
    if (!l) return CodecErrc::kUnknownOpcode;
    return decode_layout(*l, f, out);
  } catch (...) {
    return CodecErrc::kTooLarge;  // allocation failure
  }
}

DecodedPacket CodecSet::decode(ByteView frame, Direction d) const {
  DecodedPacket p;
  const CodecErrc e = try_decode(frame, d, p);
  if (e != CodecErrc::kOk) {
    std::string what = std::to_string(frame.size()) + "-byte frame";
    if (frame.size() >= 2) {
      char b[16];
      std::snprintf(b, sizeof b, " opcode 0x%04x", static_cast<unsigned>(rd(frame.data(), 2)));
      what += b;
    }
    throw CodecError(e, what);
  }
  return p;
}

DecodedPacket CodecSet::decode(ByteView frame) const { return decode(frame, Direction::ClientToServer); }

Bytes CodecSet::encode(const DecodedPacket& p) const {
  const Layout* l = find_name(p.name);
  if (!l) throw CodecError(CodecErrc::kUnknownPacket, p.name);
  const LengthRule& lr = l->length;
  const std::string& pkt = l->name;

  if (!l->fields_known) {
    Bytes payload;
    if (const FieldValue* fv = find_value(p.fields, "payload")) {
      const auto* b = std::get_if<Bytes>(fv);
      if (!b) bad_value(pkt, "payload", "needs bytes");
      payload = *b;
    }
    for (const auto& [k, _] : p.fields)
      if (k != "payload") throw CodecError(CodecErrc::kUnknownField, pkt + ": layout unknown, only 'payload' is accepted, got '" + k + "'");
    const std::size_t header = lr.kind == LengthKind::Fixed ? 2 : static_cast<std::size_t>(lr.prefix_offset) + 2;
    const std::size_t size = header + payload.size();
    if (lr.kind == LengthKind::Fixed ? size != lr.fixed_size : (size < std::max<std::size_t>(4, lr.header_size) || size > kMaxFrame))
      bad_value(pkt, "payload", "size does not give a legal frame length");
    Bytes out(size, 0);
    wr(out.data(), 2, l->opcode);
    if (lr.kind == LengthKind::Variable) wr(out.data() + lr.prefix_offset, 2, size);
    if (!payload.empty()) std::memcpy(out.data() + header, payload.data(), payload.size());
    return out;
  }

  reject_unknown(pkt, p.fields, l->head, l->tail ? &*l->tail : nullptr, false, "packet");
  if (!lr.repeat && !p.repeats.empty()) throw CodecError(CodecErrc::kUnknownField, pkt + ": has no repeat group");
  for (const auto& row : p.repeats) reject_unknown(pkt, row, l->row, nullptr, false, "row");

  std::size_t size;
  Bytes tail_bytes;
  if (lr.kind == LengthKind::Fixed) {
    size = lr.fixed_size;
  } else {
    size = lr.header_size;
    if (lr.repeat) {
      size += p.repeats.size() * lr.repeat->stride;
    } else if (l->tail) {
      if (const FieldValue* fv = find_value(p.fields, l->tail->name)) {
        if (l->tail->type == FieldType::FixedString) {
          const auto* s = std::get_if<std::string>(fv);
          if (!s) bad_value(pkt, l->tail->name, "needs a string");
          if (s->find('\0') != std::string::npos) bad_value(pkt, l->tail->name, "embedded NUL");
          tail_bytes.assign(s->begin(), s->end());
        } else {
          const auto* b = std::get_if<Bytes>(fv);
          if (!b) bad_value(pkt, l->tail->name, "needs bytes");
          tail_bytes = *b;
        }
      }
      if (l->tail->type == FieldType::FixedString) tail_bytes.push_back(0);  // wire strings carry their NUL
      size += tail_bytes.size();
    }
    if (size > kMaxFrame) throw CodecError(CodecErrc::kTooLarge, pkt + ": " + std::to_string(size) + " bytes");
  }
  Bytes out(size, 0);
  wr(out.data(), 2, l->opcode);
  if (lr.kind == LengthKind::Variable) wr(out.data() + lr.prefix_offset, 2, size);
  for (const FieldSpec& f : l->head) write_tail_or_field(pkt, f, out.data(), out.size(), 0, p.fields, f.name);
  if (!tail_bytes.empty()) std::memcpy(out.data() + l->tail->offset, tail_bytes.data(), tail_bytes.size());
  if (lr.repeat) {
    for (std::size_t i = 0; i < p.repeats.size(); ++i)
      for (const FieldSpec& f : l->row)
        write_tail_or_field(pkt, f, out.data(), out.size(), lr.repeat->offset + i * lr.repeat->stride, p.repeats[i], f.name);
  }
  return out;
}

}  // namespace netproto
