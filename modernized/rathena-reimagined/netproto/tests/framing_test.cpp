// Acceptance tests: BR-INTER-012 (packet size limits), BR-INTER-013 (length
// validation, S4 defect fix).
#include <gtest/gtest.h>

#include <stdexcept>
#include <vector>

#include "rathena/netproto/frame.hpp"
#include "rathena/netproto/inter/length_tables.hpp"
#include "test_support.hpp"

namespace rathena::netproto {
namespace {

using test::bytes;
using test::fixed_frame;
using test::variable_frame;

PacketTable client_table() {
  PacketTable t("client", FrameLimits::client());
  t.add(PacketSpec::fixed(0x0064, 55))         // CA_LOGIN
      .add(PacketSpec::variable(0x0825, 4))    // SSO-style variable packet
      .add(PacketSpec::variable(0x0b04, 10));  // variable packet with a 10-byte header
  return t;
}

// ---- BR-INTER-013 ---------------------------------------------------------

TEST(Framing, BR_INTER_013_ZeroDeclaredLengthIsRejectedImmediately) {
  // Legacy accepted len 0 and stalled the login-server waiting for data (S4).
  const auto buf = bytes({0x25, 0x08, 0x00, 0x00});
  const auto r = peek_frame(buf, client_table());
  EXPECT_EQ(r.status, FrameStatus::kInvalidLength);
  EXPECT_EQ(r.opcode, 0x0825);
  EXPECT_EQ(r.length, 0u);
  EXPECT_EQ(action_for(r.status), LinkAction::kDisconnect);
}

TEST(Framing, BR_INTER_013_DeclaredLengthBelowFourIsRejected) {
  const auto buf = bytes({0x25, 0x08, 0x03, 0x00});
  EXPECT_EQ(peek_frame(buf, client_table()).status, FrameStatus::kInvalidLength);
}

TEST(Framing, BR_INTER_013_DeclaredLengthBelowHeaderSizeIsRejected) {
  const auto table = client_table();
  auto short_decl = variable_frame(0x0b04, 9);
  EXPECT_EQ(peek_frame(short_decl, table).status, FrameStatus::kInvalidLength);

  auto exact = variable_frame(0x0b04, 10);
  const auto ok = peek_frame(exact, table);
  EXPECT_EQ(ok.status, FrameStatus::kComplete);
  EXPECT_EQ(ok.length, 10u);
}

TEST(Framing, BR_INTER_013_InvalidLengthIsDetectedBeforePayloadArrives) {
  // Only the 4-byte header is present; a bad length must not wait for more data.
  const auto buf = bytes({0x04, 0x0b, 0x05, 0x00});
  EXPECT_EQ(peek_frame(buf, client_table()).status, FrameStatus::kInvalidLength);
}

TEST(Framing, BR_INTER_013_LoginLinkVariableHeadersAreEnforced) {
  const auto& t = inter::login_from_char_table();
  // 0x272d reads users:u16 at offset 4 -> header is 6 bytes.
  auto bad = variable_frame(0x272d, 4);
  EXPECT_EQ(peek_frame(bad, t).status, FrameStatus::kInvalidLength);
  // 0x2728 reads aid@4 and char_id@8 -> header is 12 bytes.
  auto bad_reg = variable_frame(0x2728, 8);
  EXPECT_EQ(peek_frame(bad_reg, t).status, FrameStatus::kInvalidLength);
  auto good_reg = variable_frame(0x2728, 12);
  EXPECT_EQ(peek_frame(good_reg, t).status, FrameStatus::kComplete);
}

TEST(Framing, BR_INTER_013_PENDING_IntifVariableHeaderSizesAudited) {
  GTEST_SKIP() << "PENDING BR-INTER-013: intif/chrif variable-length entries currently use the generic "
                  "4-byte header; per-opcode header sizes must be audited against the legacy parse "
                  "functions (golden vectors from an instrumented build).";
}

// ---- BR-INTER-012 ---------------------------------------------------------

TEST(Framing, BR_INTER_012_HeaderIsLittleEndian) {
  const auto buf = bytes({0x25, 0x08, 0x08, 0x00, 1, 2, 3, 4});
  const auto r = peek_frame(buf, client_table());
  EXPECT_EQ(r.status, FrameStatus::kComplete);
  EXPECT_EQ(r.opcode, 0x0825);
  EXPECT_EQ(r.length, 8u);
}

TEST(Framing, BR_INTER_012_ClientVariableLengthLowerBoundIsFour) {
  const auto buf = bytes({0x25, 0x08, 0x04, 0x00});
  const auto r = peek_frame(buf, client_table());
  EXPECT_EQ(r.status, FrameStatus::kComplete);
  EXPECT_EQ(r.length, 4u);
}

TEST(Framing, BR_INTER_012_ClientVariableLengthUpperBoundIs32768) {
  const auto table = client_table();
  const auto at_limit = bytes({0x25, 0x08, 0x00, 0x80});  // 32768
  EXPECT_EQ(peek_frame(at_limit, table).status, FrameStatus::kNeedMore);

  const auto over = bytes({0x25, 0x08, 0x01, 0x80});  // 32769
  const auto r = peek_frame(over, table);
  EXPECT_EQ(r.status, FrameStatus::kInvalidLength);
  EXPECT_EQ(r.length, 32769u);
}

TEST(Framing, BR_INTER_012_ServerLinkKeepsLegacyU16Ceiling) {
  PacketTable t("server", FrameLimits::server_link());
  t.add(PacketSpec::variable(0x2b01));
  const auto buf = bytes({0x01, 0x2b, 0xff, 0xff});
  EXPECT_EQ(peek_frame(buf, t).status, FrameStatus::kNeedMore);
}

TEST(Framing, BR_INTER_012_FixedPacketWaitsForFullSize) {
  const auto table = client_table();
  auto partial = fixed_frame(0x0064, 55);
  partial.resize(54);
  EXPECT_EQ(peek_frame(partial, table).status, FrameStatus::kNeedMore);
  partial.push_back(0);
  EXPECT_EQ(peek_frame(partial, table).status, FrameStatus::kComplete);
}

TEST(Framing, BR_INTER_012_SingleByteNeedsMore) {
  const auto buf = bytes({0x64});
  EXPECT_EQ(peek_frame(buf, client_table()).status, FrameStatus::kNeedMore);
}

TEST(Framing, BR_INTER_012_DrainDeliversFramesAndKeepsPartialTail) {
  std::vector<std::uint8_t> stream;
  test::append(stream, variable_frame(0x0825, 6));
  test::append(stream, fixed_frame(0x0064, 55));
  auto tail = variable_frame(0x0825, 20);
  tail.resize(7);
  test::append(stream, tail);

  std::vector<std::uint16_t> seen;
  const auto r = drain_frames(stream, client_table(), [&](std::uint16_t op, ByteView p) {
    seen.push_back(op);
    EXPECT_GE(p.size(), 4u);
  });
  EXPECT_EQ(r.action, LinkAction::kWaitForData);
  EXPECT_EQ(r.frames, 2u);
  EXPECT_EQ(r.consumed, 61u);
  EXPECT_EQ(seen, (std::vector<std::uint16_t>{0x0825, 0x0064}));
}

TEST(Framing, BR_INTER_012_DrainStopsAtInvalidLength) {
  std::vector<std::uint8_t> stream;
  test::append(stream, variable_frame(0x0825, 6));
  test::append(stream, bytes({0x25, 0x08, 0x00, 0x00}));
  test::append(stream, variable_frame(0x0825, 6));
  int delivered = 0;
  const auto r = drain_frames(stream, client_table(), [&](std::uint16_t, ByteView) { ++delivered; });
  EXPECT_EQ(r.action, LinkAction::kDisconnect);
  EXPECT_EQ(delivered, 1);
  ASSERT_TRUE(r.fault.has_value());
  EXPECT_EQ(r.fault->status, FrameStatus::kInvalidLength);
}

TEST(Framing, BR_INTER_012_PacketTableRejectsMalformedSpecs) {
  PacketTable t("t", FrameLimits::client());
  EXPECT_THROW(t.add(PacketSpec::fixed(0x0001, 1)), std::invalid_argument);
  EXPECT_THROW(t.add(PacketSpec::variable(0x0002, 3)), std::invalid_argument);
  t.add(PacketSpec::fixed(0x0003, 2));
  EXPECT_THROW(t.add(PacketSpec::fixed(0x0003, 6)), std::invalid_argument);
}

}  // namespace
}  // namespace rathena::netproto
