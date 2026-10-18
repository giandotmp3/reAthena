// Compatibility with the gateway's private codec (gateway/src/wire.cpp, included read-only): its PacketCodec
// (decode / decode_s2c / encode over its own PacketCatalog and FieldSpec interpreter) against netproto's
// CodecSet over the SAME catalog, packet by packet and byte by byte; and its layouts against the netproto
// full catalog. Disagreements are findings, pinned below with the verdict the legacy headers give.
#include <iostream>
#include <random>
#include <set>

#include "../harness.hpp"
#include "../randval.hpp"
#include "../real_catalog.hpp"
#include "gateway/wire.hpp"
#include "layout_diff.hpp"

using namespace netproto;
using namespace fx;
using gateway::wire::PacketCodec;
using gateway::wire::WireError;

namespace {
// The gateway's catalog with its overlay layouts (CA_LOGIN, CZ_ENTER) promoted to real layouts: this is the
// catalog a migrated gateway would hand to netproto.
PacketCatalog migrated_catalog(const PacketCodec& pc) {
  PacketCatalog out;
  for (PacketDef d : pc.catalog().packets()) {
    for (PacketVersion& v : d.versions)
      if (!v.fields_known) {
        const auto& f = pc.fields_of(d.name);
        if (!f.empty() || d.name == "CZ_NOTIFY_ACTORINIT") { v.fields = f; v.fields_known = true; }
      }
    out.add(std::move(d));
  }
  return out;
}
}  // namespace

TEST("CAP-E02", gateway_packets_encode_and_decode_byte_identically_through_both_codecs) {
  PacketCodec pc;
  const PacketCatalog migrated = migrated_catalog(pc);
  const CodecSet np = CodecSet::build(migrated, pc.version());
  std::mt19937_64 g(424242);
  std::size_t packets = 0, frames = 0;
  for (const auto& rp : migrated.resolve(pc.version())) {
    const PacketVersion& v = *rp.version;
    if (!v.fields_known) continue;     // opaque in the gateway as well (no layout to compare)
    bool has_array = false;
    for (const FieldSpec& f : v.fields) has_array |= f.array_len > 1;
    if (has_array) continue;           // finding: the gateway's interpreter ignores FieldSpec::array_len (next test)
    ++packets;
    for (int it = 0; it < 200; ++it) {
      DecodedPacket p;
      p.name = rp.def->name;
      const std::size_t rows = v.length.repeat ? static_cast<std::size_t>(it % 4) : 0;
      p.repeats.assign(rows, {});
      for (const FieldSpec& f : v.fields) {
        FieldValue val = random_value(g, f);
        if (f.type == FieldType::FixedString && f.width == 0) val = Bytes{0x5A};   // the gateway's tails are Raw
        // never empty: gateway/src/wire.cpp:401 calls memcpy(dst, tail.data(), 0) with a null source for an empty tail (UBSan)
        if (f.type == FieldType::Raw && f.width == 0 && std::get<Bytes>(val).empty()) val = Bytes{0x5A};
        if (is_row_field(f)) for (auto& r : p.repeats) r[field_key(f)] = val; else p.fields[f.name] = val;
      }
      const Bytes theirs = pc.encode(p);
      const Bytes mine = np.encode(p);
      if (theirs != mine) throw CheckFailure(rp.def->name + ": the gateway's frame differs from netproto's encoding");
      const bool c2s = rp.def->direction == Direction::ClientToServer;
      const DecodedPacket a = c2s ? pc.decode(theirs) : pc.decode_s2c(theirs);
      const DecodedPacket b = np.decode(mine, rp.def->direction);
      if (!(a.name == b.name && a.fields == b.fields && a.repeats == b.repeats)) throw CheckFailure(rp.def->name + ": the two decoders disagree");
      ++frames;
    }
  }
  CHECK(packets >= 55 && frames == packets * 200);
}

TEST("CAP-E02", gateway_interpreter_ignores_array_len_netproto_does_not) {
  // Finding: ZC_NOTIFY_PLAYERMOVE.moveData is u8[6] (array_len 6) in the spec. The gateway's decode reads ONE byte
  // as an integer and its encode refuses a byte array; netproto decodes/encodes the whole array (and, in the full
  // catalog, as a pos6). The gateway never sends this packet, so nothing is lost today.
  PacketCodec pc;
  Bytes frame = {0x87, 0x00, 1, 0, 0, 0, 0x25, 0x8C, 0x85, 1, 2, 3};
  const DecodedPacket theirs = pc.decode_s2c(frame);
  CHECK(std::holds_alternative<std::int64_t>(theirs.fields.at("moveData")));
  const DecodedPacket mine = CodecSet::build(pc.catalog(), pc.version()).decode(frame, Direction::ServerToClient);
  CHECK((std::get<Bytes>(mine.fields.at("moveData")) == Bytes{0x25, 0x8C, 0x85, 1, 2, 3}));
  bool refused = false;
  try { DecodedPacket p; p.name = "ZC_NOTIFY_PLAYERMOVE"; p.fields["moveData"] = Bytes{1, 2, 3, 4, 5, 6}; (void)pc.encode(p); } catch (const WireError&) { refused = true; }
  CHECK(refused);
}

TEST("BR-INTER-013", gateway_and_netproto_accept_and_reject_the_same_truncated_and_corrupted_frames) {
  PacketCodec pc;
  const PacketCatalog migrated = migrated_catalog(pc);
  const CodecSet np = CodecSet::build(migrated, pc.version());
  std::mt19937_64 g(99);
  std::size_t compared = 0;
  std::vector<std::string> disagreements;
  for (const auto& rp : migrated.resolve(pc.version())) {
    if (!rp.version->fields_known) continue;
    DecodedPacket p;
    p.name = rp.def->name;
    p.repeats.assign(rp.version->length.repeat ? 2 : 0, {});
    for (const FieldSpec& f : rp.version->fields) {
      FieldValue val = random_value(g, f);
      if (f.type == FieldType::FixedString && f.width == 0) val = Bytes{0x5A};
      if (f.type == FieldType::Raw && f.width == 0 && std::get<Bytes>(val).empty()) val = Bytes{0x5A};
      if (is_row_field(f)) for (auto& r : p.repeats) r[field_key(f)] = val; else p.fields[f.name] = val;
    }
    const Bytes w = np.encode(p);
    const bool c2s = rp.def->direction == Direction::ClientToServer;
    auto theirs_ok = [&](const Bytes& b) { try { (void)(c2s ? pc.decode(b) : pc.decode_s2c(b)); return true; } catch (const WireError&) { return false; } };
    auto mine_ok = [&](const Bytes& b) { DecodedPacket out; return np.try_decode(b, rp.def->direction, out) == CodecErrc::kOk; };
    for (std::size_t n = 0; n <= w.size(); ++n) {   // every prefix
      const Bytes cut(w.begin(), w.begin() + static_cast<std::ptrdiff_t>(n));
      if (theirs_ok(cut) != mine_ok(cut)) disagreements.push_back(rp.def->name + ": prefix of " + std::to_string(n) + " bytes");
      ++compared;
    }
    if (rp.version->length.kind == LengthKind::Variable) {   // every wrong declared length
      for (std::uint32_t declared : {0u, 1u, 3u, 4u, 5u, static_cast<std::uint32_t>(w.size() - 1), static_cast<std::uint32_t>(w.size() + 1), 32768u, 32769u, 65535u}) {
        Bytes b = w;
        b[2] = declared & 0xFF; b[3] = declared >> 8;
        if (theirs_ok(b) != mine_ok(b)) disagreements.push_back(rp.def->name + ": declared length " + std::to_string(declared));
        ++compared;
      }
    }
  }
  for (const auto& d : disagreements) std::cerr << "FINDING " << d << "\n";
  CHECK(disagreements.empty());
  CHECK(compared > 1500);
}

TEST("CAP-E02", gateway_layouts_agree_with_the_netproto_catalog_except_the_pinned_findings) {
  PacketCodec pc;
  std::vector<std::string> out;
  std::size_t compared = 0, missing = 0;
  for (const auto& rp : pc.catalog().resolve(pc.version())) {
    const ResolvedPacket* t = nullptr;
    for (const auto& r : full_resolved()) if (r.def->name == rp.def->name) t = &r;
    if (!t) { ++missing; continue; }
    PacketVersion pv = *rp.version;
    if (!pv.fields_known) { pv.fields = pc.fields_of(rp.def->name); pv.fields_known = !pv.fields.empty(); }
    compat::diff_version(rp.def->name, *rp.def, pv, *t->def, *t->version, out);
    ++compared;
  }
  CHECK(missing == 0 && compared == pc.catalog().resolve(pc.version()).size() && compared == 59);
  // Each line is a finding the gateway's migration must decide; the verdict is what src/common/packets.hpp says.
  const std::vector<std::string> expected = {
      // gateway has no field layout for these four (they are overlays-less in its table): netproto derives them from the handlers
      "CZ_NOTIFY_ACTORINIT: private table has no field layout",
      "CZ_REQUEST_MOVE: private table has no field layout",
      "CZ_REQUEST_TIME: private table has no field layout",
      "CZ_REQUEST_CHAT: private table has no field layout",
      // netproto tags the 6 packed bytes as a position (WBUFPOS2 in clif.cpp); the gateway keeps u8[6]. netproto is right.
      "ZC_NOTIFY_PLAYERMOVE: field @6 private 'moveData' u8x6 vs netproto 'moveData' pos6",
      // `char token[]` / `char salt[]` in the legacy structs: text. The gateway keeps raw bytes. netproto's string tail is NUL-terminated on the wire.
      "CA_SSO_LOGIN_REQ: field @92 private 'token' raw[0] vs netproto 'token' string[0]",
      "AC_ACK_HASH: field @4 private 'salt' raw[0] vs netproto 'salt' string[0]",
      // PACKET_TC_RESULT carries a packetLength word: variable in the legacy header; the gateway fixed(34). Both accept the real 34-byte frame.
      "TC_RESULT: fixed/variable differs",
      // clif's char server reads nothing at offset 14 of CH_ENTER (the gateway's `unknown` u16)
      "CH_ENTER: private field 'unknown' u16 @14 has no netproto field",
      // `int16 level; int16 bIsChangedCharName;` in CHARACTER_INFO; the gateway reads them unsigned. netproto is right per the header.
      "HC_ACCEPT_ENTER: field row@92 private 'level' u16 vs netproto 'level' i16",
      "HC_ACCEPT_ENTER: field row@140 private 'bIsChangedCharName' u16 vs netproto 'bIsChangedCharName' i16",
      "HC_ACK_CHARINFO_PER_PAGE: field row@92 private 'level' u16 vs netproto 'level' i16",
      "HC_ACK_CHARINFO_PER_PAGE: field row@140 private 'bIsChangedCharName' u16 vs netproto 'bIsChangedCharName' i16",
      "HC_ACCEPT_MAKECHAR: field @94 private 'level' u16 vs netproto 'character.level' i16",
      "HC_ACCEPT_MAKECHAR: field @142 private 'bIsChangedCharName' u16 vs netproto 'character.bIsChangedCharName' i16",
      // 0x006a is AC_REFUSE_LOGIN (23 bytes in the legacy packet DB); the map-server refusal is 0x0074 ZC_REFUSE_ENTER (3 bytes) or 0x0081 SC_NOTIFY_BAN
      "ZC_REFUSE_ENTER: opcode differs",
  };
  for (const auto& s : out) std::cerr << "layout: " << s << "\n";
  CHECK(out == expected);
}

TEST("CAP-E02", gateway_field_name_differences_with_the_legacy_structs_are_the_documented_ones) {
  PacketCodec pc;
  std::vector<std::string> renamed;
  for (const auto& rp : pc.catalog().resolve(pc.version())) {
    const ResolvedPacket* t = nullptr;
    for (const auto& r : full_resolved()) if (r.def->name == rp.def->name) t = &r;
    CHECK(t != nullptr);
    if (!t->version->fields_known) continue;
    std::vector<FieldSpec> mine = rp.version->fields_known ? rp.version->fields : pc.fields_of(rp.def->name);
    for (const FieldSpec& f : mine) {
      for (const FieldSpec& g : t->version->fields)
        if (g.offset == f.offset && is_row_field(g) == is_row_field(f) && g.bit_width == 0 && field_key(g) != field_key(f) &&
            g.name.substr(g.name.rfind('.') == std::string::npos ? 0 : g.name.rfind('.') + 1) != field_key(f))   // "character.GID" is "GID"
          renamed.push_back(rp.def->name + ": " + field_key(f) + " -> " + field_key(g));
    }
  }
  for (const auto& s : renamed) std::cerr << "rename: " << s << "\n";
  const std::vector<std::string> expected = {
      "CZ_ENTER: auth_code -> login_id1",           // clif_parse_WantToConnection calls it login_id1
      "SC_NOTIFY_BAN: result -> errorCode",         // PACKET_SC_NOTIFY_BAN::errorCode
      "AC_ACCEPT_LOGIN: new -> new_",               // `new` is a C++ keyword: the legacy member is new_
      "ZC_REFUSE_ENTER: error -> errorCode",
  };
  CHECK(renamed == expected);
}

TEST("CAP-E02", gateway_truncates_over_long_strings_netproto_refuses_them) {
  // Finding: PacketCodec::encode cuts a string to width-1 (like the legacy safestrncpy); CodecSet::encode
  // reports kBadValue. A codec that silently shortens a name or a mac address hides caller bugs; the
  // gateway's policy functions already validate lengths, so the migration only needs to keep that check.
  PacketCodec pc;
  const CodecSet np = CodecSet::build(migrated_catalog(pc), pc.version());
  DecodedPacket p;
  p.name = "CA_LOGIN";
  p.fields["username"] = std::string(40, 'u');
  const Bytes g = pc.encode(p);
  CHECK(g.size() == 55 && g[6 + 23] == 0 && g[6 + 22] == 'u');   // truncated to 23 + NUL
  bool refused = false;
  try { (void)np.encode(p); } catch (const CodecError& e) { refused = e.code == CodecErrc::kBadValue; }
  CHECK(refused);
}

TEST("CAP-E02", gateway_accepts_nothing_netproto_refuses_in_the_login_phase_framing) {
  // the C2S frame tables are the same set: gateway's PacketCodec::set() is a netproto CodecSet over its own catalog
  PacketCodec pc;
  const CodecSet np = CodecSet::build(migrated_catalog(pc), pc.version());
  CHECK(pc.set().frames().size() == np.frames().size());
  for (const auto& [op, e] : pc.set().frames()) {
    CHECK(np.frames().count(op) == 1);
    CHECK(np.frames().at(op).name == e.name && np.frames().at(op).gate == e.gate);
  }
  // and the stubs are gone: the gateway's own CodecSet member now decodes too
  Bytes ack = {0x7d, 0x00};
  CHECK(pc.set().decode(ack).name == "CZ_NOTIFY_ACTORINIT");
}
