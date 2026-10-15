#include "rathena/netproto/flood_guard.hpp"

namespace rathena::netproto {

ConnectDecision ConnectionFloodGuard::on_connect(std::uint32_t ipv4, TimePoint now, bool allowlisted) {
  auto [it, inserted] = history_.try_emplace(ipv4);
  Entry& e = it->second;

  if (inserted) {
    e.last_attempt = now;
    e.burst = 1;
    return ConnectDecision::kAccept;
  }

  if (e.locked) {
    if (now - e.locked_at < config_.autoreset) {
      return allowlisted ? ConnectDecision::kAccept : ConnectDecision::kReject;
    }
    e = Entry{};  // lock expired: start fresh
    e.last_attempt = now;
    e.burst = 1;
    return ConnectDecision::kAccept;
  }

  if (now - e.last_attempt < config_.interval) {
    e.last_attempt = now;
    if (e.burst >= config_.count) {
      e.locked = true;
      e.locked_at = now;
      return allowlisted ? ConnectDecision::kAccept : ConnectDecision::kReject;
    }
    ++e.burst;
    return ConnectDecision::kAccept;
  }

  // Gap longer than the interval: the burst is over.
  e.last_attempt = now;
  e.burst = 1;
  return ConnectDecision::kAccept;
}

bool ConnectionFloodGuard::is_locked(std::uint32_t ipv4, TimePoint now) const {
  const auto it = history_.find(ipv4);
  return it != history_.end() && it->second.locked && now - it->second.locked_at < config_.autoreset;
}

std::size_t ConnectionFloodGuard::prune(TimePoint now) {
  std::size_t removed = 0;
  for (auto it = history_.begin(); it != history_.end();) {
    const Entry& e = it->second;
    const bool expired = e.locked ? (now - e.locked_at >= config_.autoreset)
                                  : (now - e.last_attempt > config_.interval * 3);
    if (expired) {
      it = history_.erase(it);
      ++removed;
    } else {
      ++it;
    }
  }
  return removed;
}

}  // namespace rathena::netproto
