#include "harness.hpp"
#include "netproto/flood_guard.hpp"

using namespace netproto;

namespace { constexpr std::uint32_t kIp = 0x0A000001, kOther = 0x0A000002; }

TEST("BR-INTER-011", first_five_in_a_burst_accepted_sixth_rejected_and_locks) {
  ConnectionFloodGuard g;
  for (int i = 0; i < 5; ++i) CHECK(g.on_connect(kIp, 1000 + i * 100) == ConnectDecision::kAccept);
  CHECK(g.on_connect(kIp, 1500) == ConnectDecision::kReject);
  CHECK(g.is_locked(kIp, 1600));
  CHECK(!g.is_locked(kOther, 1600));
}

TEST("BR-INTER-011", lock_lasts_600_seconds_then_resets) {
  ConnectionFloodGuard g;
  for (int i = 0; i < 6; ++i) (void)g.on_connect(kIp, 1000);
  CHECK(g.on_connect(kIp, 1000 + 599'999) == ConnectDecision::kReject);
  CHECK(g.on_connect(kIp, 1000 + 600'000) == ConnectDecision::kAccept);
}

TEST("BR-INTER-011", gap_longer_than_interval_resets_the_burst) {
  ConnectionFloodGuard g;
  for (int i = 0; i < 5; ++i) CHECK(g.on_connect(kIp, 0) == ConnectDecision::kAccept);
  CHECK(g.on_connect(kIp, 3001) == ConnectDecision::kAccept);
}

TEST("BR-INTER-011", allowlisted_address_is_never_rejected) {
  ConnectionFloodGuard g;
  for (int i = 0; i < 50; ++i) CHECK(g.on_connect(kIp, 0, true) == ConnectDecision::kAccept);
}

TEST("BR-INTER-011", prune_bounds_memory) {
  ConnectionFloodGuard g;
  for (std::uint32_t i = 0; i < 100; ++i) (void)g.on_connect(i, 0);
  CHECK(g.tracked() == 100);
  CHECK(g.prune(3 * 3000 + 1) == 100);
  CHECK(g.tracked() == 0);
}
