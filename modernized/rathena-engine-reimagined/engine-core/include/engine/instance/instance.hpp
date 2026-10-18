#pragma once
#include <cstdint>
#include <map>
#include <optional>
#include <set>

#include "engine/tick/tick.hpp"

namespace engine {

using MapId = uint32_t;
using InstanceId = uint64_t;

// Opaque owner. kind/id meaning belongs to the ruleset.
struct InstanceOwner {
  uint32_t kind = 0;
  uint64_t id = 0;
  friend bool operator==(const InstanceOwner&, const InstanceOwner&) = default;
};

// Mechanism only: a map clone with an owner, a TTL, and scoped timers that die
// with it. Decisions where the spec was silent:
//  * ids are never reused;
//  * ttl_ms <= 0 means "no expiry" (only owner_left/destroy end it); otherwise
//    the instance is destroyed when a timer of ttl_ms fires (due == now counts);
//  * destroying an instance cancels its TTL timer and every scoped timer;
//  * owner_left destroys every instance with that exact owner, lowest id first;
//  * add_scoped_timer on an unknown/destroyed instance returns kNoTimer;
//  * the manager must not outlive the TimerQueue; its destructor cancels all
//    timers it still owns.
class InstanceManager {
public:
  explicit InstanceManager(TimerQueue& timers) : timers_(timers) {}
  InstanceManager(const InstanceManager&) = delete;
  InstanceManager& operator=(const InstanceManager&) = delete;
  ~InstanceManager();

  InstanceId create(MapId source, InstanceOwner owner, Tick ttl_ms);
  bool exists(InstanceId id) const { return instances_.count(id) != 0; }
  std::optional<MapId> source_of(InstanceId id) const;
  std::optional<InstanceOwner> owner_of(InstanceId id) const;
  size_t count() const { return instances_.size(); }
  void owner_left(InstanceOwner owner);  // destroys instances it owns
  bool destroy(InstanceId id);
  TimerId add_scoped_timer(InstanceId id, Tick delay_ms, TimerCallback cb);

private:
  struct Instance {
    MapId source = 0;
    InstanceOwner owner;
    TimerId ttl_timer = kNoTimer;
    std::set<TimerId> scoped;
  };
  TimerQueue& timers_;
  std::map<InstanceId, Instance> instances_;
  InstanceId next_id_ = 1;
};

}  // namespace engine
