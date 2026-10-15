// Acceptance tests: BR-INTER-010 (stall timeout; legacy socket.cpp:235, 1016-1024).
#include <gtest/gtest.h>

#include <chrono>

#include "rathena/netproto/stall_policy.hpp"
#include "test_support.hpp"

namespace rathena::netproto {
namespace {

using namespace std::chrono_literals;
using test::t0;

TEST(StallPolicy, BR_INTER_010_DefaultStallTimeIs60Seconds) {
  const StallPolicy p;
  EXPECT_EQ(p.stall_time(), 60s);
  EXPECT_EQ(p.link_drop_time(), 120s);
}

TEST(StallPolicy, BR_INTER_010_ConfiguredValueBelowThreeIsRaisedToThree) {
  EXPECT_EQ(StallPolicy::from_config_seconds(0).stall_time(), 3s);
  EXPECT_EQ(StallPolicy::from_config_seconds(-5).stall_time(), 3s);
  EXPECT_EQ(StallPolicy::from_config_seconds(2).stall_time(), 3s);
  EXPECT_EQ(StallPolicy::from_config_seconds(3).stall_time(), 3s);
  EXPECT_EQ(StallPolicy::from_config_seconds(90).stall_time(), 90s);
}

TEST(StallPolicy, BR_INTER_010_ClientSilentPastStallTimeIsDisconnected) {
  const StallPolicy p;
  EXPECT_EQ(p.evaluate(LinkKind::kClient, t0(), t0() + 60s), LinkAction::kContinue);
  EXPECT_EQ(p.evaluate(LinkKind::kClient, t0(), t0() + 61s), LinkAction::kDisconnect);
}

TEST(StallPolicy, BR_INTER_010_StalledServerLinkIsPingedNotDropped) {
  const StallPolicy p;
  EXPECT_EQ(p.evaluate(LinkKind::kServerLink, t0(), t0() + 61s), LinkAction::kSendPing);
  EXPECT_EQ(p.evaluate(LinkKind::kServerLink, t0(), t0() + 120s), LinkAction::kSendPing);
}

TEST(StallPolicy, BR_INTER_010_ServerLinkDroppedAfterTwiceStallTime) {
  const StallPolicy p;
  EXPECT_EQ(p.evaluate(LinkKind::kServerLink, t0(), t0() + 121s), LinkAction::kDisconnect);
}

TEST(StallPolicy, BR_INTER_010_SessionThatNeverReceivedIsNotJudged) {
  const StallPolicy p;
  EXPECT_EQ(p.evaluate(LinkKind::kClient, std::nullopt, t0() + 24h), LinkAction::kContinue);
}

}  // namespace
}  // namespace rathena::netproto
