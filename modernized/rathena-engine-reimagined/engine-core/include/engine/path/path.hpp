#pragma once
#include <functional>
#include <vector>

#include "engine/common.hpp"
#include "engine/grid/grid.hpp"

namespace engine {

inline constexpr int kCostOrthogonal = 10;
inline constexpr int kCostDiagonal = 14;

struct PathConfig {
  int max_walkpath = 32;         // engine config, not a rule constant
  int official_walkpath_len = 14;
};

enum class PathMode : uint8_t {
  Standard,
  // Official walkpath rule (legacy unit_walktoxy / unit_walktoxy_sub): a path
  // longer than official_walkpath_len is only accepted when
  // path_search_long(from, to, LineBlock::NoPass) also succeeds.
  OfficialWalkpath,
};

// Which cells stop a straight line, mirroring the two legacy cell checks the
// callers of path_search_long use. Out-of-bounds cells and the last row and
// column follow map_getcellp: they block under NoPass and never under Wall.
enum class LineBlock : uint8_t {
  // Legacy CELL_CHKWALL (ranged attacks, area wall-checks): a cell blocks when
  // it is not Shootable. For every legal terrain combination (floor, cliff,
  // wall) this equals legacy `!walkable && !shootable`; they differ only for
  // "walkable but not shootable", which map data cannot produce.
  Wall,
  // Legacy CELL_CHKNOPASS (official walkpath rule): blocks when not Walkable.
  NoPass,
};

struct PathResult {
  bool found = false;
  std::vector<Point> steps;  // excludes the start, ends at the destination
  int cost = 0;              // sum of 10/14 step costs
};

// A*: Manhattan*10 heuristic (inadmissible on purpose), diagonal 14, no corner
// cutting, legacy neighbour order (see path.cpp), binary-heap tie-breaks and
// the legacy fixed node table: when two distinct cells hash to the same slot
// the search fails (legacy FIXME). The table has max_walkpath^2 slots (rounded
// up to a power of two), so the default config reproduces legacy bit for bit.
// A path longer than max_walkpath steps fails. Start cell is not checked
// (legacy: otherwise units standing in a blocked cell would be stuck); the
// destination must be walkable and in bounds.
PathResult path_search(const MapGrid& grid, Point from, Point to,
                       PathMode mode = PathMode::Standard,
                       const PathConfig& cfg = {});

// True when no blocking cell lies on the (legacy Bresenham-like) line between
// the points. The end cell itself is never tested; the start cell neither.
bool path_search_long(const MapGrid& grid, Point from, Point to,
                      LineBlock block = LineBlock::Wall);

// Knockback: walks `count` cells (capped at max_count, legacy 25) from `from`
// in direction (dx, dy), each clamped to -1..1. When the next cell is blocked
// and halt_on_block is false it slides along the free axis; when both axes are
// free one is chosen with `coin` (legacy rnd_chance(50,100): true zeroes dx).
Point path_blownpos(const MapGrid& grid, Point from, int dx, int dy, int count,
                    bool halt_on_block, const std::function<bool()>& coin,
                    int max_count = 25);

}  // namespace engine
