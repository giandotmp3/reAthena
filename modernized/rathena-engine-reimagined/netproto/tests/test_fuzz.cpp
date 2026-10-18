// Deterministic mutation fuzz (CAP-E02, BR-INTER-012/013): 100,000 mutated frames over the whole catalog
// through the same invariants the libFuzzer target checks (fuzz/decode_invariants.hpp). Run under
// ASan+UBSan (-DNETPROTO_SANITIZE=ON): a crash, an out-of-bounds read or undefined behaviour fails the run.
#include <random>

#include "../fuzz/decode_invariants.hpp"
#include "harness.hpp"
#include "netproto/sim_client.hpp"
#include "randval.hpp"
#include "real_catalog.hpp"

using namespace netproto;
using namespace fx;

namespace {

struct Seeds {
  std::vector<Bytes> frames;   // valid frames of every layout, both directions
};

Seeds make_seeds() {
  Seeds s;
  std::mt19937_64 g(99);
  for (const auto& rp : full_resolved()) {
    const PacketVersion& v = *rp.version;
    for (int it = 0; it < 3; ++it) {
      DecodedPacket p;
      p.name = rp.def->name;
      if (!v.fields_known) {
        p.fields["payload"] = Bytes(v.length.kind == LengthKind::Fixed ? v.length.fixed_size - 2u : v.length.header_size - v.length.prefix_offset - 2u + g() % 20, 7);
      } else {
        p.repeats.assign(v.length.repeat ? static_cast<std::size_t>(it % 3) : 0, {});
        for (const FieldSpec& f : v.fields) {
          if (is_row_field(f)) for (auto& r : p.repeats) r[field_key(f)] = random_value(g, f);
          else p.fields[f.name] = random_value(g, f);
        }
      }
      s.frames.push_back(full_set().encode(p));
    }
  }
  return s;
}

Bytes mutate(const Seeds& s, std::mt19937_64& g) {
  Bytes b = s.frames[g() % s.frames.size()];
  switch (g() % 9) {
    case 0: for (unsigned n = 1 + g() % 4; n--;) b[g() % b.size()] ^= static_cast<std::uint8_t>(1u << (g() % 8)); break;   // bit flips
    case 1: b.resize(g() % (b.size() + 1)); break;                                                                       // truncation
    case 2: for (unsigned n = 1 + g() % 16; n--;) b.push_back(static_cast<std::uint8_t>(g())); break;                    // extension
    case 3: if (b.size() >= 4) { b[2] = static_cast<std::uint8_t>(g()); b[3] = static_cast<std::uint8_t>(g()); } break;   // wrecked length word
    case 4: if (b.size() >= 4) { const std::uint16_t n = static_cast<std::uint16_t>(g() % 64); b[2] = n & 0xFF; b[3] = n >> 8; } break;  // small declared length
    case 5: b[0] = static_cast<std::uint8_t>(g()); if (g() & 1) b[1] = static_cast<std::uint8_t>(g()); break;            // other opcode
    case 6: { const Bytes& o = s.frames[g() % s.frames.size()]; b.insert(b.end(), o.begin(), o.end()); break; }            // two frames glued
    case 7: { b.assign(2 + g() % 200, 0); for (auto& x : b) x = static_cast<std::uint8_t>(g()); b[0] = s.frames[g() % s.frames.size()][0]; b[1] = s.frames[g() % s.frames.size()][1]; break; }  // random body, real opcode
    default: for (auto& x : b) if (g() % 5 == 0) x = static_cast<std::uint8_t>(g()); break;                              // noise
  }
  return b;
}

}  // namespace

TEST("BR-INTER-013", mutation_fuzz_100k_frames_decode_without_crash_or_ub_and_keep_the_invariants) {
  const Seeds seeds = make_seeds();
  CHECK(seeds.frames.size() > 3000);
  const FrameTable c2s = full_set().frames(), s2c = full_set().server_frames();
  std::mt19937_64 g(0xF022);
  std::size_t accepted = 0, refused = 0;
  for (int i = 0; i < 100000; ++i) {
    const Bytes in = mutate(seeds, g);
    netproto::fuzz::check_one(full_set(), c2s, s2c, in);
    DecodedPacket p;
    (full_set().try_decode(in, Direction::ClientToServer, p) == CodecErrc::kOk ||
     full_set().try_decode(in, Direction::ServerToClient, p) == CodecErrc::kOk)
        ? ++accepted : ++refused;
  }
  CHECK(accepted > 5000 && refused > 5000);  // the mutations reach both outcomes
}

TEST("BR-INTER-013", simclient_receive_survives_mutated_server_streams) {
  const Seeds seeds = make_seeds();
  std::mt19937_64 g(0x51D);
  const auto codecs = std::make_shared<const CodecSet>(full_set());
  std::size_t delivered = 0, closed = 0;
  for (int i = 0; i < 4000; ++i) {
    SimClient c(codecs, std::nullopt);
    Bytes stream;
    for (unsigned n = 1 + g() % 4; n--;) { const Bytes m = mutate(seeds, g); stream.insert(stream.end(), m.begin(), m.end()); }
    // deliver in random slices: partial frames must be buffered, never mis-parsed
    std::size_t pos = 0;
    while (pos < stream.size()) {
      const std::size_t n = 1 + g() % 97;
      const std::size_t take = std::min(n, stream.size() - pos);
      delivered += c.receive(ByteView(stream.data() + pos, take)).size();
      pos += take;
    }
    closed += c.closed();
    CHECK(c.buffered() <= 65536);
  }
  CHECK(delivered > 1000 && closed > 100);
}

TEST("BR-INTER-013", arbitrary_bytes_of_every_short_length_decode_to_a_typed_error_or_a_packet) {
  std::mt19937_64 g(31337);
  for (int i = 0; i < 20000; ++i) {
    Bytes b(g() % 40);
    for (auto& x : b) x = static_cast<std::uint8_t>(g());
    DecodedPacket p;
    const CodecErrc e = full_set().try_decode(b, Direction::ClientToServer, p);
    CHECK(e == CodecErrc::kOk || *to_string(e) != '\0');
  }
}
