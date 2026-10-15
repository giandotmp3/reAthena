// Acceptance tests:
//   BR-INTER-001  char -> login handshake 0x2710 / 0x2711 (with version field)
//   BR-LOGIN-025  char-server registration: sex 'S', id < MAX_SERVERS (5), free slot
//   BR-CHAR-030   map-server registration 0x2af8 / 0x2af9: injected credentials, free slot
//
// All credentials below are FAKE test values (see test_support.hpp).
#include <gtest/gtest.h>

#include "rathena/netproto/inter/handshake.hpp"
#include "rathena/netproto/packets/inter_structs.hpp"
#include "test_support.hpp"

namespace rathena::netproto::inter {
namespace {

using test::bytes;
using test::fake_inter_credentials;

CharServerLogin sample_char_login() {
  CharServerLogin p;
  p.userid = "fake-char-acct";
  p.passwd = "fake-char-pass-0000";
  p.version = kProtocolCurrent;
  p.ip = 0x0A000102;  // 10.0.1.2
  p.port = 6121;
  p.server_name = "TestWorld";
  p.type = 0;
  p.new_display = 1;
  return p;
}

MapServerLogin sample_map_login() {
  const auto creds = fake_inter_credentials();
  MapServerLogin p;
  p.userid = creds.userid;
  p.passwd = creds.passwd;
  p.version = kProtocolCurrent;
  p.ip = 0x7F000001;
  p.port = 5121;
  return p;
}

ServerAccountAuth server_account(std::uint32_t id) { return {true, 'S', id}; }

// ---- BR-INTER-001: wire format ----------------------------------------------

TEST(Handshake, BR_INTER_001_CharServerLoginIs86BytesWithLegacyOffsets) {
  const auto enc = encode(sample_char_login());
  ASSERT_TRUE(enc.ok());
  const auto& b = enc.value();
  ASSERT_EQ(b.size(), 86u);
  EXPECT_EQ(b[0], 0x10);
  EXPECT_EQ(b[1], 0x27);
  EXPECT_EQ(b[2], 'f');                       // userid @2
  EXPECT_EQ(b[26], 'f');                      // passwd @26
  EXPECT_EQ(read_u32le(b, 50), kProtocolV1);  // version @50 (legacy: 0)
  EXPECT_EQ(read_u32be(b, 54), 0x0A000102u);  // ip @54, network order
  EXPECT_EQ(read_u16be(b, 58), 6121);         // port @58, network order
  EXPECT_EQ(b[60], 'T');                      // name @60
  EXPECT_EQ(read_u16le(b, 84), 1);            // new @84
}

TEST(Handshake, BR_INTER_001_CharServerLoginRoundTrips) {
  const auto in = sample_char_login();
  const auto dec = decode_char_server_login(encode(in).value());
  ASSERT_TRUE(dec.ok());
  EXPECT_EQ(dec.value().userid, in.userid);
  EXPECT_EQ(dec.value().passwd, in.passwd);
  EXPECT_EQ(dec.value().version, in.version);
  EXPECT_EQ(dec.value().ip, in.ip);
  EXPECT_EQ(dec.value().port, in.port);
  EXPECT_EQ(dec.value().server_name, in.server_name);
  EXPECT_EQ(dec.value().type, in.type);
  EXPECT_EQ(dec.value().new_display, in.new_display);
}

TEST(Handshake, BR_INTER_001_LegacyUnversionedPeerDecodesAsVersionZero) {
  auto legacy = sample_char_login();
  legacy.version = kProtocolLegacy;
  const auto dec = decode_char_server_login(encode(legacy).value());
  ASSERT_TRUE(dec.ok());
  EXPECT_EQ(dec.value().version, kProtocolLegacy);
}

TEST(Handshake, BR_INTER_001_AckIsThreeBytesZeroAcceptedThreeRefused) {
  EXPECT_EQ(encode(ServerLoginAck{kOpCharServerLoginAck, AckCode::kAccepted}), bytes({0x11, 0x27, 0x00}));
  EXPECT_EQ(encode(ServerLoginAck{kOpCharServerLoginAck, AckCode::kRefused}), bytes({0x11, 0x27, 0x03}));
}

TEST(Handshake, BR_INTER_001_AnyNonZeroAckIsARefusal) {
  const auto dec = decode_server_login_ack(bytes({0x11, 0x27, 0x05}), kOpCharServerLoginAck);
  ASSERT_TRUE(dec.ok());
  EXPECT_EQ(dec.value().code, AckCode::kRefused);
  EXPECT_EQ(decode_server_login_ack(bytes({0xf9, 0x2a, 0x00}), kOpCharServerLoginAck).error(),
            CodecError::kWrongOpcode);
}

TEST(Handshake, BR_INTER_001_TruncatedOrWrongOpcodeIsRejected) {
  auto b = encode(sample_char_login()).value();
  b.resize(85);
  EXPECT_EQ(decode_char_server_login(b).error(), CodecError::kTruncated);
  b.resize(86);
  b[0] = 0x11;
  EXPECT_EQ(decode_char_server_login(b).error(), CodecError::kWrongOpcode);
}

TEST(Handshake, BR_INTER_001_CredentialsAreNeverSilentlyTruncated) {
  auto p = sample_char_login();
  p.passwd = std::string(24, 'x');  // does not fit 23 + NUL
  EXPECT_EQ(encode(p).error(), CodecError::kFieldTooLong);
  p.passwd = std::string(23, 'x');
  EXPECT_TRUE(encode(p).ok());
}

TEST(Handshake, BR_INTER_001_DecodedFieldsAreAlwaysTerminated) {
  auto b = encode(sample_char_login()).value();
  for (std::size_t i = 2; i < 26; ++i) b[i] = 'A';  // no NUL in the userid field
  const auto dec = decode_char_server_login(b);
  ASSERT_TRUE(dec.ok());
  EXPECT_EQ(dec.value().userid.size(), 23u);
}

TEST(Handshake, BR_INTER_001_DescribeNeverContainsThePassword) {
  const auto p = sample_char_login();
  const auto text = describe(p);
  EXPECT_EQ(text.find(p.passwd), std::string::npos);
  EXPECT_NE(text.find("[REDACTED]"), std::string::npos);
  EXPECT_NE(text.find("10.0.1.2:6121"), std::string::npos);
}

TEST(Handshake, BR_INTER_001_LegacyVersionRefusedByDefault) {
  auto p = sample_char_login();
  p.version = kProtocolLegacy;
  const ServerSlots slots(kMaxCharServers);
  const auto d = evaluate_char_server_login(p, server_account(0), slots);
  EXPECT_EQ(d.code, AckCode::kRefused);
  EXPECT_EQ(d.reason, RefuseReason::kUnsupportedVersion);
}

TEST(Handshake, BR_INTER_001_OperatorMayAllowLegacyVersionDuringRollout) {
  auto p = sample_char_login();
  p.version = kProtocolLegacy;
  const ServerSlots slots(kMaxCharServers);
  const auto d = evaluate_char_server_login(p, server_account(0), slots, HandshakePolicy{kProtocolLegacy});
  EXPECT_TRUE(d.accepted());
}

TEST(Handshake, BR_INTER_001_UnknownFutureVersionRefused) {
  auto p = sample_char_login();
  p.version = 99;
  const ServerSlots slots(kMaxCharServers);
  EXPECT_EQ(evaluate_char_server_login(p, server_account(0), slots).reason, RefuseReason::kUnsupportedVersion);
}

TEST(Handshake, BR_INTER_001_StructLayoutsMatchCodecSizes) {
  // K1: compile-time layouts and the explicit codec agree.
  EXPECT_EQ(sizeof(packets::PACKET_CHAR_SERVER_LOGIN), kCharServerLoginSize);
  EXPECT_EQ(sizeof(packets::PACKET_MAP_SERVER_LOGIN), kMapServerLoginSize);
  EXPECT_EQ(sizeof(packets::PACKET_SERVER_LOGIN_ACK), kServerLoginAckSize);
}

// ---- BR-LOGIN-025: char-server registration at login ---------------------------

TEST(Handshake, BR_LOGIN_025_ServerAccountWithFreeSlotIsAccepted) {
  const ServerSlots slots(kMaxCharServers);
  const auto d = evaluate_char_server_login(sample_char_login(), server_account(1), slots);
  EXPECT_TRUE(d.accepted());
  ASSERT_TRUE(d.slot.has_value());
  EXPECT_EQ(*d.slot, 1u);  // slot index == account_id
}

TEST(Handshake, BR_LOGIN_025_NonServerAccountIsRefused) {
  const ServerSlots slots(kMaxCharServers);
  const auto d = evaluate_char_server_login(sample_char_login(), ServerAccountAuth{true, 'M', 1}, slots);
  EXPECT_EQ(d.code, AckCode::kRefused);
  EXPECT_EQ(d.reason, RefuseReason::kNotServerAccount);
}

TEST(Handshake, BR_LOGIN_025_AccountIdAtOrAboveMaxServersIsRefused) {
  const ServerSlots slots(kMaxCharServers);
  EXPECT_TRUE(evaluate_char_server_login(sample_char_login(), server_account(4), slots).accepted());
  const auto d = evaluate_char_server_login(sample_char_login(), server_account(5), slots);
  EXPECT_EQ(d.reason, RefuseReason::kServerIdOutOfRange);
}

TEST(Handshake, BR_LOGIN_025_OccupiedSlotIsRefused) {
  ServerSlots slots(kMaxCharServers);
  slots.set_occupied(2, true);
  const auto d = evaluate_char_server_login(sample_char_login(), server_account(2), slots);
  EXPECT_EQ(d.reason, RefuseReason::kSlotOccupied);
}

TEST(Handshake, BR_LOGIN_025_BadCredentialsAreRefused) {
  const ServerSlots slots(kMaxCharServers);
  const auto d = evaluate_char_server_login(sample_char_login(), ServerAccountAuth{false, 'S', 1}, slots);
  EXPECT_EQ(d.code, AckCode::kRefused);
  EXPECT_EQ(d.reason, RefuseReason::kBadCredentials);
}

// ---- BR-CHAR-030: map-server registration at char ------------------------------

TEST(Handshake, BR_CHAR_030_MapServerLoginIs60BytesWithLegacyOffsets) {
  const auto enc = encode(sample_map_login());
  ASSERT_TRUE(enc.ok());
  const auto& b = enc.value();
  ASSERT_EQ(b.size(), 60u);
  EXPECT_EQ(read_u16le(b, 0), kOpMapServerLogin);
  EXPECT_EQ(read_u32le(b, 50), kProtocolV1);
  EXPECT_EQ(read_u32be(b, 54), 0x7F000001u);
  EXPECT_EQ(read_u16be(b, 58), 5121);
  const auto dec = decode_map_server_login(b);
  ASSERT_TRUE(dec.ok());
  EXPECT_EQ(dec.value().userid, sample_map_login().userid);
}

TEST(Handshake, BR_CHAR_030_MatchingInjectedCredentialsAndFreeSlotAccepted) {
  ServerSlots slots(kDefaultMaxMapServers);
  slots.set_occupied(0, true);
  const auto d = evaluate_map_server_login(sample_map_login(), fake_inter_credentials(), slots);
  EXPECT_TRUE(d.accepted());
  ASSERT_TRUE(d.slot.has_value());
  EXPECT_EQ(*d.slot, 1u);
}

TEST(Handshake, BR_CHAR_030_WrongPasswordRefused) {
  auto p = sample_map_login();
  p.passwd = "fake-wrong-pass";
  const auto d = evaluate_map_server_login(p, fake_inter_credentials(), ServerSlots(kDefaultMaxMapServers));
  EXPECT_EQ(d.code, AckCode::kRefused);
  EXPECT_EQ(d.reason, RefuseReason::kBadCredentials);
  EXPECT_EQ(encode(ServerLoginAck{kOpMapServerLoginAck, d.code}), bytes({0xf9, 0x2a, 0x03}));
}

TEST(Handshake, BR_CHAR_030_WrongUseridRefused) {
  auto p = sample_map_login();
  p.userid = "fake-other-user";
  EXPECT_EQ(evaluate_map_server_login(p, fake_inter_credentials(), ServerSlots(kDefaultMaxMapServers)).reason,
            RefuseReason::kBadCredentials);
}

TEST(Handshake, BR_CHAR_030_PasswordPrefixDoesNotMatch) {
  auto p = sample_map_login();
  p.passwd = p.passwd.substr(0, p.passwd.size() - 1);
  EXPECT_FALSE(evaluate_map_server_login(p, fake_inter_credentials(), ServerSlots(kDefaultMaxMapServers)).accepted());
}

TEST(Handshake, BR_CHAR_030_NoFreeSlotRefused) {
  ServerSlots slots(kDefaultMaxMapServers);
  slots.set_occupied(0, true);
  slots.set_occupied(1, true);
  EXPECT_EQ(evaluate_map_server_login(sample_map_login(), fake_inter_credentials(), slots).reason,
            RefuseReason::kNoFreeSlot);
}

TEST(Handshake, BR_CHAR_030_UnconfiguredCredentialsFailClosed) {
  // Legacy allowed an empty password (inter.cpp:52). The rebuild refuses when
  // ${INTER_SERVER_USER}/${INTER_SERVER_PASS} were not provided, even if the
  // peer also sends empty fields.
  MapServerLogin empty;
  empty.version = kProtocolCurrent;
  const auto d = evaluate_map_server_login(empty, InterServerCredentials{}, ServerSlots(kDefaultMaxMapServers));
  EXPECT_EQ(d.reason, RefuseReason::kCredentialsNotConfigured);
  EXPECT_FALSE(
      evaluate_map_server_login(empty, InterServerCredentials{"fake-user", ""}, ServerSlots(kDefaultMaxMapServers))
          .accepted());
}

TEST(Handshake, BR_CHAR_030_LegacyVersionRefusedByDefault) {
  auto p = sample_map_login();
  p.version = kProtocolLegacy;
  EXPECT_EQ(evaluate_map_server_login(p, fake_inter_credentials(), ServerSlots(kDefaultMaxMapServers)).reason,
            RefuseReason::kUnsupportedVersion);
}

TEST(Handshake, BR_CHAR_030_ConstantTimeEqualsSemantics) {
  EXPECT_TRUE(constant_time_equals("abc", "abc"));
  EXPECT_FALSE(constant_time_equals("abc", "abd"));
  EXPECT_FALSE(constant_time_equals("abc", std::string_view("abc\0", 4)));  // length differs
  EXPECT_FALSE(constant_time_equals("abc", "ab"));
  EXPECT_FALSE(constant_time_equals("", "a"));
  EXPECT_TRUE(constant_time_equals("", ""));
}

}  // namespace
}  // namespace rathena::netproto::inter
