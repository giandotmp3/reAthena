#pragma once
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "engine/common.hpp"
#include "engine/ecs/entity.hpp"

namespace engine {

using CellFlags = uint32_t;

// Engine-known mechanism bits. Everything at or above kFirstDynamic is
// registered by rulesets and opaque to the engine.
namespace cell {
inline constexpr CellFlags Walkable = 1u << 0;
inline constexpr CellFlags Shootable = 1u << 1;
inline constexpr CellFlags kTerrainMask = Walkable | Shootable;
inline constexpr CellFlags kFirstDynamic = 1u << 8;
}  // namespace cell

class DynamicFlagRegistry {
public:
  // Idempotent per name; throws std::length_error when bits are exhausted.
  CellFlags register_flag(const std::string& name);

private:
  std::unordered_map<std::string, CellFlags> flags_;
  CellFlags next_ = cell::kFirstDynamic;
};

inline constexpr int kBlockShift = 3;  // 8x8 buckets
inline constexpr int block_of(int v) { return v >> kBlockShift; }

class MapGrid {
public:
  MapGrid(uint16_t width, uint16_t height);
  uint16_t width() const { return w_; }
  uint16_t height() const { return h_; }
  bool in_bounds(int x, int y) const;

  void set_terrain(int x, int y, CellFlags terrain);
  // Effective flags. Out of bounds is 0. Legacy quirk: the last row and last
  // column have their terrain bits masked off (they count as solid); dynamic
  // bits are still reported.
  CellFlags cell(int x, int y) const;
  void set_dynamic(int x, int y, CellFlags f);
  void clear_dynamic(int x, int y, CellFlags f);
  bool has(int x, int y, CellFlags f) const { return (cell(x, y) & f) == f; }
  bool walkable(int x, int y) const { return has(x, y, cell::Walkable); }
  bool shootable(int x, int y) const { return has(x, y, cell::Shootable); }

private:
  size_t idx(int x, int y) const { return size_t(y) * w_ + size_t(x); }
  uint16_t w_, h_;
  std::vector<CellFlags> terrain_;
  std::vector<CellFlags> dynamic_;
};

// Chebyshev distance (rules) and client-visible distance.
int distance_chebyshev(Point a, Point b);
int distance_client(Point a, Point b);  // trunc(max(0, sqrt(dx^2+dy^2) - 0.1))

// Two bucket classes mirror the legacy pass order: Primary first, then
// Secondary. The engine does not know what either class contains.
enum class BucketClass : uint8_t { Primary = 0, Secondary = 1 };

struct AreaQuery {
  int x0, y0, x1, y1;  // inclusive
  // When set, a candidate is kept only if a clear shootable line exists from
  // this origin (path_search_long semantics).
  std::optional<Point> wall_check_from;
  static AreaQuery around(Point c, int range) {
    return {c.x - range, c.y - range, c.x + range, c.y + range, std::nullopt};
  }
};

// Block-bucket spatial index (8x8 cells per bucket, one bucket array per
// BucketClass), reproducing legacy map_addblock / map_delblock / map_moveblock
// / map_foreachinareaV:
//  * insertion is at the head of the bucket list (LIFO iteration);
//  * a move inside the same bucket keeps the list position, a move to another
//    bucket re-inserts at the head of the new one;
//  * an area query collects candidates first (Primary buckets, then Secondary;
//    bucket rows outer, columns inner; each bucket newest first), then calls
//    back for every candidate that is still indexed at call time. Entities
//    inserted during iteration are not visited; entities that moved are still
//    visited (the snapshot is not re-filtered by position), as in legacy.
// Positions must be inside the grid. The engine never interprets the classes.
class SpatialIndex {
public:
  explicit SpatialIndex(const MapGrid& grid);
  // false (and no change) if `e` is already indexed or p is out of bounds.
  bool insert(EntityId e, Point p, BucketClass c);
  bool remove(EntityId e);
  // No-op for an entity that is not indexed (legacy: block not in map).
  // Returns false if `to` is out of bounds (nothing changes).
  bool move(EntityId e, Point to);
  std::optional<Point> position_of(EntityId e) const;
  size_t size() const { return where_.size(); }
  void for_each_in_area(const AreaQuery& q,
                        const std::function<void(EntityId)>& cb);

private:
  struct Where {
    Point p;
    BucketClass cls;
  };
  static uint64_t key(EntityId e) { return (uint64_t(e.generation) << 32) | e.index; }
  size_t bucket_of(Point p) const {
    return size_t(block_of(p.x)) + size_t(block_of(p.y)) * bxs_;
  }
  std::vector<EntityId>& bucket(BucketClass c, size_t b) {
    return buckets_[size_t(c)][b];
  }

  const MapGrid& grid_;
  size_t bxs_, bys_;
  // Each bucket stores oldest first; iteration walks it backwards (LIFO).
  std::vector<std::vector<EntityId>> buckets_[2];
  std::unordered_map<uint64_t, Where> where_;
};

}  // namespace engine
