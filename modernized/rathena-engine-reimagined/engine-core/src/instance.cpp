#include "engine/instance/instance.hpp"

#include <vector>

#include "engine/common.hpp"

namespace engine {

InstanceManager::~InstanceManager() {
  for (auto& [_, inst] : instances_) {
    if (inst.ttl_timer != kNoTimer) timers_.cancel(inst.ttl_timer);
    for (TimerId t : inst.scoped) timers_.cancel(t);
  }
}

InstanceId InstanceManager::create(MapId source, InstanceOwner owner, Tick ttl_ms) {
  InstanceId id = next_id_++;
  Instance& inst = instances_[id];
  inst.source = source;
  inst.owner = owner;
  if (ttl_ms > 0)
    inst.ttl_timer = timers_.add_once(ttl_ms, [this, id](TimerId, Tick, Tick) {
      auto it = instances_.find(id);
      if (it != instances_.end()) it->second.ttl_timer = kNoTimer;  // already fired
      destroy(id);
    });
  return id;
}

std::optional<MapId> InstanceManager::source_of(InstanceId id) const {
  auto it = instances_.find(id);
  if (it == instances_.end()) return std::nullopt;
  return it->second.source;
}

std::optional<InstanceOwner> InstanceManager::owner_of(InstanceId id) const {
  auto it = instances_.find(id);
  if (it == instances_.end()) return std::nullopt;
  return it->second.owner;
}

void InstanceManager::owner_left(InstanceOwner owner) {
  std::vector<InstanceId> doomed;
  for (auto& [id, inst] : instances_)
    if (inst.owner == owner) doomed.push_back(id);
  for (InstanceId id : doomed) destroy(id);
}

bool InstanceManager::destroy(InstanceId id) {
  auto it = instances_.find(id);
  if (it == instances_.end()) return false;
  Instance inst = std::move(it->second);
  instances_.erase(it);  // erase first: timer cancels cannot observe a half-dead instance
  if (inst.ttl_timer != kNoTimer) timers_.cancel(inst.ttl_timer);
  for (TimerId t : inst.scoped) timers_.cancel(t);
  return true;
}

TimerId InstanceManager::add_scoped_timer(InstanceId id, Tick delay_ms, TimerCallback cb) {
  auto it = instances_.find(id);
  if (it == instances_.end()) return kNoTimer;
  TimerId t = timers_.add_once(delay_ms, [this, id, cb = std::move(cb)](TimerId tid, Tick s, Tick n) {
    auto inst = instances_.find(id);
    if (inst == instances_.end()) return;  // defensive: destroy() cancels these
    inst->second.scoped.erase(tid);
    cb(tid, s, n);
  });
  it->second.scoped.insert(t);
  return t;
}

}  // namespace engine
