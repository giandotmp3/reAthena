#include <stdexcept>

#include "engine/common.hpp"
#include "engine/ecs/registry.hpp"
#include "engine/ecs/schema.hpp"

namespace engine {

EntityId EntityAllocator::create() {
  EntityId id;
  if (!free_list_.empty()) {
    id.index = free_list_.back();
    free_list_.pop_back();
    id.generation = generations_[id.index];
  } else {
    id.index = static_cast<uint32_t>(generations_.size());
    id.generation = 0;
    generations_.push_back(0);
    live_.push_back(0);
  }
  live_[id.index] = 1;
  ++alive_count_;
  return id;
}

bool EntityAllocator::alive(EntityId id) const {
  return id.valid() && id.index < generations_.size() && live_[id.index] &&
         generations_[id.index] == id.generation;
}

bool EntityAllocator::destroy(EntityId id) {
  if (!alive(id)) return false;
  live_[id.index] = 0;
  ++generations_[id.index];
  free_list_.push_back(id.index);
  --alive_count_;
  return true;
}

bool Registry::destroy(EntityId id) {
  if (!alloc_.alive(id)) return false;
  for (auto& [_, p] : pools_) p->remove_entity(id);
  return alloc_.destroy(id);
}

void Registry::defer_destroy(EntityId id) {
  if (alloc_.alive(id)) deferred_.push_back(id);
}

size_t Registry::flush_deferred() {
  // Take the list first: destroy() never calls back, but a caller-owned hook
  // added later might defer again; those land in the next flush.
  std::vector<EntityId> batch;
  batch.swap(deferred_);
  size_t n = 0;
  for (EntityId id : batch)
    if (destroy(id)) ++n;
  return n;
}

namespace {
bool same_shape(const std::vector<FieldDef>& a, const std::vector<FieldDef>& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i)
    if (a[i].name != b[i].name || a[i].type != b[i].type) return false;
  return true;
}
FieldValue default_for(FieldType t) {
  switch (t) {
    case FieldType::Int: return int64_t{0};
    case FieldType::Double: return 0.0;
    case FieldType::String: return std::string();
    case FieldType::Bool: return false;
  }
  return int64_t{0};
}
}  // namespace

SchemaId SchemaStore::register_schema(const std::string& namespaced_id,
                                      std::vector<FieldDef> fields) {
  auto colon = namespaced_id.find(':');
  if (colon == std::string::npos || colon == 0 || colon + 1 == namespaced_id.size())
    throw std::invalid_argument("schema id must be namespaced: " + namespaced_id);
  for (size_t i = 0; i < fields.size(); ++i) {
    if (fields[i].name.empty()) throw std::invalid_argument("empty field name");
    for (size_t j = 0; j < i; ++j)
      if (fields[j].name == fields[i].name)
        throw std::invalid_argument("duplicate field: " + fields[i].name);
  }
  auto it = by_name_.find(namespaced_id);
  if (it != by_name_.end()) {
    if (!same_shape(schemas_[it->second].fields, fields))
      throw std::invalid_argument("schema re-registered with a different shape: " + namespaced_id);
    return it->second;
  }
  SchemaId id = static_cast<SchemaId>(schemas_.size());
  schemas_.push_back({namespaced_id, std::move(fields)});
  by_name_.emplace(namespaced_id, id);
  return id;
}

std::optional<SchemaId> SchemaStore::find(const std::string& namespaced_id) const {
  auto it = by_name_.find(namespaced_id);
  if (it == by_name_.end()) return std::nullopt;
  return it->second;
}

const FieldDef& SchemaStore::field_def(SchemaId s, const std::string& field) const {
  if (s >= schemas_.size()) throw std::out_of_range("unknown schema id");
  for (const FieldDef& f : schemas_[s].fields)
    if (f.name == field) return f;
  throw std::out_of_range("unknown field: " + schemas_[s].id + "." + field);
}

void SchemaStore::set(EntityId e, SchemaId s, const std::string& field, FieldValue v) {
  const FieldDef& def = field_def(s, field);
  if (v.index() != static_cast<size_t>(def.type))  // variant order == FieldType order
    throw std::invalid_argument("type mismatch for field: " + field);
  values_[{key(e), s}][field] = std::move(v);
}

FieldValue SchemaStore::get(EntityId e, SchemaId s, const std::string& field) const {
  const FieldDef& def = field_def(s, field);
  auto it = values_.find({key(e), s});
  if (it != values_.end()) {
    auto f = it->second.find(field);
    if (f != it->second.end()) return f->second;
  }
  return default_for(def.type);
}

void SchemaStore::remove_entity(EntityId e) {
  for (auto it = values_.lower_bound({key(e), 0}); it != values_.end() && it->first.first == key(e);)
    it = values_.erase(it);
}

}  // namespace engine
