#pragma once
#include <cstdint>
#include <functional>
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

class AttributeSystem {
public:
  // Interning is implemented.
  AttrId intern(std::string_view name);
  const std::string& name_of(AttrId id) const;

  // STUBS (EC-ATTR-*).
  void define_stages(AttrId attr, std::vector<AggregationStage> stages);
  void add_contribution(EntityId e, AttrId attr, Contribution c);
  size_t remove_source(EntityId e, SourceId source);  // returns removed count
  int64_t get(EntityId e, AttrId attr);               // incremental, cached
  int64_t recompute_from_scratch(EntityId e, AttrId attr) const;

private:
  std::vector<std::string> names_;
  std::unordered_map<std::string, AttrId> ids_;
};

}  // namespace engine
