// CodecSet::decode / encode over FieldSpec (CAP-E02): every field kind on hand-built layouts, error
// handling, presence by (date, flavour), repeat groups, tails, bit fields, unknown-layout packets, and
// property round trips over random values.
#include <cstring>
#include <functional>
#include <limits>
#include <tuple>
#include <random>

#include "fixtures.hpp"
#include "harness.hpp"
#include "randval.hpp"

using namespace netproto;
using namespace fx;

namespace {
FieldSpec fld(const char* n, std::uint16_t off, FieldType t, std::uint16_t w = 0, std::uint16_t arr = 1) {
  FieldSpec f; f.name = n; f.offset = off; f.type = t; f.width = w; f.array_len = arr; return f;
}
FieldSpec row_fld(const char* n, std::uint16_t off, FieldType t, std::uint16_t w = 0) {
  FieldSpec f = fld(n, off, t, w); f.row = true; return f;
}
FieldSpec bits(const char* n, std::uint16_t off, FieldType t, std::uint8_t shift, std::uint8_t width) {
  FieldSpec f = fld(n, off, t); f.bit_shift = shift; f.bit_width = width; return f;
}
CodecSet one(PacketDef d, ClientVersion v = {20211103, Flavour::Re}) {
  PacketCatalog c;
  c.add(std::move(d));
  return CodecSet::build(c, v);
}
PacketDef def(const char* name, std::uint16_t op, LengthRule len, std::vector<FieldSpec> f, Direction dir = Direction::ClientToServer) {
  return packet(name, dir, Phase::InWorld, op, len, "RE>=20000000", std::move(f));
}
std::int64_t I(const DecodedPacket& p, const char* k) { return std::get<std::int64_t>(p.fields.at(k)); }
bool throws_code(const std::function<void()>& fn, CodecErrc want) {
  try { fn(); } catch (const CodecError& e) { return e.code == want; }
  return false;
}
}  // namespace

TEST("CAP-E02", integer_widths_signs_and_little_endian_order) {
  const auto s = one(def("P", 0x0200, fixed(2 + 1 + 1 + 2 + 2 + 4 + 4 + 8 + 8),
                         {fld("u8", 2, FieldType::U8), fld("i8", 3, FieldType::I8), fld("u16", 4, FieldType::U16),
                          fld("i16", 6, FieldType::I16), fld("u32", 8, FieldType::U32), fld("i32", 12, FieldType::I32),
                          fld("i64", 16, FieldType::I64), fld("u64", 24, FieldType::U64)}));
  Bytes f = {0x00, 0x02, 0xFF, 0xFF, 0x34, 0x12, 0xFE, 0xFF, 0x78, 0x56, 0x34, 0x12, 0x00, 0x00, 0x00, 0x80,
             0x01, 0, 0, 0, 0, 0, 0, 0x80, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  const auto p = s.decode(f);
  CHECK(I(p, "u8") == 255 && I(p, "i8") == -1 && I(p, "u16") == 0x1234 && I(p, "i16") == -2);
  CHECK(I(p, "u32") == 0x12345678 && I(p, "i32") == std::numeric_limits<std::int32_t>::min());
  CHECK(I(p, "i64") == std::numeric_limits<std::int64_t>::min() + 1 && I(p, "u64") == -1);  // u64 is carried as its bit pattern
  CHECK(s.encode(p) == f);
}

TEST("CAP-E02", fixed_strings_are_nul_terminated_and_never_overflow) {
  const auto s = one(def("P", 0x0201, fixed(2 + 8), {fld("name", 2, FieldType::FixedString, 8)}));
  DecodedPacket p; p.name = "P";
  p.fields["name"] = std::string("abc");
  CHECK(s.encode(p) == bytes({0x01, 0x02, 'a', 'b', 'c', 0, 0, 0, 0, 0}));
  CHECK(std::get<std::string>(s.decode(bytes({0x01, 0x02, 'a', 'b', 0, 'X', 'Y', 0, 0, 0})).fields.at("name")) == "ab");  // cut at the first NUL
  CHECK(std::get<std::string>(s.decode(bytes({0x01, 0x02, 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h'})).fields.at("name")) == "abcdefgh");  // unterminated: width-limited
  p.fields["name"] = std::string("1234567");  // 7 + NUL fits
  CHECK(s.decode(s.encode(p)).fields == p.fields);
  p.fields["name"] = std::string("12345678");  // would lose its terminator
  CHECK(throws_code([&] { (void)s.encode(p); }, CodecErrc::kBadValue));
  p.fields["name"] = std::string("a\0b", 3);
  CHECK(throws_code([&] { (void)s.encode(p); }, CodecErrc::kBadValue));
  p.fields["name"] = std::int64_t{5};
  CHECK(throws_code([&] { (void)s.encode(p); }, CodecErrc::kBadValue));
}

TEST("CAP-E02", arrays_raw_and_float_fields) {
  const auto s = one(def("P", 0x0202, fixed(2 + 4 + 8 + 4 + 3),
                         {fld("b", 2, FieldType::U8, 0, 4), fld("w", 6, FieldType::U16, 0, 4), fld("f", 14, FieldType::F32),
                          fld("raw", 18, FieldType::Raw, 3)}));
  DecodedPacket p; p.name = "P";
  p.fields["b"] = Bytes{1, 2, 3, 4};
  p.fields["w"] = std::vector<std::int64_t>{1, 2, 0xFFFF, 40000};
  p.fields["f"] = 12.25;
  p.fields["raw"] = Bytes{9, 8, 7};
  const Bytes w = s.encode(p);
  CHECK(w.size() == 21 && w[2] == 1 && w[5] == 4 && w[6] == 1 && w[10] == 0xFF && w[11] == 0xFF);
  CHECK(s.decode(w).fields == p.fields);
  p.fields["b"] = Bytes{1, 2, 3, 4, 5};
  CHECK(throws_code([&] { (void)s.encode(p); }, CodecErrc::kBadValue));
}

TEST("CAP-E02", bit_fields_share_one_storage_byte) {
  // legacy: struct { uint8 IsIdentified:1; uint8 IsDamaged:1; uint8 PlaceETCTab:1; uint8 SpareBits:5; } Flag;
  const auto s = one(def("P", 0x0203, fixed(3), {bits("IsIdentified", 2, FieldType::U8, 0, 1), bits("IsDamaged", 2, FieldType::U8, 1, 1),
                                                   bits("PlaceETCTab", 2, FieldType::U8, 2, 1), bits("SpareBits", 2, FieldType::U8, 3, 5)}));
  const auto p = s.decode(bytes({0x03, 0x02, 0b10110101}));
  CHECK(I(p, "IsIdentified") == 1 && I(p, "IsDamaged") == 0 && I(p, "PlaceETCTab") == 1 && I(p, "SpareBits") == 0b10110);
  CHECK(s.encode(p) == bytes({0x03, 0x02, 0b10110101}));
  DecodedPacket q = p; q.fields["IsDamaged"] = std::int64_t{2};  // does not fit one bit
  CHECK(throws_code([&] { (void)s.encode(q); }, CodecErrc::kBadValue));
}

TEST("CAP-E02", packed_positions_in_fields) {
  const auto s = one(def("P", 0x0204, fixed(2 + 3 + 6), {fld("pos", 2, FieldType::Pos3), fld("move", 5, FieldType::Pos6)}));
  DecodedPacket p; p.name = "P";
  p.fields["pos"] = Pos3{150, 200, 5};
  p.fields["move"] = Pos6{10, 20, 1023, 0, 3, 12};
  const Bytes w = s.encode(p);
  CHECK(w[2] == 0x25 && w[3] == 0x8C && w[4] == 0x85);  // the same bytes as the pos3 golden vector
  CHECK(s.decode(w).fields == p.fields);
  p.fields["pos"] = Pos3{1024, 0, 0};
  CHECK(throws_code([&] { (void)s.encode(p); }, CodecErrc::kBadValue));
}

TEST("CAP-E02", variable_length_prefix_is_written_and_validated) {
  const auto s = one(def("V", 0x0205, variable(6), {fld("a", 4, FieldType::U16)}));
  DecodedPacket p; p.name = "V"; p.fields["a"] = std::int64_t{0x0102};
  const Bytes w = s.encode(p);
  CHECK(w == bytes({0x05, 0x02, 0x06, 0x00, 0x02, 0x01}));
  CHECK(I(s.decode(w), "a") == 0x0102);
  Bytes bad = w; bad[2] = 7;  // declares 7, buffer has 6
  CHECK(throws_code([&] { (void)s.decode(bad); }, CodecErrc::kLengthMismatch));
  bad[2] = 5; bad.resize(5);  // declared length below the header
  CHECK(throws_code([&] { (void)s.decode(bad); }, CodecErrc::kBadLength));
  CHECK(throws_code([&] { (void)s.decode(bytes({0x05, 0x02, 0x06})); }, CodecErrc::kShortFrame));
  CHECK(throws_code([&] { (void)s.decode(bytes({0x05})); }, CodecErrc::kShortFrame));
  CHECK(throws_code([&] { (void)s.decode(bytes({0x99, 0x09})); }, CodecErrc::kUnknownOpcode));
}

TEST("CAP-E02", string_and_raw_tails) {
  const auto s = one(def("T", 0x0206, variable(8), {fld("id", 4, FieldType::U32), fld("text", 8, FieldType::FixedString)}));
  DecodedPacket p; p.name = "T"; p.fields["id"] = std::int64_t{7}; p.fields["text"] = std::string("hello");
  const Bytes w = s.encode(p);
  CHECK(w.size() == 8 + 6 && w.back() == 0 && w[2] == 14);  // the NUL is part of the frame
  CHECK(s.decode(w).fields == p.fields);
  Bytes unterminated(w.begin(), w.end() - 1); unterminated[2] = 13;  // tolerated: the codec does not police text
  CHECK(std::get<std::string>(s.decode(unterminated).fields.at("text")) == "hello");
  p.fields.erase("text");
  CHECK(s.encode(p).size() == 9);  // an absent string is the empty string, still NUL terminated
  const auto r = one(def("R", 0x0207, variable(4), {fld("blob", 4, FieldType::Raw)}));
  DecodedPacket q; q.name = "R"; q.fields["blob"] = Bytes{1, 2, 3};
  CHECK(r.encode(q) == bytes({0x07, 0x02, 0x07, 0x00, 1, 2, 3}));
  CHECK(r.decode(r.encode(q)).fields == q.fields);
  q.fields.erase("blob");
  CHECK(r.encode(q) == bytes({0x07, 0x02, 0x04, 0x00}));
  q.fields["blob"] = Bytes(40000, 1);
  CHECK(throws_code([&] { (void)r.encode(q); }, CodecErrc::kTooLarge));
}

TEST("CAP-E02", repeat_groups_decode_into_rows_and_must_tile_the_frame) {
  LengthRule len = variable(8);
  len.repeat = RepeatGroup{8, 6};
  const auto s = one(def("G", 0x0208, len, {fld("count", 4, FieldType::U32), row_fld("id", 0, FieldType::U32), row_fld("qty", 4, FieldType::U16)}));
  DecodedPacket p; p.name = "G"; p.fields["count"] = std::int64_t{2};
  p.repeats = {{{"id", std::int64_t{10}}, {"qty", std::int64_t{1}}}, {{"id", std::int64_t{20}}, {"qty", std::int64_t{2}}}};
  const Bytes w = s.encode(p);
  CHECK(w.size() == 8 + 12 && w[2] == 20);
  const auto d = s.decode(w);
  CHECK(d.repeats == p.repeats && d.fields == p.fields);
  Bytes odd = w; odd.push_back(0); odd[2] = 21;
  CHECK(throws_code([&] { (void)s.decode(odd); }, CodecErrc::kRepeatMismatch));
  DecodedPacket none; none.name = "G";
  CHECK(s.encode(none).size() == 8 && s.decode(s.encode(none)).repeats.empty());  // zero rows is legal
  p.repeats[1]["bogus"] = std::int64_t{1};
  CHECK(throws_code([&] { (void)s.encode(p); }, CodecErrc::kUnknownField));
}

TEST("CAP-E02", gateway_conventions_are_understood) {
  // the gateway's private layouts name row fields "[].x" and use a width-0 Raw tail; both keep working
  LengthRule len = variable(4);
  len.repeat = RepeatGroup{4, 4};
  const auto s = one(def("G", 0x0209, len, {fld("[].a", 0, FieldType::U16), fld("[].b", 2, FieldType::U16)}));
  DecodedPacket p; p.name = "G"; p.repeats = {{{"a", std::int64_t{1}}, {"b", std::int64_t{2}}}};
  CHECK(s.encode(p) == bytes({0x09, 0x02, 0x08, 0x00, 1, 0, 2, 0}));
  CHECK(s.decode(s.encode(p)).repeats == p.repeats);
}

TEST("CAP-E02", field_presence_depends_on_date_and_flavour) {
  // CHARACTER_INFO-style: the last word exists on RE clients from 20211103 on, another one on main.
  PacketDef d;
  d.name = "C"; d.direction = Direction::ClientToServer; d.gate = Phase::InWorld;
  PacketVersion v; v.applies = pred(""); v.opcode = 0x020a; v.length = fixed(10);
  v.fields = {fld("a", 2, FieldType::U16)};
  FieldSpec re_only = fld("re_wide", 4, FieldType::U32); re_only.present_when = pred("RE>=20211103");
  FieldSpec main_only = fld("main_narrow", 4, FieldType::U16); main_only.present_when = pred("MAIN>=20000000");
  FieldSpec ancient = fld("ancient", 8, FieldType::U16); ancient.present_when = pred("RE>=20000000 && RE<20100000");
  v.fields.push_back(re_only); v.fields.push_back(main_only); v.fields.push_back(ancient);
  d.versions.push_back(v);
  PacketCatalog c; c.add(d);
  const Bytes f = bytes({0x0a, 0x02, 1, 0, 0x78, 0x56, 0x34, 0x12, 9, 0});
  const auto re = CodecSet::build(c, {20211103, Flavour::Re}).decode(f);
  CHECK(re.fields.count("re_wide") && !re.fields.count("main_narrow") && !re.fields.count("ancient") && I(re, "re_wide") == 0x12345678);
  const auto old_re = CodecSet::build(c, {20211102, Flavour::Re}).decode(f);
  CHECK(!old_re.fields.count("re_wide") && !old_re.fields.count("main_narrow") && !old_re.fields.count("ancient"));
  const auto main = CodecSet::build(c, {20211103, Flavour::Main}).decode(f);
  CHECK(main.fields.count("main_narrow") && !main.fields.count("re_wide") && I(main, "main_narrow") == 0x5678);
  const auto ancient_re = CodecSet::build(c, {20090101, Flavour::Re}).decode(f);
  CHECK(ancient_re.fields.count("ancient") && I(ancient_re, "ancient") == 9);
  // encode honours the same filter: a field absent on this client is an unknown field
  DecodedPacket p; p.name = "C"; p.fields["re_wide"] = std::int64_t{1};
  CHECK(throws_code([&] { (void)CodecSet::build(c, {20211103, Flavour::Main}).encode(p); }, CodecErrc::kUnknownField));
}

TEST("CAP-E02", unknown_layouts_pass_their_payload_through) {
  PacketDef d = def("U", 0x020b, fixed(6), {});
  d.versions[0].fields_known = false;
  PacketDef v = def("W", 0x020c, variable(4), {});
  v.versions[0].fields_known = false;
  PacketCatalog c; c.add(d); c.add(v);
  const auto s = CodecSet::build(c, {20211103, Flavour::Re});
  const auto p = s.decode(bytes({0x0b, 0x02, 1, 2, 3, 4}));
  CHECK(std::get<Bytes>(p.fields.at("payload")) == (Bytes{1, 2, 3, 4}));
  CHECK(s.encode(p) == bytes({0x0b, 0x02, 1, 2, 3, 4}));
  const auto q = s.decode(bytes({0x0c, 0x02, 0x07, 0x00, 5, 6, 7}));
  CHECK(std::get<Bytes>(q.fields.at("payload")) == (Bytes{5, 6, 7}));
  CHECK(s.encode(q) == bytes({0x0c, 0x02, 0x07, 0x00, 5, 6, 7}));
  DecodedPacket wrong; wrong.name = "U"; wrong.fields["payload"] = Bytes{1};
  CHECK(throws_code([&] { (void)s.encode(wrong); }, CodecErrc::kBadValue));  // fixed packet: wrong size
  wrong.fields["x"] = std::int64_t{1};
  CHECK(throws_code([&] { (void)s.encode(wrong); }, CodecErrc::kUnknownField));
}

TEST("CAP-E02", encode_rejects_unknown_packets_names_and_out_of_range_values) {
  const auto s = one(def("P", 0x020d, fixed(6), {fld("a", 2, FieldType::U16), fld("b", 4, FieldType::I16)}));
  DecodedPacket p; p.name = "NOPE";
  CHECK(throws_code([&] { (void)s.encode(p); }, CodecErrc::kUnknownPacket));
  p.name = "P"; p.fields["c"] = std::int64_t{1};
  CHECK(throws_code([&] { (void)s.encode(p); }, CodecErrc::kUnknownField));  // a typo is an error, not a silent drop
  p.fields.clear(); p.fields["a"] = std::int64_t{65536};
  CHECK(throws_code([&] { (void)s.encode(p); }, CodecErrc::kBadValue));
  p.fields["a"] = std::int64_t{-32769};
  CHECK(throws_code([&] { (void)s.encode(p); }, CodecErrc::kBadValue));
  p.fields["a"] = std::int64_t{-1};  // the bit pattern of 0xFFFF: accepted, decodes back canonically
  p.fields["b"] = std::int64_t{40000};
  const auto q = s.decode(s.encode(p));
  CHECK(I(q, "a") == 65535 && I(q, "b") == 40000 - 65536);
  p.fields["a"] = std::string("x");
  CHECK(throws_code([&] { (void)s.encode(p); }, CodecErrc::kBadValue));
}

TEST("CAP-E02", build_rejects_inconsistent_layouts) {
  auto rejects = [](PacketDef d) {
    PacketCatalog c; c.add(std::move(d));
    try { (void)CodecSet::build(c, {20211103, Flavour::Re}); } catch (const SpecError&) { return true; }
    return false;
  };
  CHECK(rejects(def("P", 0x0300, fixed(4), {fld("a", 2, FieldType::U32)})));                                  // runs past the length
  CHECK(rejects(def("P", 0x0300, fixed(8), {fld("a", 2, FieldType::U16), fld("a", 4, FieldType::U16)})));       // duplicate name
  CHECK(rejects(def("P", 0x0300, variable(4), {fld("a", 2, FieldType::U32)})));                               // runs past the header
  CHECK(rejects(def("P", 0x0300, fixed(8), {fld("t", 2, FieldType::Raw)})));                                  // tail in a fixed packet
  CHECK(rejects(def("P", 0x0300, variable(6), {fld("t", 4, FieldType::Raw)})));                               // tail not at the header size
  CHECK(rejects(def("P", 0x0300, variable(4), {row_fld("r", 0, FieldType::U8)})));                            // row field without a group
  LengthRule g = variable(4); g.repeat = RepeatGroup{4, 2};
  CHECK(rejects(def("P", 0x0300, g, {row_fld("r", 0, FieldType::U32)})));                                     // row wider than the stride
  LengthRule g2 = variable(4); g2.repeat = RepeatGroup{6, 2};
  CHECK(rejects(def("P", 0x0300, g2, {})));                                                                   // group not at the header
  CHECK(rejects(def("P", 0x0300, fixed(8), {bits("x", 2, FieldType::U8, 4, 8)})));                           // bits past the storage integer
  CHECK(rejects(def("P", 0x0300, fixed(8), {fld("s", 2, FieldType::Pos3, 0, 2)})));                           // position arrays
  CHECK(rejects(def("P", 0x0300, variable(8), {fld("a", 2, FieldType::U16)})));                               // field on the length word
  CHECK(rejects(def("P", 0x0300, fixed(8), {fld("a", 0, FieldType::U16)})));                                  // field on the opcode
}

namespace {
DecodedPacket random_packet(std::mt19937_64& g, const std::string& name, const std::vector<FieldSpec>& fields, bool has_repeat, bool known) {
  DecodedPacket p;
  p.name = name;
  if (!known) {
    Bytes b(g() % 12);
    for (auto& x : b) x = static_cast<std::uint8_t>(g());
    p.fields["payload"] = b;  // size fixed up by the caller for fixed packets
    return p;
  }
  const std::size_t rows = has_repeat ? g() % 4 : 0;
  p.repeats.resize(rows);
  for (const FieldSpec& f : fields) {
    if (is_row_field(f)) {
      for (auto& r : p.repeats) r[field_key(f)] = random_value(g, f);
    } else {
      p.fields[f.name] = random_value(g, f);
    }
  }
  return p;
}
}  // namespace

TEST("CAP-E02", random_values_roundtrip_through_every_field_kind) {
  // one packet that has every field kind, a repeat group and a string tail is not legal (tail xor rows), so two
  const std::vector<FieldSpec> head = {
      fld("u8", 2, FieldType::U8), fld("i8", 3, FieldType::I8), fld("u16", 4, FieldType::U16), fld("i16", 6, FieldType::I16),
      fld("u32", 8, FieldType::U32), fld("i32", 12, FieldType::I32), fld("i64", 16, FieldType::I64), fld("u64", 24, FieldType::U64),
      fld("s", 32, FieldType::FixedString, 13), fld("raw", 45, FieldType::Raw, 5), fld("pos", 50, FieldType::Pos3),
      fld("mv", 53, FieldType::Pos6), fld("arr8", 59, FieldType::U8, 0, 5), fld("arr16", 64, FieldType::I16, 0, 3),
      fld("arr32", 70, FieldType::U32, 0, 2), fld("fl", 78, FieldType::F32),
      bits("b0", 82, FieldType::U16, 0, 3), bits("b1", 82, FieldType::U16, 3, 9), bits("b2", 82, FieldType::U16, 12, 4)};
  // a variable packet keeps its length word at offset 2, so its fields start at 4
  auto shifted = [&](std::vector<FieldSpec> v) { for (auto& f : v) f.offset = static_cast<std::uint16_t>(f.offset + 2); return v; };
  auto with = [&](std::vector<FieldSpec> extra) { auto v = shifted(head); v.insert(v.end(), extra.begin(), extra.end()); return v; };
  LengthRule rep = variable(86); rep.repeat = RepeatGroup{86, 9};
  const auto fixed_fields = head;
  const auto rows_fields = with({row_fld("id", 0, FieldType::U32), row_fld("name", 4, FieldType::FixedString, 4), row_fld("k", 8, FieldType::I8)});
  const auto tail_fields = with({fld("text", 86, FieldType::FixedString)});
  PacketCatalog c;
  c.add(def("FIX", 0x0400, fixed(84), fixed_fields));
  c.add(def("ROWS", 0x0401, rep, rows_fields));
  c.add(def("TAIL", 0x0402, variable(86), tail_fields));
  const CodecSet s = CodecSet::build(c, {20211103, Flavour::Re});
  std::mt19937_64 g(0xC0DEC);
  for (int i = 0; i < 3000; ++i) {
    for (const auto& [name, fields, repeat] : {std::tuple{"FIX", fixed_fields, false}, std::tuple{"ROWS", rows_fields, true},
                                                  std::tuple{"TAIL", tail_fields, false}}) {
      const DecodedPacket p = random_packet(g, name, fields, repeat, true);
      const Bytes w = s.encode(p);
      const DecodedPacket q = s.decode(w);
      CHECK(q.name == p.name);
      CHECK(q.fields == p.fields);
      CHECK(q.repeats == p.repeats);
      CHECK(s.encode(q) == w);  // canonical form: encode(decode(x)) == x for frames the codec itself wrote
    }
  }
}

TEST("BR-INTER-013", decoding_every_prefix_and_every_single_byte_flip_never_reads_out_of_bounds) {
  LengthRule rep = variable(8); rep.repeat = RepeatGroup{8, 6};
  PacketCatalog c;
  c.add(def("ROWS", 0x0401, rep, {fld("n", 4, FieldType::U32), row_fld("id", 0, FieldType::U32), row_fld("q", 4, FieldType::U16)}));
  c.add(def("TXT", 0x0402, variable(8), {fld("id", 4, FieldType::U32), fld("t", 8, FieldType::FixedString)}));
  c.add(def("FIX", 0x0403, fixed(21), {fld("s", 2, FieldType::FixedString, 16), fld("p", 18, FieldType::Pos3)}));
  const CodecSet s = CodecSet::build(c, {20211103, Flavour::Re});
  std::mt19937_64 g(5);
  for (const char* name : {"ROWS", "TXT", "FIX"}) {
    DecodedPacket p; p.name = name;
    if (std::string(name) == "ROWS") p.repeats = {{{"id", std::int64_t{1}}, {"q", std::int64_t{2}}}, {{"id", std::int64_t{3}}, {"q", std::int64_t{4}}}};
    if (std::string(name) == "TXT") p.fields["t"] = std::string("some text");
    const Bytes w = s.encode(p);
    for (std::size_t n = 0; n <= w.size(); ++n) {  // truncated frames: only the whole one decodes
      DecodedPacket out;
      const CodecErrc e = s.try_decode(ByteView(w.data(), n), Direction::ClientToServer, out);
      CHECK((e == CodecErrc::kOk) == (n == w.size()));
    }
    for (std::size_t i = 0; i < w.size(); ++i)
      for (int k = 0; k < 8; ++k) {
        Bytes m = w;
        m[i] ^= static_cast<std::uint8_t>(1u << k);
        DecodedPacket out;
        (void)s.try_decode(m, Direction::ClientToServer, out);  // any verdict, no UB (ASan/UBSan job)
      }
  }
  (void)g;
}
