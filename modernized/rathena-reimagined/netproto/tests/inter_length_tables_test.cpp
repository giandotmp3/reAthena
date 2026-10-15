// Acceptance tests: BR-INTER-007 (map -> char inter packet framing via length table).
#include <gtest/gtest.h>

#include <vector>

#include "rathena/netproto/inter/length_tables.hpp"
#include "test_support.hpp"

namespace rathena::netproto::inter {
namespace {

using test::fixed_frame;
using test::variable_frame;

void expect_fixed(const PacketTable& t, std::uint16_t op, std::uint16_t size) {
  const auto* s = t.find(op);
  ASSERT_NE(s, nullptr) << "opcode 0x" << std::hex << op;
  EXPECT_EQ(s->kind, PacketSpec::Kind::kFixed) << "opcode 0x" << std::hex << op;
  EXPECT_EQ(s->size, size) << "opcode 0x" << std::hex << op;
}

void expect_variable(const PacketTable& t, std::uint16_t op) {
  const auto* s = t.find(op);
  ASSERT_NE(s, nullptr) << "opcode 0x" << std::hex << op;
  EXPECT_EQ(s->kind, PacketSpec::Kind::kVariable) << "opcode 0x" << std::hex << op;
}

TEST(InterLengthTables, BR_INTER_007_IntifFixedLengthsMatchLegacyTable) {
  const auto& t = intif_map_to_char_table();
  expect_fixed(t, 0x3002, 7);
  expect_fixed(t, 0x3005, 13);
  expect_fixed(t, 0x3007, 2 + 4 + 4 + 4 + 24);  // accinfo, NAME_LENGTH 24
  expect_fixed(t, 0x3024, 15 + 24);
  expect_fixed(t, 0x3025, 17 + 16);             // MAP_NAME_LENGTH_EXT 16
  expect_fixed(t, 0x303e, 186);
  expect_fixed(t, 0x304e, 6 + 24);
  expect_fixed(t, 0x3056, 12);                  // itembound retrieve (not auction)
  expect_fixed(t, 0x3064, 16 + 24 + 50);        // achievement reward
  expect_fixed(t, 0x3080, 52);
  expect_fixed(t, 0x30a0, 2);
}

TEST(InterLengthTables, BR_INTER_007_IntifVariableEntriesMatchLegacyTable) {
  const auto& t = intif_map_to_char_table();
  for (std::uint16_t op : {0x3000, 0x3001, 0x3003, 0x3004, 0x3009, 0x3020, 0x3030, 0x3040, 0x3061, 0x3090, 0x30a1}) {
    expect_variable(t, op);
  }
}

TEST(InterLengthTables, BR_INTER_007_IntifUnusedOpcodesAreUnknown) {
  const auto& t = intif_map_to_char_table();
  for (std::uint16_t op : {0x3008, 0x300a, 0x3012, 0x302b, 0x3043, 0x304f, 0x30a4, 0x30b0}) {
    EXPECT_EQ(t.find(op), nullptr) << "opcode 0x" << std::hex << op;
  }
}

TEST(InterLengthTables, BR_INTER_007_ChrifLengthsMatchLegacyTable) {
  const auto& t = chrif_table();
  expect_fixed(t, 0x2af8, 60);
  expect_fixed(t, 0x2af9, 3);
  expect_fixed(t, 0x2b06, 28 + 16);
  expect_fixed(t, 0x2b23, 2);  // keepalive
  expect_fixed(t, 0x2b24, 2);  // keepalive ack
  expect_fixed(t, 0x2b26, 19); // auth handoff
  expect_variable(t, 0x2b01);  // char save
  EXPECT_EQ(t.find(0x2b13), nullptr);
  EXPECT_EQ(t.find(0x2b30), nullptr);
}

TEST(InterLengthTables, BR_INTER_007_MapToCharLinkFramesMixedStream) {
  std::vector<std::uint8_t> stream;
  test::append(stream, fixed_frame(0x2b23, 2));       // chrif keepalive
  test::append(stream, fixed_frame(0x3002, 7));       // intif whisper reply
  test::append(stream, variable_frame(0x3000, 16));   // intif broadcast
  test::append(stream, variable_frame(0x2b01, 300));  // chrif char save

  std::vector<std::uint16_t> ops;
  const auto r = drain_frames(stream, map_to_char_link_table(),
                              [&](std::uint16_t op, ByteView) { ops.push_back(op); });
  EXPECT_EQ(r.action, LinkAction::kContinue);
  EXPECT_EQ(r.consumed, stream.size());
  EXPECT_EQ(ops, (std::vector<std::uint16_t>{0x2b23, 0x3002, 0x3000, 0x2b01}));
}

TEST(InterLengthTables, BR_INTER_007_LoginFromCharTableMatchesLegacyHandlers) {
  const auto& t = login_from_char_table();
  expect_fixed(t, 0x2712, 23);
  expect_fixed(t, 0x2716, 6);
  expect_fixed(t, 0x2719, 2);
  expect_fixed(t, 0x2722, 86);
  expect_fixed(t, 0x2738, 13);
  expect_fixed(t, 0x2742, 15);
  expect_variable(t, 0x2728);
  expect_variable(t, 0x272d);
  EXPECT_EQ(t.size(), 20u);
}

TEST(InterLengthTables, BR_INTER_007_PENDING_GoldenParityWithInstrumentedLegacyBuild) {
  GTEST_SKIP() << "PENDING BR-INTER-007: compare every table entry and recorded map->char traffic against "
                  "golden captures from an instrumented legacy build (architecture section 3.2 step 1).";
}

}  // namespace
}  // namespace rathena::netproto::inter
