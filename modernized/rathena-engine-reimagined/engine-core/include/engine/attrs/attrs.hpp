#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "engine/ecs/entity.hpp"

namespace engine {

using AttrId = uint32_t;
using SourceId = uint64_t;

// One contribution to an attribute. `tag` is an opaque channel label chosen
// by the ruleset (the engine never interprets it).
struct Contribution {
  SourceId source = 0;
  std::string tag;
  int64_t value = 0;
};

// A ruleset-supplied aggregation stage: folds the contributions into the
// running accumulator.
struct AggregationStage {
  std::string id;
  std::function<int64_t(int64_t acc, std::span<const Contribution>)> fn;
};

// Contributions per (entity, attribute) are kept in insertion order. The value
// of an attribute is the left fold of its ruleset-supplied stage list over the
// whole contribution list, starting from 0. An attribute with no stage list
// reads 0 (the engine does not guess how to combine anything).
// Entities are keyed by full handle (index + generation): a stale handle never
// sees a later entity's contributions.
class AttributeSystem {
public:
  AttrId intern(std::string_view name);
  const std::string& name_of(AttrId id) const;

  // Replaces the stage list; every cached value of the attribute is dropped.
  void define_stages(AttrId attr, std::vector<AggregationStage> stages);
  void add_contribution(EntityId e, AttrId attr, Contribution c);
  // Removes every contribution of `source` from every attribute of `e`.
  size_t remove_source(EntityId e, SourceId source);  // returns removed count
  // Cached; recomputed only when that (entity, attribute) was touched.
  int64_t get(EntityId e, AttrId attr);
  // Reference implementation: ignores the cache.
  int64_t recompute_from_scratch(EntityId e, AttrId attr) const;
  void remove_entity(EntityId e);  // drop all state of a destroyed entity

private:
  struct Slot {
    std::vector<Contribution> contributions;
    int64_t cached = 0;
    bool dirty = true;
  };
  static uint64_t key(EntityId e) { return (uint64_t(e.generation) << 32) | e.index; }
  int64_t fold(AttrId attr, const std::vector<Contribution>& cs) const;

  std::vector<std::string> names_;
  std::unordered_map<std::string, AttrId> ids_;
  std::unordered_map<AttrId, std::vector<AggregationStage>> stages_;
  std::map<std::pair<uint64_t, AttrId>, Slot> slots_;
};

}  // namespace engine
