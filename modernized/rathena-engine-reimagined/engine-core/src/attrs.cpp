#include "engine/attrs/attrs.hpp"
#include <stdexcept>

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

int64_t AttributeSystem::fold(AttrId attr, const std::vector<Contribution>& cs) const {
  auto it = stages_.find(attr);
  if (it == stages_.end()) return 0;
  int64_t acc = 0;
  for (const AggregationStage& st : it->second)
    acc = st.fn(acc, std::span<const Contribution>(cs.data(), cs.size()));
  return acc;
}

void AttributeSystem::define_stages(AttrId attr, std::vector<AggregationStage> stages) {
  if (attr >= names_.size()) throw std::out_of_range("unknown attribute id");
  stages_[attr] = std::move(stages);
  for (auto& [k, slot] : slots_)
    if (k.second == attr) slot.dirty = true;
}

void AttributeSystem::add_contribution(EntityId e, AttrId attr, Contribution c) {
  if (attr >= names_.size()) throw std::out_of_range("unknown attribute id");
  Slot& s = slots_[{key(e), attr}];
  s.contributions.push_back(std::move(c));
  s.dirty = true;
}

size_t AttributeSystem::remove_source(EntityId e, SourceId source) {
  size_t removed = 0;
  for (auto it = slots_.lower_bound({key(e), 0}); it != slots_.end() && it->first.first == key(e);) {
    auto& cs = it->second.contributions;
    size_t before = cs.size();
    std::erase_if(cs, [&](const Contribution& c) { return c.source == source; });
    if (cs.size() != before) {
      removed += before - cs.size();
      it->second.dirty = true;
    }
    if (cs.empty()) it = slots_.erase(it); else ++it;
  }
  return removed;
}

int64_t AttributeSystem::get(EntityId e, AttrId attr) {
  if (attr >= names_.size()) throw std::out_of_range("unknown attribute id");
  auto it = slots_.find({key(e), attr});
  if (it == slots_.end()) return fold(attr, {});  // no contributions: stages over an empty list
  Slot& s = it->second;
  if (s.dirty) {
    s.cached = fold(attr, s.contributions);  // throws leave the slot dirty
    s.dirty = false;
  }
  return s.cached;
}

int64_t AttributeSystem::recompute_from_scratch(EntityId e, AttrId attr) const {
  auto it = slots_.find({key(e), attr});
  static const std::vector<Contribution> kNone;
  return fold(attr, it == slots_.end() ? kNone : it->second.contributions);
}

void AttributeSystem::remove_entity(EntityId e) {
  for (auto it = slots_.lower_bound({key(e), 0}); it != slots_.end() && it->first.first == key(e);)
    it = slots_.erase(it);
}

}  // namespace engine
