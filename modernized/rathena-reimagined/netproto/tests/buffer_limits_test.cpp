// Acceptance tests: BR-INTER-012 (buffer limits; legacy socket.cpp:243-263).
#include <gtest/gtest.h>

#include "rathena/netproto/buffer_limits.hpp"

namespace rathena::netproto {
namespace {

TEST(BufferLimits, BR_INTER_012_ClientWriteQueueCappedAtOneMiB) {
  const BufferLimits limits;
  EXPECT_FALSE(limits.write_exceeds(LinkKind::kClient, 1024 * 1024));
  EXPECT_TRUE(limits.write_exceeds(LinkKind::kClient, 1024 * 1024 + 1));
}

TEST(BufferLimits, BR_INTER_012_ServerLinkWriteQueueIsNotCapped) {
  const BufferLimits limits;
  EXPECT_FALSE(limits.write_exceeds(LinkKind::kServerLink, 64u * 1024 * 1024));
}

TEST(BufferLimits, BR_INTER_012_ReceiveBufferSizesPerLinkKind) {
  const BufferLimits limits;
  EXPECT_EQ(limits.recv_buffer_size(LinkKind::kClient), 2u * 1024);
  EXPECT_EQ(limits.recv_buffer_size(LinkKind::kServerLink), 256u * 1024);
}

TEST(BufferLimits, BR_INTER_012_MaxOutboundClientPacketDependsOnPacketver) {
  EXPECT_EQ(max_outbound_client_packet(20120410), 0x6000u);
  EXPECT_EQ(max_outbound_client_packet(20131222), 0x6000u);
  EXPECT_EQ(max_outbound_client_packet(20131223), 0xFFFFu);
  EXPECT_EQ(max_outbound_client_packet(20211103), 0xFFFFu);  // default PACKETVER
  EXPECT_TRUE(outbound_client_packet_allowed(0x6000, 20120410));
  EXPECT_FALSE(outbound_client_packet_allowed(0x6001, 20120410));
}

}  // namespace
}  // namespace rathena::netproto
