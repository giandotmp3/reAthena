#pragma once
// BR-INTER-010: stall timeout (legacy common/socket.cpp:235, 1016-1024, 1314-1317).
//
//   - stall_time defaults to 60 s; configured values below 3 s are raised to 3 s
//   - a session that has never received data is not judged
//   - client link silent for more than stall_time -> disconnect
//   - server link silent for more than stall_time -> send keepalive ping
//     (char->login 0x2719, map->char 0x2b23); silent for more than
//     2 x stall_time -> drop the link (spec 3.3 x-timing link_drop)

#include <chrono>
#include <cstdint>
#include <optional>

#include "rathena/netproto/link_policy.hpp"

namespace rathena::netproto {

using SteadyClock = std::chrono::steady_clock;
using TimePoint = SteadyClock::time_point;

class StallPolicy {
 public:
  static constexpr std::chrono::seconds kDefault{60};
  static constexpr std::chrono::seconds kMinimum{3};

  StallPolicy() = default;

  /// Build from the `stall_time` config value in seconds (clamped to >= 3).
  [[nodiscard]] static StallPolicy from_config_seconds(std::int64_t seconds) noexcept;

  [[nodiscard]] std::chrono::seconds stall_time() const noexcept { return stall_time_; }
  [[nodiscard]] std::chrono::seconds link_drop_time() const noexcept { return stall_time_ * 2; }

  /// Decide what to do with a connection given the time it last received data.
  /// Returns kContinue, kSendPing (server link) or kDisconnect.
  [[nodiscard]] LinkAction evaluate(LinkKind kind, std::optional<TimePoint> last_recv,
                                    TimePoint now) const noexcept;

 private:
  explicit StallPolicy(std::chrono::seconds s) : stall_time_(s) {}
  std::chrono::seconds stall_time_{kDefault};
};

}  // namespace rathena::netproto
