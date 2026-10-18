#pragma once
#include <cstdint>
#include <utility>
#include <vector>

#include "engine/ecs/entity.hpp"

namespace engine {

// Type-erased base so the registry can strip components on destroy.
class PoolBase {
public:
  virtual ~PoolBase() = default;
  virtual bool remove_entity(EntityId id) = 0;
};

// Sparse-set component pool: O(1) add/get/remove, dense contiguous iteration,
// swap-remove on erase.
template <typename T>
class SparseSet final : public PoolBase {
public:
  template <typename... Args>
  T& emplace(EntityId id, Args&&... args) {
    if (T* existing = get(id)) {
      *existing = T(std::forward<Args>(args)...);
      return *existing;
    }
    if (sparse_.size() <= id.index) sparse_.resize(id.index + 1, kEmpty);
    sparse_[id.index] = static_cast<uint32_t>(dense_.size());
    dense_.push_back(id);
    data_.emplace_back(std::forward<Args>(args)...);
    return data_.back();
  }

  bool contains(EntityId id) const { return slot(id) != kEmpty; }

  T* get(EntityId id) {
    uint32_t s = slot(id);
    return s == kEmpty ? nullptr : &data_[s];
  }
  const T* get(EntityId id) const {
    uint32_t s = slot(id);
    return s == kEmpty ? nullptr : &data_[s];
  }

  bool remove_entity(EntityId id) override {
    uint32_t s = slot(id);
    if (s == kEmpty) return false;
    uint32_t last = static_cast<uint32_t>(dense_.size() - 1);
    if (s != last) {
      dense_[s] = dense_[last];
      data_[s] = std::move(data_[last]);
      sparse_[dense_[s].index] = s;
    }
    sparse_[id.index] = kEmpty;
    dense_.pop_back();
    data_.pop_back();
    return true;
  }

  size_t size() const { return dense_.size(); }
  const std::vector<EntityId>& entities() const { return dense_; }

private:
  static constexpr uint32_t kEmpty = 0xFFFFFFFFu;
  uint32_t slot(EntityId id) const {
    if (!id.valid() || id.index >= sparse_.size()) return kEmpty;
    uint32_t s = sparse_[id.index];
    if (s == kEmpty || dense_[s] != id) return kEmpty;  // generation check
    return s;
  }
  std::vector<uint32_t> sparse_;
  std::vector<EntityId> dense_;
  std::vector<T> data_;
};

}  // namespace engine
