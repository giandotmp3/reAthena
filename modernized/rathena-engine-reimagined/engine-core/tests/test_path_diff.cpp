// Differential test: engine::path_search / path_search_long against a trimmed,
// compiled-in copy of the legacy code (tests/legacy_ref/). Random maps use only
// the three legal gat combinations (floor, wall, cliff).
#include <cstdint>
#include <string>

#include "engine/path/path.hpp"
#include "harness.hpp"
#include "legacy_ref/legacy_path_ref.hpp"

using namespace engine;

namespace {
struct Rng {
  uint64_t s;
  explicit Rng(uint64_t seed) : s(seed * 2654435769u + 88172645463325252ull) {}
  uint32_t next() {
    s ^= s << 13; s ^= s >> 7; s ^= s << 17;
    return uint32_t(s >> 16);
  }
  int range(int lo, int hi) { return lo + int(next() % uint32_t(hi - lo + 1)); }  // inclusive
  bool chance(int pct) { return int(next() % 100) < pct; }
};

struct Pair {
  MapGrid grid;
  legacy_ref::Map ref;
};

Pair random_map(Rng& r) {
  int w = r.range(6, 80), h = r.range(6, 60);
  Pair p{MapGrid(uint16_t(w), uint16_t(h)), {}};
  p.ref.xs = w;
  p.ref.ys = h;
  p.ref.cell.resize(size_t(w) * h);
  int wall_pct = r.chance(25) ? 0 : r.range(0, 35);
  int cliff_pct = r.chance(50) ? 0 : r.range(0, 10);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      int roll = r.range(0, 99);
      bool wall = roll < wall_pct, cliff = !wall && roll < wall_pct + cliff_pct;
      CellFlags f = wall ? 0 : cliff ? cell::Shootable : (cell::Walkable | cell::Shootable);
      p.grid.set_terrain(x, y, f);
      p.ref.cell[size_t(y) * w + x] = {!wall && !cliff, !wall};
    }
  return p;
}

int dir_of(int dx, int dy) {
  static const int table[3][3] = {{1, 0, 7}, {2, -1, 6}, {3, 4, 5}};
  return table[-dy + 1][dx + 1];
}

std::vector<int> dirs_of(Point start, const std::vector<Point>& steps) {
  std::vector<int> d;
  Point prev = start;
  for (Point p : steps) { d.push_back(dir_of(p.x - prev.x, p.y - prev.y)); prev = p; }
  return d;
}
}  // namespace

TEST("EC-PATH-010", differential_a_star_matches_legacy_on_random_maps) {
  Rng r(20261018);
  int found = 0, not_found = 0, long_paths = 0, compared = 0;
  for (int m = 0; m < 3000; ++m) {
    Pair mp = random_map(r);
    int w = mp.grid.width(), h = mp.grid.height();
    for (int q = 0; q < 6; ++q) {
      Point a{r.range(0, w - 1), r.range(0, h - 1)}, b;
      if (r.chance(75)) {  // mostly nearby targets so paths exist and get long
        b = {std::min(w - 1, std::max(0, a.x + r.range(-34, 34))),
             std::min(h - 1, std::max(0, a.y + r.range(-34, 34)))};
      } else {
        b = {r.range(-2, w + 1), r.range(-2, h + 1)};  // includes out of bounds
      }
      std::vector<int> ref_dirs;
      bool ref_ok = legacy_ref::path_search(mp.ref, a.x, a.y, b.x, b.y, &ref_dirs);
      PathResult mine = path_search(mp.grid, a, b);
      ++compared;
      if (ref_ok != mine.found || (ref_ok && ref_dirs != dirs_of(a, mine.steps))) {
        throw CheckFailure("A* mismatch on map #" + std::to_string(m) + " " + std::to_string(w) + "x" +
                           std::to_string(h) + " from (" + std::to_string(a.x) + "," + std::to_string(a.y) +
                           ") to (" + std::to_string(b.x) + "," + std::to_string(b.y) +
                           ") legacy=" + std::to_string(ref_ok) + " engine=" + std::to_string(mine.found));
      }
      if (ref_ok) { ++found; if (ref_dirs.size() > 14) ++long_paths; } else ++not_found;

      // Official walkpath composition (unit_walktoxy): len > 14 needs the
      // NOPASS long search too.
      bool ref_official = ref_ok && !(ref_dirs.size() > 14 &&
          !legacy_ref::path_search_long(mp.ref, a.x, a.y, b.x, b.y, legacy_ref::CELL_CHKNOPASS));
      CHECK(path_search(mp.grid, a, b, PathMode::OfficialWalkpath).found == ref_official);
    }
  }
  CHECK(compared == 18000);
  CHECK(found > 3000);       // the comparison is not vacuous
  CHECK(not_found > 1000);
  CHECK(long_paths > 300);   // paths beyond the official 14 cells are covered
}

TEST("EC-PATH-010", differential_path_search_long_matches_legacy) {
  Rng r(7);
  int clear = 0, blocked = 0;
  for (int m = 0; m < 3000; ++m) {
    Pair mp = random_map(r);
    int w = mp.grid.width(), h = mp.grid.height();
    for (int q = 0; q < 10; ++q) {
      Point a{r.range(-2, w + 1), r.range(-2, h + 1)}, b{r.range(-2, w + 1), r.range(-2, h + 1)};
      bool rw = legacy_ref::path_search_long(mp.ref, a.x, a.y, b.x, b.y, legacy_ref::CELL_CHKWALL);
      bool rn = legacy_ref::path_search_long(mp.ref, a.x, a.y, b.x, b.y, legacy_ref::CELL_CHKNOPASS);
      CHECK(path_search_long(mp.grid, a, b, LineBlock::Wall) == rw);
      CHECK(path_search_long(mp.grid, a, b, LineBlock::NoPass) == rn);
      (rw ? clear : blocked)++;
    }
  }
  CHECK(clear > 3000 && blocked > 3000);
}

TEST("EC-PATH-012", hash_collision_fails_search_even_when_a_short_path_exists) {
  // Legacy FIXME: nodes live in a 32*32 table indexed by (x + y*32) & 1023.
  // (1,10) and (33,9) share slot 321. The start's west neighbour is (1,10),
  // and the walk east generates (33,9) as a diagonal: 31 steps, search fails.
  MapGrid g(100, 30);
  for (int y = 0; y < 30; ++y)
    for (int x = 0; x < 100; ++x) g.set_terrain(x, y, cell::Walkable | cell::Shootable);
  CHECK(path_search(g, {2, 10}, {31, 10}).found);   // 29 steps, no collision
  CHECK(!path_search(g, {2, 10}, {33, 10}).found);  // 31 steps, collision
  // The same pair on the legacy copy.
  legacy_ref::Map ref;
  ref.xs = 100; ref.ys = 30; ref.cell.assign(3000, {true, true});
  CHECK(legacy_ref::path_search(ref, 2, 10, 31, 10, nullptr));
  CHECK(!legacy_ref::path_search(ref, 2, 10, 33, 10, nullptr));
  // A larger node table (engine config) removes the collision.
  PathConfig cfg;
  cfg.max_walkpath = 64;
  PathResult big = path_search(g, {2, 10}, {33, 10}, PathMode::Standard, cfg);
  CHECK(big.found && big.steps.size() == 31);
}
