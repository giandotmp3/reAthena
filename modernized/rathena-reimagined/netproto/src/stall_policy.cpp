#include "rathena/netproto/stall_policy.hpp"

namespace rathena::netproto {

StallPolicy StallPolicy::from_config_seconds(std::int64_t seconds) noexcept {
  if (seconds < kMinimum.count()) return StallPolicy(kMinimum);
  return StallPolicy(std::chrono::seconds(seconds));
}

LinkAction StallPolicy::evaluate(LinkKind kind, std::optional<TimePoint> last_recv, TimePoint now) const noexcept {
  if (!last_recv) return LinkAction::kContinue;  // legacy: rdata_tick == 0 is not judged
  const auto silent = now - *last_recv;
  if (silent <= stall_time_) return LinkAction::kContinue;
  if (kind == LinkKind::kClient) return LinkAction::kDisconnect;
  return silent > link_drop_time() ? LinkAction::kDisconnect : LinkAction::kSendPing;
}

}  // namespace rathena::netproto
