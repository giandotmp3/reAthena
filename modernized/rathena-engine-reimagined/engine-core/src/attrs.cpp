#include "engine/attrs/attrs.hpp"
#include "engine/common.hpp"

namespace engine {

AttrId AttributeSystem::intern(std::string_view name) {
  auto it = ids_.find(std::string(name));
  if (it != ids_.end()) return it->second;
  AttrId id = static_cast<AttrId>(names_.size());
  names_.emplace_back(name);
  ids_.emplace(names_.back(), id);
  return id;
}

const std::string& AttributeSystem::name_of(AttrId id) const { return names_.at(id); }

void AttributeSystem::define_stages(AttrId, std::vector<AggregationStage>) {
  throw NotImplemented("AttributeSystem::define_stages");
}
void AttributeSystem::add_contribution(EntityId, AttrId, Contribution) {
  throw NotImplemented("AttributeSystem::add_contribution");
}
size_t AttributeSystem::remove_source(EntityId, SourceId) {
  throw NotImplemented("AttributeSystem::remove_source");
}
int64_t AttributeSystem::get(EntityId, AttrId) {
  throw NotImplemented("AttributeSystem::get");
}
int64_t AttributeSystem::recompute_from_scratch(EntityId, AttrId) const {
  throw NotImplemented("AttributeSystem::recompute_from_scratch");
}

}  // namespace engine
