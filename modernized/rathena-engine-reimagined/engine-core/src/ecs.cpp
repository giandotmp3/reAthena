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

void Registry::defer_destroy(EntityId) { throw NotImplemented("Registry::defer_destroy"); }
void Registry::flush_deferred() { throw NotImplemented("Registry::flush_deferred"); }

SchemaId SchemaStore::register_schema(const std::string&, std::vector<FieldDef>) {
  throw NotImplemented("SchemaStore::register_schema");
}
void SchemaStore::set(EntityId, SchemaId, const std::string&, FieldValue) {
  throw NotImplemented("SchemaStore::set");
}
FieldValue SchemaStore::get(EntityId, SchemaId, const std::string&) const {
  throw NotImplemented("SchemaStore::get");
}

}  // namespace engine
