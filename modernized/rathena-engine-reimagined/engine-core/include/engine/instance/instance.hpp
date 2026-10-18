#pragma once
#include <cstdint>
#include <optional>

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

// STUBS (EC-INST-*). Mechanism only: a map clone with an owner, a TTL, and
// scoped timers that die with it.
class InstanceManager {
public:
  explicit InstanceManager(TimerQueue& timers) : timers_(timers) {}
  InstanceId create(MapId source, InstanceOwner owner, Tick ttl_ms);
  bool exists(InstanceId id) const;
  std::optional<MapId> source_of(InstanceId id) const;
  void owner_left(InstanceOwner owner);  // destroys instances it owns
  bool destroy(InstanceId id);
  TimerId add_scoped_timer(InstanceId id, Tick delay_ms, TimerCallback cb);

private:
  TimerQueue& timers_;
};

}  // namespace engine
