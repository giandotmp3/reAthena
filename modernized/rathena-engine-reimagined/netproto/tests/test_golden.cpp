// Golden byte vectors taken from the REAL legacy structs (CAP-E02): tests/golden/typed_re_20211103.txt was
// written by tools/legacy/layout_oracle.py, which fills every typed struct of src/map/packets.hpp and
// src/common/packets.hpp member by member with g++ and dumps the object's bytes (positions through the
// legacy WBUFPOS/WBUFPOS2). Here every vector is decoded with the catalog's codec, each field is compared
// with the value that was written, and the value is encoded back to the identical bytes.
// ctest `layout_oracle` regenerates the file with g++ and fails if it differs from the checked-in one.
#include <cstring>
#include <sstream>

#include "harness.hpp"
#include "real_catalog.hpp"

using namespace netproto;
using namespace fx;

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
  std::uint16_t opcode = 0;
  std::size_t size = 0;
  int rows = 0;
  Bytes bytes;
  std::vector<Expect> expect;
};

std::vector<Golden> load() {
  std::vector<Golden> out;
  std::istringstream in(slurp_file("tests/golden/typed_re_20211103.txt"));
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') continue;
    std::istringstream ls(line);
    std::string tag;
    ls >> tag;
    if (tag == "P") {
      Golden g;
      unsigned op;
      ls >> g.name >> op >> g.size >> g.rows;
      g.opcode = static_cast<std::uint16_t>(op);
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

bool matches(const FieldValue& fv, const Expect& e) {
  if (e.kind == "i") return std::get_if<std::int64_t>(&fv) && std::get<std::int64_t>(fv) == std::stoll(e.v.at(0));
  if (e.kind == "s") { const auto h = unhex(e.v.at(0)); return std::get_if<std::string>(&fv) && std::get<std::string>(fv) == std::string(h.begin(), h.end()); }
  if (e.kind == "r") return std::get_if<Bytes>(&fv) && std::get<Bytes>(fv) == unhex(e.v.at(0));
  if (e.kind == "a") {
    const auto* a = std::get_if<std::vector<std::int64_t>>(&fv);
    if (!a || a->size() != std::stoul(e.v.at(0))) return false;
    for (std::size_t i = 0; i < a->size(); ++i) if ((*a)[i] != std::stoll(e.v.at(1 + i))) return false;
    return true;
  }
  if (e.kind == "p3") return std::get_if<Pos3>(&fv) && std::get<Pos3>(fv) == Pos3{static_cast<std::uint16_t>(std::stoi(e.v[0])), static_cast<std::uint16_t>(std::stoi(e.v[1])), static_cast<std::uint8_t>(std::stoi(e.v[2]))};
  if (e.kind == "p6") {
    return std::get_if<Pos6>(&fv) && std::get<Pos6>(fv) == Pos6{static_cast<std::uint16_t>(std::stoi(e.v[0])), static_cast<std::uint16_t>(std::stoi(e.v[1])), static_cast<std::uint16_t>(std::stoi(e.v[2])),
                                                                 static_cast<std::uint16_t>(std::stoi(e.v[3])), static_cast<std::uint8_t>(std::stoi(e.v[4])), static_cast<std::uint8_t>(std::stoi(e.v[5]))};
  }
  if (e.kind == "f") {
    const std::uint32_t bits = static_cast<std::uint32_t>(std::stoul(e.v.at(0), nullptr, 16));
    float f;
    std::memcpy(&f, &bits, 4);
    return std::get_if<double>(&fv) && std::get<double>(fv) == static_cast<double>(f);
  }
  return false;
}

}  // namespace

TEST("CAP-E02", golden_vectors_cover_every_typed_struct_of_the_catalog) {
  const auto gold = load();
  std::size_t typed = 0;
  std::map<std::string, bool> seen;
  for (const auto& g : gold) seen[g.name] = true;
  for (const auto& rp : full_resolved()) {
    const bool is_typed = rp.version->source == "typed-struct" || rp.version->source == "common-struct";
    typed += is_typed;
    CHECK(seen.count(rp.def->name) == is_typed);
  }
  CHECK(typed == 544 && gold.size() == 544);
  std::size_t client = 0, server = 0, fields = 0, rows = 0;
  for (const auto& g : gold) {
    (g.name.rfind("CZ_", 0) == 0 || g.name.rfind("CA_", 0) == 0 || g.name.rfind("CH_", 0) == 0 ? client : server)++;
    fields += g.expect.size();
    rows += static_cast<std::size_t>(g.rows);
  }
  CHECK(client > 100 && server > 300 && fields > 2000 && rows > 50);
}

TEST("CAP-E02", legacy_struct_bytes_decode_to_the_written_values_and_encode_back_identically) {
  const auto gold = load();
  std::size_t checked_fields = 0;
  for (const auto& g : gold) {
    const auto dir = full_set().direction_of(g.name);
    CHECK(dir.has_value());
    CHECK(*full_set().opcode_of(g.name) == g.opcode);
    CHECK(g.bytes.size() == g.size);
    DecodedPacket p;
    try {
      p = full_set().decode(g.bytes, *dir);
    } catch (const CodecError& e) {
      throw CheckFailure(g.name + ": " + e.what());
    }
    CHECK(p.name == g.name);
    CHECK(static_cast<int>(p.repeats.size()) == g.rows || (g.rows == 0 && p.repeats.empty()));
    for (const auto& e : g.expect) {
      const auto& m = e.row < 0 ? p.fields : p.repeats.at(static_cast<std::size_t>(e.row));
      const auto it = m.find(e.field);
      if (it == m.end()) throw CheckFailure(g.name + ": no field " + e.field);
      if (!matches(it->second, e)) throw CheckFailure(g.name + "." + e.field + " differs from the value the legacy struct held");
      ++checked_fields;
    }
    // everything the real struct contains is in the catalog: encoding the decoded values reproduces the bytes
    const Bytes again = full_set().encode(p);
    if (again != g.bytes) throw CheckFailure(g.name + ": encode(decode(legacy bytes)) differs from the legacy bytes");
  }
  CHECK(checked_fields > 2000);
}

TEST("CAP-E02", pos3_and_pos6_match_the_legacy_wbufpos_routines) {
#define POS3(x, y, d, b0, b1, b2, rx, ry, rd)                                                         \
  {                                                                                                   \
    const std::array<std::uint8_t, 3> wire{b0, b1, b2};                                               \
    CHECK(pack_pos3({x, y, d}) == wire);                                                              \
    CHECK(unpack_pos3(wire) == (Pos3{rx, ry, rd}));                                                   \
  }
#define POS6(x0, y0, x1, y1, sx, sy, b0, b1, b2, b3, b4, b5, r0, r1, r2, r3, rsx, rsy)                \
  {                                                                                                   \
    const std::array<std::uint8_t, 6> wire{b0, b1, b2, b3, b4, b5};                                   \
    CHECK(pack_pos6({x0, y0, x1, y1, sx, sy}) == wire);                                               \
    CHECK(unpack_pos6(wire) == (Pos6{r0, r1, r2, r3, rsx, rsy}));                                     \
  }
#include "legacy_ref/pos_vectors.inc"
#undef POS3
#undef POS6
}
