// Acceptance tests: BR-INTER-011 (connection-flood guard).
// Shipped defaults: 5 connections / 3000 ms, then lock for 600 s.
#include <gtest/gtest.h>

#include <chrono>

#include "rathena/netproto/flood_guard.hpp"
#include "test_support.hpp"

namespace rathena::netproto {
namespace {

using namespace std::chrono_literals;
using test::t0;

constexpr std::uint32_t kIpA = 0xC0A80001;  // 192.168.0.1 (test value)
constexpr std::uint32_t kIpB = 0xC0A80002;

TEST(FloodGuard, BR_INTER_011_DefaultsAreShippedConfigValues) {
  const FloodGuardConfig c;
  EXPECT_EQ(c.count, 5u);
  EXPECT_EQ(c.interval, 3000ms);
  EXPECT_EQ(c.autoreset, 600000ms);
}

TEST(FloodGuard, BR_INTER_011_FiveConnectionsInBurstAcceptedSixthRejected) {
  ConnectionFloodGuard g;
  auto now = t0();
  for (int i = 0; i < 5; ++i) {
    EXPECT_EQ(g.on_connect(kIpA, now), ConnectDecision::kAccept) << "connection " << i + 1;
    now += 100ms;
  }
  EXPECT_EQ(g.on_connect(kIpA, now), ConnectDecision::kReject);
  EXPECT_TRUE(g.is_locked(kIpA, now));
}

TEST(FloodGuard, BR_INTER_011_LockLastsForAutoreset) {
  ConnectionFloodGuard g;
  auto now = t0();
  for (int i = 0; i < 6; ++i) (void)g.on_connect(kIpA, now += 10ms);
  const auto locked_at = now;
  EXPECT_EQ(g.on_connect(kIpA, locked_at + 599s), ConnectDecision::kReject);
  EXPECT_EQ(g.on_connect(kIpA, locked_at + 600s), ConnectDecision::kAccept);
  EXPECT_FALSE(g.is_locked(kIpA, locked_at + 600s));
}

TEST(FloodGuard, BR_INTER_011_GapLongerThanIntervalResetsBurst) {
  ConnectionFloodGuard g;
  auto now = t0();
  for (int i = 0; i < 5; ++i) EXPECT_EQ(g.on_connect(kIpA, now += 10ms), ConnectDecision::kAccept);
  now += 3001ms;
  for (int i = 0; i < 5; ++i) EXPECT_EQ(g.on_connect(kIpA, now += 10ms), ConnectDecision::kAccept);
}

TEST(FloodGuard, BR_INTER_011_IntervalIsMeasuredFromPreviousAttempt) {
  // Legacy: each attempt within ddos_interval of the previous one extends the burst.
  ConnectionFloodGuard g;
  auto now = t0();
  for (int i = 0; i < 5; ++i) EXPECT_EQ(g.on_connect(kIpA, now += 2900ms), ConnectDecision::kAccept);
  EXPECT_EQ(g.on_connect(kIpA, now += 2900ms), ConnectDecision::kReject);
}

TEST(FloodGuard, BR_INTER_011_AllowlistedAddressAcceptedEvenWhenLocked) {
  ConnectionFloodGuard g;
  auto now = t0();
  for (int i = 0; i < 6; ++i) (void)g.on_connect(kIpA, now += 10ms);
  EXPECT_EQ(g.on_connect(kIpA, now += 10ms, /*allowlisted=*/true), ConnectDecision::kAccept);
  EXPECT_EQ(g.on_connect(kIpA, now += 10ms), ConnectDecision::kReject);
}

TEST(FloodGuard, BR_INTER_011_OtherAddressesAreUnaffected) {
  ConnectionFloodGuard g;
  auto now = t0();
  for (int i = 0; i < 6; ++i) (void)g.on_connect(kIpA, now += 10ms);
  EXPECT_EQ(g.on_connect(kIpB, now), ConnectDecision::kAccept);
}

TEST(FloodGuard, BR_INTER_011_PruneBoundsMemory) {
  ConnectionFloodGuard g;
  auto now = t0();
  (void)g.on_connect(kIpB, now);
  for (int i = 0; i < 6; ++i) (void)g.on_connect(kIpA, now += 10ms);
  EXPECT_EQ(g.tracked(), 2u);
  EXPECT_EQ(g.prune(now + 9001ms), 1u);  // idle unlocked entry > 3 x interval
  EXPECT_EQ(g.tracked(), 1u);
  EXPECT_EQ(g.prune(now + 600s), 1u);    // lock expired
  EXPECT_EQ(g.tracked(), 0u);
}

TEST(FloodGuard, BR_INTER_011_CustomConfigIsHonoured) {
  ConnectionFloodGuard g(FloodGuardConfig{2, 1000ms, 5000ms});
  auto now = t0();
  EXPECT_EQ(g.on_connect(kIpA, now), ConnectDecision::kAccept);
  EXPECT_EQ(g.on_connect(kIpA, now += 1ms), ConnectDecision::kAccept);
  EXPECT_EQ(g.on_connect(kIpA, now += 1ms), ConnectDecision::kReject);
  EXPECT_EQ(g.on_connect(kIpA, now + 5000ms), ConnectDecision::kAccept);
}

}  // namespace
}  // namespace rathena::netproto
