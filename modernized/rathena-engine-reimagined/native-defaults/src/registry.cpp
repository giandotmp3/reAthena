#include "native_defaults/registry.hpp"

#include <stdexcept>

namespace nd {

void EffectQueue::push(Effect e) {
  if (applying) throw std::logic_error("EffectQueue::push during apply (re-entrancy guard, BR-RULESET-116)");
  items.push_back(std::move(e));
}

int ElementTable::at(int atk, int def_element, int def_level) const {
  if (atk < 0 || atk >= elements || def_element < 0 || def_element >= elements || def_level < 1 || def_level > levels)
    throw std::out_of_range("ElementTable::at");
  return ratio.at(static_cast<std::size_t>((atk * elements + def_element) * levels + (def_level - 1)));
}

int SizeTable::at(int weapon_type, int size) const {
  if (weapon_type < 0 || weapon_type >= weapon_types || size < 0 || size >= sizes)
    throw std::out_of_range("SizeTable::at");
  return ratio.at(static_cast<std::size_t>(weapon_type * sizes + size));
}

void Registry::register_native(std::unique_ptr<IStage> stage) {
  const StageId id = stage->id();
  if (natives_.count(id)) throw RegistryError("duplicate native stage " + id.str());
  natives_.emplace(id, std::move(stage));
}

void Registry::register_reference(const StageId& id, ReferenceFn fn) {
  references_[id] = std::move(fn);
}

Resolution Registry::resolve(const StageId& id, bool has_script_hook) const {
  const auto n = natives_.find(id);
  const auto r = references_.find(id);
  if (n == natives_.end() && r == references_.end()) throw RegistryError("unknown stage " + id.str());
  if (n != natives_.end() && require_reference_ && r == references_.end())
    throw RegistryError("native stage " + id.str() + " has no Luau reference");
  if (has_script_hook || n == natives_.end()) {
    if (r == references_.end()) throw RegistryError("stage " + id.str() + " is hooked but has no Luau reference");
    return {ResolvedPath::LuauReference, nullptr, &r->second};
  }
  return {ResolvedPath::Native, n->second.get(), nullptr};
}

}  // namespace nd
