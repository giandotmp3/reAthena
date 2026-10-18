#include "engine/movement/movement.hpp"

namespace engine {

WalkResult MovementSystem::request_walk(EntityId, Point) { throw NotImplemented("MovementSystem::request_walk"); }
void MovementSystem::stop(EntityId) { throw NotImplemented("MovementSystem::stop"); }

}  // namespace engine
