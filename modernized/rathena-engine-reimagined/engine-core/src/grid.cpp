#include "engine/grid/grid.hpp"

#include "engine/path/path.hpp"

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
  // Legacy distance_client(): truncate(max(0, sqrt(dx^2+dy^2) - 0.1)).
  double dx = a.x - b.x, dy = a.y - b.y;
  double d = std::sqrt(dx * dx + dy * dy) - 0.1;
  if (d < 0) d = 0;
  return static_cast<int>(d);
}

SpatialIndex::SpatialIndex(const MapGrid& grid)
    : grid_(grid),
      bxs_((size_t(grid.width()) + (1u << kBlockShift) - 1) >> kBlockShift),
      bys_((size_t(grid.height()) + (1u << kBlockShift) - 1) >> kBlockShift) {
  for (auto& v : buckets_) v.resize(bxs_ * bys_);
}

bool SpatialIndex::insert(EntityId e, Point p, BucketClass c) {
  if (!e.valid() || !grid_.in_bounds(p.x, p.y)) return false;
  if (!where_.emplace(key(e), Where{p, c}).second) return false;
  bucket(c, bucket_of(p)).push_back(e);
  return true;
}

bool SpatialIndex::remove(EntityId e) {
  auto it = where_.find(key(e));
  if (it == where_.end()) return false;
  auto& b = bucket(it->second.cls, bucket_of(it->second.p));
  b.erase(std::find(b.begin(), b.end(), e));
  where_.erase(it);
  return true;
}

bool SpatialIndex::move(EntityId e, Point to) {
  auto it = where_.find(key(e));
  if (it == where_.end()) return true;
  if (!grid_.in_bounds(to.x, to.y)) return false;
  size_t from_b = bucket_of(it->second.p), to_b = bucket_of(to);
  if (from_b != to_b) {
    auto& b = bucket(it->second.cls, from_b);
    b.erase(std::find(b.begin(), b.end(), e));
    bucket(it->second.cls, to_b).push_back(e);
  }
  it->second.p = to;
  return true;
}

std::optional<Point> SpatialIndex::position_of(EntityId e) const {
  auto it = where_.find(key(e));
  if (it == where_.end()) return std::nullopt;
  return it->second.p;
}

void SpatialIndex::for_each_in_area(const AreaQuery& q,
                                    const std::function<void(EntityId)>& cb) {
  int x0 = std::min(q.x0, q.x1), x1 = std::max(q.x0, q.x1);
  int y0 = std::min(q.y0, q.y1), y1 = std::max(q.y0, q.y1);
  x0 = std::max(x0, 0);
  y0 = std::max(y0, 0);
  x1 = std::min(x1, int(grid_.width()) - 1);
  y1 = std::min(y1, int(grid_.height()) - 1);
  if (x0 > x1 || y0 > y1) return;

  std::vector<EntityId> snapshot;
  for (BucketClass cls : {BucketClass::Primary, BucketClass::Secondary}) {
    for (int by = block_of(y0); by <= block_of(y1); ++by) {
      for (int bx = block_of(x0); bx <= block_of(x1); ++bx) {
        const auto& b = buckets_[size_t(cls)][size_t(bx) + size_t(by) * bxs_];
        for (auto it = b.rbegin(); it != b.rend(); ++it) {
          const Where& w = where_.at(key(*it));
          if (w.p.x < x0 || w.p.x > x1 || w.p.y < y0 || w.p.y > y1) continue;
          if (q.wall_check_from &&
              !path_search_long(grid_, *q.wall_check_from, w.p, LineBlock::Wall))
            continue;
          snapshot.push_back(*it);
        }
      }
    }
  }
  for (EntityId e : snapshot)
    if (where_.count(key(e))) cb(e);
}

}  // namespace engine
