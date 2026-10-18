#include "fixtures.hpp"
#include "harness.hpp"

using namespace netproto;

TEST("CAP-E02", pos3_matches_known_wire_vector) {
  const auto b = pack_pos3({150, 200, 5});
  CHECK(b[0] == 0x25 && b[1] == 0x8C && b[2] == 0x85);
  CHECK(unpack_pos3(b) == (Pos3{150, 200, 5}));
}

TEST("CAP-E02", pos3_roundtrips_boundaries) {
  for (std::uint16_t x : {0, 1, 2, 3, 4, 511, 512, 1023})
    for (std::uint16_t y : {0, 1, 15, 16, 512, 1023})
      for (std::uint8_t d : {0, 7, 15}) {
        const Pos3 p{x, y, d};
        CHECK(unpack_pos3(pack_pos3(p)) == p);
      }
}

TEST("CAP-E02", pos3_masks_out_of_range_input_instead_of_overflowing) {
  const auto p = unpack_pos3(pack_pos3({0xFFFF, 0xFFFF, 0xFF}));
  CHECK(p.x == 1023 && p.y == 1023 && p.dir == 15);
}

TEST("CAP-E02", pos6_roundtrips_boundaries) {
  for (std::uint16_t a : {0, 1, 300, 1023})
    for (std::uint16_t b : {0, 2, 777, 1023}) {
      const Pos6 p{a, b, b, a, 3, 12};
      CHECK(unpack_pos6(pack_pos6(p)) == p);
    }
}
