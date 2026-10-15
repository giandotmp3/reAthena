#pragma once
// BR-INTER-011: connection-flood guard (legacy common/socket.cpp:1076-1235,
// shipped conf/packet_athena.conf).
//
// Defaults are the *shipped* values (spec 5.6: shipped config = code defaults):
//   ddos_count 5, ddos_interval 3000 ms, ddos_autoreset 600000 ms.
//
// Contract (spec section 4: "5 connections / 3000 ms, then lock for 600 s"):
//   - consecutive connections from one IPv4 address, each within `interval`
//     of the previous one, form a burst
//   - the first `count` connections of a burst are accepted; the next one is
//     rejected and locks the address for `autoreset`
//   - while locked every connection is rejected, unless the address matches an
//     operator allow rule (legacy "allow" accepts even if flagged as DDoS)
//   - a gap longer than `interval` resets the burst
//   - `prune()` forgets idle unlocked entries after 3 x interval and locked
//     entries after the lock expires (memory bound)
//
// Note: legacy accepted count+1 connections before locking (post-increment
// off-by-one). The rebuild follows the documented operator semantics above.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <unordered_map>

#include "rathena/netproto/stall_policy.hpp"

namespace rathena::netproto {

struct FloodGuardConfig {
  std::uint32_t count = 5;
  std::chrono::milliseconds interval{3000};
  std::chrono::milliseconds autoreset{600000};
};

enum class ConnectDecision : std::uint8_t { kAccept, kReject };

class ConnectionFloodGuard {
 public:
  explicit ConnectionFloodGuard(FloodGuardConfig config = {}) : config_(config) {}

  /// `ipv4` in host byte order. `allowlisted` = matched an operator allow rule.
  [[nodiscard]] ConnectDecision on_connect(std::uint32_t ipv4, TimePoint now, bool allowlisted = false);

  [[nodiscard]] bool is_locked(std::uint32_t ipv4, TimePoint now) const;

  /// Periodic cleanup (legacy connect_check_clear timer).
  std::size_t prune(TimePoint now);

  [[nodiscard]] std::size_t tracked() const noexcept { return history_.size(); }
  [[nodiscard]] const FloodGuardConfig& config() const noexcept { return config_; }

 private:
  struct Entry {
    TimePoint last_attempt{};
    std::uint32_t burst = 0;  // connections accepted in the current burst
    bool locked = false;
    TimePoint locked_at{};
  };

  FloodGuardConfig config_;
  std::unordered_map<std::uint32_t, Entry> history_;
};

}  // namespace rathena::netproto
