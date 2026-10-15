// Acceptance tests: BR-INTER-003 (account-data packet 0x2717).
// [DEFECT] legacy sends group_id as 1 byte, truncating groups above 255; the
// rebuild widens it (v1) and refuses to truncate for a legacy (v0) peer.
//
// Note: the account-data *invariants* (slot computation, VIP) live in
// rules/session per architecture section 8; this file pins the wire codec.
#include <gtest/gtest.h>

#include "rathena/netproto/inter/account_data.hpp"
#include "rathena/netproto/packets/inter_structs.hpp"

namespace rathena::netproto::inter {
namespace {

AccountData sample() {
  AccountData a;
  a.account_id = 2000001;
  a.email = "player@example.invalid";
  a.expiration_time = 0;
  a.group_id = 99;
  a.char_slots = 9;
  a.birthdate = "2000-01-01";
  a.pincode = "";
  a.pincode_change = 0;
  a.is_vip = false;
  a.char_vip = 6;
  a.char_billing = 0;
  return a;
}

TEST(AccountData, BR_INTER_003_V1CarriesGroupIdAbove255) {
  auto a = sample();
  a.group_id = 1000;
  const auto enc = encode(a, kProtocolV1);
  ASSERT_TRUE(enc.ok());
  const auto dec = decode_account_data(enc.value(), kProtocolV1);
  ASSERT_TRUE(dec.ok());
  EXPECT_EQ(dec.value().group_id, 1000);
  EXPECT_EQ(dec.value(), a);
}

TEST(AccountData, BR_INTER_003_V1LayoutIs79BytesWithWideGroupAtOffset75) {
  auto a = sample();
  a.group_id = 70000;
  const auto b = encode(a, kProtocolV1).value();
  ASSERT_EQ(b.size(), 79u);
  EXPECT_EQ(read_u16le(b, 0), kOpAccountData);
  EXPECT_EQ(read_u32le(b, 75), 70000u);
  EXPECT_EQ(b[50], 255);  // legacy byte saturates, never wraps (70000 & 0xFF == 112)
}

TEST(AccountData, BR_INTER_003_V0LayoutMatchesLegacyOffsets) {
  const auto a = sample();
  const auto b = encode(a, kProtocolLegacy).value();
  ASSERT_EQ(b.size(), 75u);
  EXPECT_EQ(read_u32le(b, 2), 2000001u);  // aid @2
  EXPECT_EQ(b[6], 'p');                   // email @6
  EXPECT_EQ(b[50], 99);                   // group @50 (1 byte)
  EXPECT_EQ(b[51], 9);                    // char_slots @51
  EXPECT_EQ(b[52], '2');                  // birthdate @52
  EXPECT_EQ(b[73], 6);                    // char_vip @73
  const auto dec = decode_account_data(b, kProtocolLegacy);
  ASSERT_TRUE(dec.ok());
  EXPECT_EQ(dec.value(), a);
}

TEST(AccountData, BR_INTER_003_V0RefusesToTruncateGroupAbove255) {
  auto a = sample();
  a.group_id = 1000;  // legacy would have sent 1000 & 0xFF == 232
  const auto enc = encode(a, kProtocolLegacy);
  ASSERT_FALSE(enc.ok());
  EXPECT_EQ(enc.error(), CodecError::kNotRepresentable);
}

TEST(AccountData, BR_INTER_003_FrameSpecFollowsNegotiatedVersion) {
  EXPECT_EQ(account_data_spec(kProtocolLegacy).value().size, 75);
  EXPECT_EQ(account_data_spec(kProtocolV1).value().size, 79);
  EXPECT_EQ(account_data_spec(42).error(), CodecError::kUnsupportedVersion);
  EXPECT_EQ(decode_account_data(std::vector<std::uint8_t>(79, 0), 42).error(), CodecError::kUnsupportedVersion);
}

TEST(AccountData, BR_INTER_003_TruncatedV1PacketIsRejected) {
  auto b = encode(sample(), kProtocolV1).value();
  b.resize(75);  // a v0-sized packet on a v1 link
  EXPECT_EQ(decode_account_data(b, kProtocolV1).error(), CodecError::kTruncated);
}

TEST(AccountData, BR_INTER_003_OverlongStringFieldsAreRejected) {
  auto a = sample();
  a.email = std::string(40, 'e');
  EXPECT_EQ(encode(a, kProtocolV1).error(), CodecError::kFieldTooLong);
  a = sample();
  a.pincode = "12345";
  EXPECT_EQ(encode(a, kProtocolV1).error(), CodecError::kFieldTooLong);
}

TEST(AccountData, BR_INTER_003_StructLayoutsMatchCodecSizes) {
  EXPECT_EQ(sizeof(packets::PACKET_ACCOUNT_DATA_V0), kAccountDataSizeV0);
  EXPECT_EQ(sizeof(packets::PACKET_ACCOUNT_DATA_V1), kAccountDataSizeV1);
}

}  // namespace
}  // namespace rathena::netproto::inter
