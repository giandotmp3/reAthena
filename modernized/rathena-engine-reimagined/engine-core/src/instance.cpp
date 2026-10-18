#include "engine/instance/instance.hpp"

#include "engine/common.hpp"

namespace engine {

InstanceId InstanceManager::create(MapId, InstanceOwner, Tick) { throw NotImplemented("InstanceManager::create"); }
bool InstanceManager::exists(InstanceId) const { throw NotImplemented("InstanceManager::exists"); }
std::optional<MapId> InstanceManager::source_of(InstanceId) const { throw NotImplemented("InstanceManager::source_of"); }
void InstanceManager::owner_left(InstanceOwner) { throw NotImplemented("InstanceManager::owner_left"); }
bool InstanceManager::destroy(InstanceId) { throw NotImplemented("InstanceManager::destroy"); }
TimerId InstanceManager::add_scoped_timer(InstanceId, Tick, TimerCallback) { throw NotImplemented("InstanceManager::add_scoped_timer"); }

}  // namespace engine
