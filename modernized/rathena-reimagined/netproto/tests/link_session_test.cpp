// Pending acceptance tests for the LinkSession stub (link_session.hpp).
// Each is skipped with its rule ID until the state machine is implemented;
// the bodies describe the contract to implement.
#include <gtest/gtest.h>

#include "rathena/netproto/link_session.hpp"

namespace rathena::netproto {
namespace {

TEST(LinkSession, BR_INTER_001_PENDING_OnlyHandshakeIsFramedBeforeRegistration) {
  GTEST_SKIP() << "PENDING BR-INTER-001: LinkSession(kLoginAcceptsChar) must frame only 0x2710 before "
                  "mark_registered(); any other opcode yields kDisconnect.";
}

TEST(LinkSession, BR_INTER_007_PENDING_SwitchesToPostAuthTableAfterRegistration) {
  GTEST_SKIP() << "PENDING BR-INTER-007: after mark_registered(), LinkSession(kCharAcceptsMap) frames with "
                  "map_to_char_link_table() and rejects 0x2af8 re-registration.";
}

TEST(LinkSession, BR_INTER_003_PENDING_AccountDataLayoutFollowsNegotiatedVersion) {
  GTEST_SKIP() << "PENDING BR-INTER-003: on the char side of the login link, 0x2717 is framed as 75 bytes "
                  "for a v0 peer and 79 bytes for a v1 peer (account_data_spec(version)).";
}

TEST(LinkSession, BR_INTER_010_PENDING_TickRequestsKeepaliveOnStalledServerLink) {
  GTEST_SKIP() << "PENDING BR-INTER-010: tick() returns kSendPing after stall_time of silence, "
                  "kDisconnect after 2 x stall_time; on_bytes() refreshes the last-receive time.";
}

}  // namespace
}  // namespace rathena::netproto
