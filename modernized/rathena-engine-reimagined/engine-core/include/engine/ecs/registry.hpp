#pragma once
#include <memory>
#include <typeindex>
#include <unordered_map>
#include <vector>

#include "engine/ecs/entity.hpp"
#include "engine/ecs/sparse_set.hpp"

namespace engine {

// Engine-defined components. Fields are mechanism only; meaning comes from
// the ruleset.
struct Position { int32_t x = 0, y = 0; };
struct Facing { uint8_t dir = 0; };
struct Collider { uint32_t flags = 0; };

class Registry {
public:
  EntityId create() { return alloc_.create(); }
  bool alive(EntityId id) const { return alloc_.alive(id); }
  size_t alive_count() const { return alloc_.alive_count(); }

  // Immediate destroy: strips every component, then retires the handle.
  bool destroy(EntityId id);

  template <typename T, typename... Args>
  T& emplace(EntityId id, Args&&... args) {
    return pool<T>().emplace(id, std::forward<Args>(args)...);
  }
  template <typename T>
  T* get(EntityId id) { return alive(id) ? pool<T>().get(id) : nullptr; }
  template <typename T>
  bool remove(EntityId id) { return pool<T>().remove_entity(id); }
  template <typename T>
  SparseSet<T>& pool() {
    auto key = std::type_index(typeid(T));
    auto it = pools_.find(key);
    if (it == pools_.end())
      it = pools_.emplace(key, std::make_unique<SparseSet<T>>()).first;
    return static_cast<SparseSet<T>&>(*it->second);
  }

  // Deferred structural changes: safe to call while iterating a pool.
  // STUB (EC-ECS-003).
  void defer_destroy(EntityId id);
  void flush_deferred();

private:
  EntityAllocator alloc_;
  std::unordered_map<std::type_index, std::unique_ptr<PoolBase>> pools_;
  std::vector<EntityId> deferred_;
};

}  // namespace engine
