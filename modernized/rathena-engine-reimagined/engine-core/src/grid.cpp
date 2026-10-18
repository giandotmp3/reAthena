#include "engine/grid/grid.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <stdexcept>

namespace engine {

CellFlags DynamicFlagRegistry::register_flag(const std::string& name) {
  auto it = flags_.find(name);
  if (it != flags_.end()) return it->second;
  if (next_ == 0) throw std::length_error("dynamic cell flags exhausted");
  CellFlags f = next_;
  next_ <<= 1;  // wraps to 0 after bit 31
  flags_.emplace(name, f);
  return f;
}

MapGrid::MapGrid(uint16_t width, uint16_t height)
    : w_(width), h_(height), terrain_(size_t(width) * height, 0),
      dynamic_(size_t(width) * height, 0) {}

bool MapGrid::in_bounds(int x, int y) const {
  return x >= 0 && y >= 0 && x < w_ && y < h_;
}

void MapGrid::set_terrain(int x, int y, CellFlags terrain) {
  if (!in_bounds(x, y)) throw std::out_of_range("set_terrain");
  terrain_[idx(x, y)] = terrain & cell::kTerrainMask;
}

CellFlags MapGrid::cell(int x, int y) const {
  if (!in_bounds(x, y)) return 0;
  CellFlags t = terrain_[idx(x, y)];
  if (x == w_ - 1 || y == h_ - 1) t = 0;  // edge quirk: last row/col solid
  return t | dynamic_[idx(x, y)];
}

void MapGrid::set_dynamic(int x, int y, CellFlags f) {
  if (!in_bounds(x, y)) throw std::out_of_range("set_dynamic");
  dynamic_[idx(x, y)] |= (f & ~cell::kTerrainMask);
}

void MapGrid::clear_dynamic(int x, int y, CellFlags f) {
  if (!in_bounds(x, y)) throw std::out_of_range("clear_dynamic");
  dynamic_[idx(x, y)] &= ~f;
}

int distance_chebyshev(Point a, Point b) {
  return std::max(std::abs(a.x - b.x), std::abs(a.y - b.y));
}

int distance_client(Point a, Point b) {
  double dx = a.x - b.x, dy = a.y - b.y;
  return static_cast<int>(std::floor(std::sqrt(dx * dx + dy * dy) - 0.1));
}

void SpatialIndex::insert(EntityId, Point, BucketClass) { throw NotImplemented("SpatialIndex::insert"); }
bool SpatialIndex::remove(EntityId) { throw NotImplemented("SpatialIndex::remove"); }
void SpatialIndex::move(EntityId, Point) { throw NotImplemented("SpatialIndex::move"); }
std::optional<Point> SpatialIndex::position_of(EntityId) const { throw NotImplemented("SpatialIndex::position_of"); }
void SpatialIndex::for_each_in_area(const AreaQuery&, const std::function<void(EntityId)>&) {
  throw NotImplemented("SpatialIndex::for_each_in_area");
}

}  // namespace engine
