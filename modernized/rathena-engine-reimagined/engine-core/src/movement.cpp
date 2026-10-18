#include "engine/movement/movement.hpp"

#include <algorithm>

namespace engine {

MovementSystem::~MovementSystem() {
  for (auto& [_, w] : walks_)
    if (w.timer != kNoTimer) timers_.cancel(w.timer);
}

Tick MovementSystem::step_delay(const Position& at, Point to, const Mover& m) const {
  Tick speed = std::max<int32_t>(m.step_ms, 1);
  bool diagonal = at.x != to.x && at.y != to.y;
  if (diagonal) speed = speed * kCostDiagonal / kCostOrthogonal;  // legacy: speed*14/10
  return speed;
}

void MovementSystem::schedule(EntityId e, Walk& w, Tick delay) {
  w.timer = timers_.add_once(delay, [this, e](TimerId, Tick scheduled, Tick now) {
    on_step(e, scheduled, now);
  });
}

void MovementSystem::finish(EntityId e) {
  if (Mover* m = reg_.get<Mover>(e)) m->walking = false;
  walks_.erase(key(e));
}

bool MovementSystem::replan(EntityId, Walk& w, Point from) {
  PathResult r = path_search(grid_, from, w.dest, config.path_mode, config.path);
  if (!r.found) return false;
  w.path = std::move(r.steps);
  w.pos = 0;
  w.retarget = false;
  return true;
}

WalkResult MovementSystem::request_walk(EntityId e, Point dest) {
  Position* pos = reg_.get<Position>(e);
  Mover* mover = reg_.get<Mover>(e);
  if (!pos || !mover) return {};
  PathResult r = path_search(grid_, {pos->x, pos->y}, dest, config.path_mode, config.path);
  if (!r.found) return {};

  auto it = walks_.find(key(e));
  if (it != walks_.end()) {  // already walking: re-plan at the next cell centre
    it->second.dest = dest;
    it->second.retarget = true;
    return {true};
  }
  if (r.steps.empty()) return {true};  // already at the destination

  Walk w;
  w.path = std::move(r.steps);
  w.dest = dest;
  mover->walking = true;
  Walk& slot = walks_.emplace(key(e), std::move(w)).first->second;
  schedule(e, slot, step_delay(*pos, slot.path[0], *mover));
  return {true};
}

void MovementSystem::stop(EntityId e) {
  auto it = walks_.find(key(e));
  if (it == walks_.end()) return;
  if (it->second.timer != kNoTimer) timers_.cancel(it->second.timer);
  finish(e);
}

void MovementSystem::on_step(EntityId e, Tick scheduled, Tick now) {
  auto it = walks_.find(key(e));
  if (it == walks_.end()) return;
  it->second.timer = kNoTimer;

  Position* pos = reg_.get<Position>(e);
  Mover* mover = reg_.get<Mover>(e);
  if (!pos || !mover) {  // entity destroyed or stripped while walking
    walks_.erase(it);
    return;
  }

  Walk& w = it->second;
  const Point from{pos->x, pos->y};
  const Point to = w.path[w.pos];

  if (!grid_.walkable(to.x, to.y)) {
    // Blocked since planning: search again from here, like unit_walktoxy_sub.
    if (!replan(e, w, from) || w.path.empty()) { finish(e); return; }
    schedule(e, w, step_delay(*pos, w.path[0], *mover));
    return;
  }

  if (hooks_.before_move && hooks_.before_move(e, from, to) == MoveVerdict::Veto) {
    // Hook may have stopped/replaced the walk; only end it if it is still ours.
    if (walks_.count(key(e))) finish(e);
    return;
  }
  if (!walks_.count(key(e)) || !reg_.alive(e)) return;  // hook stopped the walk

  pos = reg_.get<Position>(e);
  if (!pos) { walks_.erase(key(e)); return; }
  pos->x = to.x;
  pos->y = to.y;
  spatial_.move(e, to);
  if (hooks_.moved) hooks_.moved(e, from, to);

  // The hook may have destroyed the entity, stopped the walk or moved it.
  auto again = walks_.find(key(e));
  if (again == walks_.end()) return;
  pos = reg_.get<Position>(e);
  mover = reg_.get<Mover>(e);
  if (!pos || !mover) { walks_.erase(again); return; }
  if (pos->x != to.x || pos->y != to.y) { finish(e); return; }
  Walk& w2 = again->second;
  if (w2.timer != kNoTimer) return;  // hook already re-armed it

  w2.pos++;
  if (w2.retarget && !replan(e, w2, to)) { finish(e); return; }
  if (w2.pos >= w2.path.size()) { finish(e); return; }  // arrived
  Tick delay = step_delay(*pos, w2.path[w2.pos], *mover);
  schedule(e, w2, scheduled + delay - now);  // chained to the scheduled time
}

}  // namespace engine
