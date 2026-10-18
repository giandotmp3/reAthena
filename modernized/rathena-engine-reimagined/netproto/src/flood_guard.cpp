#include "netproto/flood_guard.hpp"

namespace netproto {

ConnectDecision ConnectionFloodGuard::on_connect(std::uint32_t ip, Millis now, bool allowlisted) {
  auto [it, fresh] = history_.try_emplace(ip);
  Entry& e = it->second;
  if (allowlisted) {
    e.last = now;
    return ConnectDecision::kAccept;
  }
  if (e.locked) {
    if (now - e.locked_at < config_.autoreset) return ConnectDecision::kReject;
    e = Entry{};
  }
  if (!fresh && now - e.last > config_.interval) e.burst = 0;
  e.last = now;
  if (e.burst >= config_.count) {
    e.locked = true;
    e.locked_at = now;
    return ConnectDecision::kReject;
  }
  ++e.burst;
  return ConnectDecision::kAccept;
}

bool ConnectionFloodGuard::is_locked(std::uint32_t ip, Millis now) const {
  const auto it = history_.find(ip);
  return it != history_.end() && it->second.locked && now - it->second.locked_at < config_.autoreset;
}

std::size_t ConnectionFloodGuard::prune(Millis now) {
  std::size_t removed = 0;
  for (auto it = history_.begin(); it != history_.end();) {
    const Entry& e = it->second;
    const bool dead = e.locked ? now - e.locked_at >= config_.autoreset
                               : now - e.last > 3 * config_.interval;
    if (dead) { it = history_.erase(it); ++removed; } else { ++it; }
  }
  return removed;
}

}  // namespace netproto
