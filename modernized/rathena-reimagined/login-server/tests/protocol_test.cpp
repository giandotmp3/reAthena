// CAP-24 client protocol stubs. Sizes are pinned against src/common/packets.hpp.
#include <gtest/gtest.h>

#include <vector>

#include "rathena/login/protocol/client_handler.hpp"
#include "rathena/login/protocol/client_packets.hpp"
#include "test_support.hpp"

using namespace rathena::login;
using namespace rathena::login::proto;
using namespace rathena::login::test;

namespace {
std::vector<std::uint8_t> ca_login(const std::string& user, const std::string& pass) {
  std::vector<std::uint8_t> v(kCaLoginSize, 0);
  v[0] = 0x64;
  v[2] = 55;  // version
  std::copy(user.begin(), user.end(), v.begin() + 6);
  std::copy(pass.begin(), pass.end(), v.begin() + 30);
  v[54] = 22;
  return v;
}
}  // namespace

TEST(LoginProtocol, CAP24_DecodeCaLogin) {
  const auto r = decode_client_packet(ca_login("player01", "fakepass9"));
  ASSERT_TRUE(r.request);
  const auto& p = std::get<PlainLogin>(*r.request);
  EXPECT_EQ(p.username, "player01");
  EXPECT_EQ(p.password, "fakepass9");
  EXPECT_EQ(p.version, 55u);
  EXPECT_EQ(p.clienttype, 22);
  EXPECT_EQ(r.consumed, 55u);
}

TEST(LoginProtocol, CAP24_IncompleteAndUnknown) {
  auto v = ca_login("a", "b");
  v.resize(20);
  EXPECT_EQ(decode_client_packet(v).error, DecodeError::Incomplete);
  const std::vector<std::uint8_t> unk = {0xEF, 0xBE};
  EXPECT_EQ(decode_client_packet(unk).error, DecodeError::UnknownOpcode);
}

TEST(LoginProtocol, S4_SsoDeclaredLengthBelowHeaderIsRejected) {
  std::vector<std::uint8_t> v(kSsoHeaderSize, 0);
  v[0] = 0x25;
  v[1] = 0x08;
  v[2] = 0;  // declared length 0
  v[3] = 0;
  EXPECT_EQ(decode_client_packet(v).error, DecodeError::BadLength);
  v[2] = static_cast<std::uint8_t>(kSsoHeaderSize);
  EXPECT_TRUE(decode_client_packet(v).request.has_value());
}

TEST(LoginProtocol, CAP24_RefuseLoginLayout) {
  const auto v = encode_refuse_login(6, "2025-10-09 10:00:00");
  ASSERT_EQ(v.size(), kRefuseLoginSize);
#if RATHENA_PACKETVER >= 20120000
  EXPECT_EQ(v[0], 0x3e);
  EXPECT_EQ(v[1], 0x08);
  EXPECT_EQ(v[2], 6);
  EXPECT_EQ(v[6], '2');
#endif
}

TEST(LoginProtocol, CAP24_AcceptLoginLayout) {
  AcceptLogin a;
  a.account_id = 2'000'001;
  a.web_token = "AAAABBBBCCCCDDDD";
  a.servers.push_back({make_ipv4(192, 0, 2, 10), 6121, "fake-world", 3, 0, 0});
  const auto v = encode_accept_login(a);
#if RATHENA_PACKETVER >= 20170315
  EXPECT_EQ(v.size(), 64u + kAcceptServerEntrySize);
#endif
  EXPECT_EQ(v[2] | (v[3] << 8), static_cast<int>(v.size()));
  EXPECT_EQ(v[64], 192);  // network-order IP
}

TEST(LoginProtocol, BR_LOGIN_003_HandlerRefusesChallengeWhenShimOff) {
  LoginConfig cfg;
  SeededRng rng(3);
  LoginClientHandler h(cfg, rng);
  const std::vector<std::uint8_t> req_hash = {0xdb, 0x01};
  const auto out = h.on_bytes(req_hash);
  ASSERT_EQ(out.to_client.size(), 1u);
  EXPECT_EQ(out.to_client[0][0], 0xdc);
  EXPECT_EQ(h.challenge_salt().find('\0'), std::string::npos);  // BR-LOGIN-005
  std::vector<std::uint8_t> login2(kCaLogin2Size, 0);
  login2[0] = 0xdd;
  login2[1] = 0x01;
  const auto r = h.on_bytes(login2);
  ASSERT_EQ(r.to_client.size(), 1u);
  EXPECT_EQ(r.to_client[0][2], 3);  // refused, code 3
}

TEST(LoginProtocol, CAP24_MalformedPacketDisconnects) {
  LoginConfig cfg;
  SeededRng rng(3);
  LoginClientHandler h(cfg, rng);
  const std::vector<std::uint8_t> junk = {0x00, 0x00};
  EXPECT_TRUE(h.on_bytes(junk).disconnect);
}
