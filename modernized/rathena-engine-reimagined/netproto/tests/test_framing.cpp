#include "fixtures.hpp"
#include "harness.hpp"

using namespace netproto;
using namespace fx;

TEST("BR-INTER-013", zero_declared_length_is_rejected_immediately_S4) {
  const auto r = peek_frame(bytes({0x04, 0x0b, 0x00, 0x00}), table2021());
  CHECK(r.status == FrameStatus::kInvalidLength);
  CHECK(action_for(r.status) == LinkAction::kDisconnect);
}

TEST("BR-INTER-013", declared_length_below_four_or_below_header_is_rejected) {
  const auto t = table2021();
  CHECK(peek_frame(bytes({0x04, 0x0b, 0x03, 0x00}), t).status == FrameStatus::kInvalidLength);
  CHECK(peek_frame(variable_frame(0x0b04, 9), t).status == FrameStatus::kInvalidLength);
  const auto ok = peek_frame(variable_frame(0x0b04, 10), t);
  CHECK(ok.status == FrameStatus::kComplete && ok.length == 10);
}

TEST("BR-INTER-013", invalid_length_detected_before_payload_arrives) {
  // Only the 4-byte header is buffered; must not wait for more data.
  CHECK(peek_frame(bytes({0x04, 0x0b, 0x05, 0x00}), table2021()).status == FrameStatus::kInvalidLength);
}

TEST("BR-INTER-012", variable_packet_size_limits_are_4_to_32768_inclusive) {
  const auto t = table2021();
  CHECK(peek_frame(variable_frame(0x0b04, 32768), t).status == FrameStatus::kComplete);
  CHECK(peek_frame(bytes({0x04, 0x0b, 0x01, 0x80}), t).status == FrameStatus::kInvalidLength);  // 32769
  CHECK(peek_frame(bytes({0x04, 0x0b, 0xFF, 0xFF}), t).status == FrameStatus::kInvalidLength);  // 65535
}

TEST("BR-INTER-012", receive_buffer_is_bounded) {
  CHECK(buffer_within_limit(65536));
  CHECK(!buffer_within_limit(65537));
}

TEST("BR-MAP-059", unknown_opcode_disconnects) {
  const auto r = peek_frame(bytes({0xEE, 0xEE, 0, 0}), table2021());
  CHECK(r.status == FrameStatus::kUnknownOpcode);
  CHECK(action_for(r.status) == LinkAction::kDisconnect);
}

TEST("BR-MAP-059", partial_frames_wait_and_complete_frames_report_length) {
  const auto t = table2021();
  CHECK(peek_frame(bytes({0x85}), t).status == FrameStatus::kNeedMore);
  CHECK(peek_frame(bytes({0x85, 0x00, 0x01}), t).status == FrameStatus::kNeedMore);
  const auto r = peek_frame(fixed_frame(0x0085, 5), t);
  CHECK(r.status == FrameStatus::kComplete && r.length == 5 && r.opcode == 0x0085);
}

TEST("BR-MAP-059", opcode_meaning_is_per_negotiated_version) {
  // 0x0072 is a connect packet on old main, a different packet on RE.
  const auto old_main = CodecSet::build(catalog(), {20100101, Flavour::Main});
  const auto re = CodecSet::build(catalog(), {20120101, Flavour::Re});
  CHECK(*old_main.name_of(0x0072) == "CZ_ENTER_OLD");
  CHECK(*re.name_of(0x0072) == "CZ_USE_ITEM_OLD");
  CHECK(!CodecSet::build(catalog(), v2021()).name_of(0x0072));
}
