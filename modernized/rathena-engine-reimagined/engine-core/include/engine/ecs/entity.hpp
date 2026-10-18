#pragma once
#include <cstdint>
#include <vector>

namespace engine {

// Generational entity handle. A handle to a destroyed entity never aliases a
// later entity that reuses the same index.
struct EntityId {
  uint32_t index = 0xFFFFFFFFu;
  uint32_t generation = 0;
  bool valid() const { return index != 0xFFFFFFFFu; }
  friend bool operator==(const EntityId&, const EntityId&) = default;
};
inline constexpr EntityId kNullEntity{};

class EntityAllocator {
public:
  EntityId create();
  bool destroy(EntityId id);  // false if already stale
  bool alive(EntityId id) const;
  size_t alive_count() const { return alive_count_; }

private:
  std::vector<uint32_t> generations_;
  std::vector<uint8_t> live_;
  std::vector<uint32_t> free_list_;
  size_t alive_count_ = 0;
};

}  // namespace engine
