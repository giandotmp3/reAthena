// Acceptance tests: BR-INTER-002 (unknown packet from a char server -> disconnect).
#include <gtest/gtest.h>

#include <vector>

#include "rathena/netproto/frame.hpp"
#include "rathena/netproto/inter/handshake.hpp"
#include "rathena/netproto/inter/length_tables.hpp"
#include "rathena/netproto/link_policy.hpp"
#include "test_support.hpp"

namespace rathena::netproto {
namespace {

using test::fixed_frame;

TEST(UnknownPacket, BR_INTER_002_UnknownOpcodeFromCharServerDisconnects) {
  const auto buf = fixed_frame(0x27ff, 8);
  const auto r = peek_frame(buf, inter::login_from_char_table());
  EXPECT_EQ(r.status, FrameStatus::kUnknownPacket);
  EXPECT_EQ(r.opcode, 0x27ff);
  EXPECT_EQ(action_for(r.status), LinkAction::kDisconnect);
}

TEST(UnknownPacket, BR_INTER_002_BytesAfterUnknownPacketAreNeverDelivered) {
  std::vector<std::uint8_t> stream;
  test::append(stream, fixed_frame(0x2719, 2));  // keepalive (known)
  test::append(stream, fixed_frame(0x27fe, 2));  // unknown
  test::append(stream, fixed_frame(0x2719, 2));  // must never be handled
  int handled = 0;
  const auto r = drain_frames(stream, inter::login_from_char_table(), [&](std::uint16_t, ByteView) { ++handled; });
  EXPECT_EQ(handled, 1);
  EXPECT_EQ(r.action, LinkAction::kDisconnect);
  ASSERT_TRUE(r.fault.has_value());
  EXPECT_EQ(r.fault->opcode, 0x27fe);
}

TEST(UnknownPacket, BR_INTER_002_PolicyIsDisconnectForEveryLinkKind) {
  EXPECT_EQ(on_unknown_packet("login<-char", LinkKind::kServerLink, 0x27ff).action, LinkAction::kDisconnect);
  EXPECT_EQ(on_unknown_packet("map<-client", LinkKind::kClient, 0x7fff).action, LinkAction::kDisconnect);
}

TEST(UnknownPacket, BR_INTER_002_ReportNamesOpcodeAndLinkOnly) {
  const auto report = on_unknown_packet("login<-char", LinkKind::kServerLink, 0x27ff);
  const auto msg = report.message();
  EXPECT_NE(msg.find("0x27ff"), std::string::npos);
  EXPECT_NE(msg.find("login<-char"), std::string::npos);
  EXPECT_NE(msg.find("disconnect"), std::string::npos);
}

TEST(UnknownPacket, BR_INTER_002_PreRegistrationOnlyAcceptsHandshake) {
  const auto table = inter::char_server_handshake_table();
  const auto auth_verify = fixed_frame(0x2712, 23);  // valid after registration only
  EXPECT_EQ(peek_frame(auth_verify, table).status, FrameStatus::kUnknownPacket);
  const auto hs = fixed_frame(inter::kOpCharServerLogin, inter::kCharServerLoginSize);
  EXPECT_EQ(peek_frame(hs, table).status, FrameStatus::kComplete);
}

TEST(UnknownPacket, BR_INTER_002_DroppedAuctionOpcodesAreUnknownOnMapLink) {
  // HITL #1: legacy auction family 0x3050-0x3055 is not rebuilt.
  for (std::uint16_t op = 0x3050; op <= 0x3055; ++op) {
    const auto buf = fixed_frame(op, 12);
    EXPECT_EQ(peek_frame(buf, inter::map_to_char_link_table()).status, FrameStatus::kUnknownPacket)
        << "opcode 0x" << std::hex << op;
  }
}

}  // namespace
}  // namespace rathena::netproto
