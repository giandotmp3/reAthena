#include "fixtures.hpp"
#include "real_catalog.hpp"
#include "harness.hpp"
#include "netproto/connect_check.hpp"

using namespace netproto;

namespace {
ConnectPacket good() { return {2'000'001, 150000, 0xDEAD, 1234, 1}; }
}  // namespace

TEST("BR-MAP-060", valid_connect_packet_passes) {
  CHECK(validate_connect(19, 19, good(), false) == ConnectError::kNone);
}

TEST("BR-MAP-060", already_authed_session_is_refused_without_reply) {
  const auto e = validate_connect(19, 19, good(), true);
  CHECK(e == ConnectError::kAlreadyLoggedIn);
  CHECK(refusal_code(e) == 0);
}

TEST("BR-MAP-060", wrong_length_is_code_1) {
  const auto e = validate_connect(18, 19, good(), false);
  CHECK(e == ConnectError::kWrongLength && refusal_code(e) == 1);
}

TEST("BR-MAP-060", account_id_must_be_in_assigned_range_code_2) {
  auto p = good();
  for (std::int64_t bad : {-1LL, 0LL, 1'999'999LL, 100'000'001LL, 0x1'0000'0000LL}) {
    p.account_id = bad;
    CHECK(refusal_code(validate_connect(19, 19, p, false)) == 2);
  }
  p.account_id = kStartAccountNum; CHECK(validate_connect(19, 19, p, false) == ConnectError::kNone);
  p.account_id = kEndAccountNum;   CHECK(validate_connect(19, 19, p, false) == ConnectError::kNone);
}

TEST("BR-MAP-060", char_id_must_be_positive_code_3) {
  auto p = good();
  p.char_id = 0;  CHECK(refusal_code(validate_connect(19, 19, p, false)) == 3);
  p.char_id = -5; CHECK(refusal_code(validate_connect(19, 19, p, false)) == 3);
}

TEST("BR-MAP-060", sex_must_be_0_or_1_code_6) {
  auto p = good();
  p.sex = 2; CHECK(refusal_code(validate_connect(19, 19, p, false)) == 6);
  p.sex = 0; CHECK(validate_connect(19, 19, p, false) == ConnectError::kNone);
}

TEST("BR-MAP-060", first_failing_check_wins) {
  ConnectPacket p{0, 0, 0, 0, 9};
  CHECK(validate_connect(19, 19, p, false) == ConnectError::kInvalidAccount);
  CHECK(validate_connect(3, 19, p, false) == ConnectError::kWrongLength);
}

TEST("BR-MAP-060", connect_fields_are_read_from_per_version_offsets_of_the_decoded_frame) {
  const auto codecs = CodecSet::build(fx::catalog(), fx::v2021());
  Bytes frame = fx::fixed_frame(0x0436, 19);
  frame[2] = 0x01; frame[3] = 0x00; frame[4] = 0x1E; frame[5] = 0x00;  // account_id = 0x001E0001
  const auto pkt = codecs.decode(frame);
  CHECK(pkt.name == "CZ_ENTER");
  CHECK(std::get<std::int64_t>(pkt.fields.at("account_id")) == 0x001E0001);
}

TEST("BR-MAP-060", connect_packet_is_validated_from_the_real_wire_bytes_of_the_re_client) {
  // 23-byte CZ_ENTER of the RE client: account, char, login_id1, tick at 2, 6, 10, 14; sex at 22
  const CodecSet s = CodecSet::build(generated::packets_re_20211103_catalog(), ClientVersion{20211103, Flavour::Re});
  auto frame = [](std::uint32_t acc, std::uint32_t ch, std::uint8_t sex) {
    Bytes f(23, 0);
    f[0] = 0x36; f[1] = 0x04;
    for (int i = 0; i < 4; ++i) { f[2 + i] = static_cast<std::uint8_t>(acc >> (8 * i)); f[6 + i] = static_cast<std::uint8_t>(ch >> (8 * i)); }
    f[22] = sex;
    return f;
  };
  auto verdict = [&](const Bytes& f, bool authed) {
    const auto p = connect_packet_from(s.decode(f));
    return validate_connect(f.size(), 23, *p, authed);
  };
  CHECK(verdict(frame(2'000'001, 150'001, 1), false) == ConnectError::kNone);
  CHECK(verdict(frame(1'999'999, 150'001, 1), false) == ConnectError::kInvalidAccount);
  CHECK(verdict(frame(2'000'001, 0, 1), false) == ConnectError::kInvalidChar);
  CHECK(verdict(frame(2'000'001, 150'001, 2), false) == ConnectError::kInvalidSex);
  CHECK(verdict(frame(2'000'001, 150'001, 1), true) == ConnectError::kAlreadyLoggedIn);
  DecodedPacket other; other.name = "CZ_REQUEST_TIME";
  CHECK(!connect_packet_from(other));
  DecodedPacket partial; partial.name = "CZ_ENTER"; partial.fields["account_id"] = std::int64_t{1};
  CHECK(!connect_packet_from(partial));
}
