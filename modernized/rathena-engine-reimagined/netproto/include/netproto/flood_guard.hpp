#pragma once
// BR-INTER-011: connection-flood guard. Defaults: 5 connections / 3000 ms, then
// lock the address for 600000 ms. The first `count` connections of a burst are
// accepted, the next is rejected and locks the address (legacy accepted count+1;
// the rebuild follows the documented operator semantics). An allow-listed
// address is always accepted. A gap longer than `interval` resets the burst.
#include <cstddef>
#include <cstdint>
#include <unordered_map>

#include "netproto/common.hpp"

namespace netproto {

struct FloodGuardConfig {
  std::uint32_t count = 5;
  Millis interval = 3000;
  Millis autoreset = 600000;
};

enum class ConnectDecision : std::uint8_t { kAccept, kReject };

class ConnectionFloodGuard {
 public:
  explicit ConnectionFloodGuard(FloodGuardConfig c = {}) : config_(c) {}
  [[nodiscard]] ConnectDecision on_connect(std::uint32_t ipv4, Millis now, bool allowlisted = false);
  [[nodiscard]] bool is_locked(std::uint32_t ipv4, Millis now) const;
  /// Forget idle entries (3 x interval) and expired locks. Bounds memory.
  std::size_t prune(Millis now);
  [[nodiscard]] std::size_t tracked() const noexcept { return history_.size(); }

 private:
  struct Entry {
    Millis last = 0;
    std::uint32_t burst = 0;
    bool locked = false;
    Millis locked_at = 0;
  };
  FloodGuardConfig config_;
  std::unordered_map<std::uint32_t, Entry> history_;
};

}  // namespace netproto
