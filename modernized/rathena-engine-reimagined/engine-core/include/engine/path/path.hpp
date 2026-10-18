#pragma once
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
  // Official walkpath rule: a path longer than official_walkpath_len is only
  // accepted when path_search_long(from, to) also succeeds.
  OfficialWalkpath,
};

struct PathResult {
  bool found = false;
  std::vector<Point> steps;  // excludes the start, ends at the destination
  int cost = 0;              // sum of 10/14 step costs
};

// STUBS (EC-PATH-*).
// A*: Manhattan*10 heuristic (inadmissible on purpose), diagonal 14, no
// corner cutting, fixed neighbour order, hash collision fails the search.
PathResult path_search(const MapGrid& grid, Point from, Point to,
                       PathMode mode = PathMode::Standard,
                       const PathConfig& cfg = {});

// True when a clear line of shootable cells joins the two points.
bool path_search_long(const MapGrid& grid, Point from, Point to);

}  // namespace engine
