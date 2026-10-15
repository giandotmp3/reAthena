#include "rathena/login/protocol/client_packets.hpp"

#include <algorithm>
#include <cstring>

namespace rathena::login::proto {
namespace {

inline constexpr std::size_t kMaxPacketLen = 32768;

std::uint16_t rd16(std::span<const std::uint8_t> b, std::size_t o) {
  return static_cast<std::uint16_t>(b[o] | (b[o + 1] << 8));
}
std::uint32_t rd32(std::span<const std::uint8_t> b, std::size_t o) {
  return std::uint32_t{b[o]} | (std::uint32_t{b[o + 1]} << 8) | (std::uint32_t{b[o + 2]} << 16) |
         (std::uint32_t{b[o + 3]} << 24);
}
std::string rdstr(std::span<const std::uint8_t> b, std::size_t o, std::size_t n) {
  const auto* p = reinterpret_cast<const char*>(b.data() + o);
  return std::string(p, strnlen(p, n));
}
void wr16(std::vector<std::uint8_t>& v, std::uint16_t x) {
  v.push_back(static_cast<std::uint8_t>(x));
  v.push_back(static_cast<std::uint8_t>(x >> 8));
}
void wr32(std::vector<std::uint8_t>& v, std::uint32_t x) {
  for (int i = 0; i < 4; ++i) v.push_back(static_cast<std::uint8_t>(x >> (8 * i)));
}
void wrstr(std::vector<std::uint8_t>& v, const std::string& s, std::size_t n) {
  for (std::size_t i = 0; i < n; ++i) v.push_back(i < s.size() ? static_cast<std::uint8_t>(s[i]) : 0);
}
void patch16(std::vector<std::uint8_t>& v, std::size_t o, std::uint16_t x) {
  v[o] = static_cast<std::uint8_t>(x);
  v[o + 1] = static_cast<std::uint8_t>(x >> 8);
}

DecodeResult incomplete() { return {std::nullopt, DecodeError::Incomplete, 0}; }
DecodeResult fail(DecodeError e) { return {std::nullopt, e, 0}; }

ChallengeLogin decode_challenge(std::uint16_t op, std::span<const std::uint8_t> b) {
  ChallengeLogin c;
  c.opcode = op;
  c.version = rd32(b, 2);
  c.username = rdstr(b, 6, kNameLength);
  std::copy_n(b.begin() + 30, 16, c.digest.begin());
  c.clienttype = b[46];
  return c;
}

}  // namespace

DecodeResult decode_client_packet(std::span<const std::uint8_t> b) {
  if (b.size() < 2) return incomplete();
  const std::uint16_t op = rd16(b, 0);
  switch (op) {
    case CA_LOGIN: {
      if (b.size() < kCaLoginSize) return incomplete();
      PlainLogin p;
      p.version = rd32(b, 2);
      p.username = rdstr(b, 6, kNameLength);
      p.password = rdstr(b, 30, kNameLength);
      p.clienttype = b[54];
      return {ClientRequest{std::move(p)}, std::nullopt, kCaLoginSize};
    }
    case CA_LOGIN2:
      if (b.size() < kCaLogin2Size) return incomplete();
      return {ClientRequest{decode_challenge(op, b)}, std::nullopt, kCaLogin2Size};
    case CA_LOGIN3:
      if (b.size() < kCaLogin3Size) return incomplete();
      return {ClientRequest{decode_challenge(op, b)}, std::nullopt, kCaLogin3Size};
    case CA_LOGIN4:
      if (b.size() < kCaLogin4Size) return incomplete();
      return {ClientRequest{decode_challenge(op, b)}, std::nullopt, kCaLogin4Size};
    case CA_REQ_HASH:
      return {ClientRequest{ReqHash{}}, std::nullopt, kCaReqHashSize};
    case CA_SSO_LOGIN_REQ: {
      if (b.size() < 4) return incomplete();
      const std::size_t len = rd16(b, 2);
      // S4 / BR-INTER-013: declared length must cover the fixed header.
      if (len < kSsoHeaderSize || len > kMaxPacketLen) return fail(DecodeError::BadLength);
      if (b.size() < len) return incomplete();
      SsoLogin s;
      s.version = rd32(b, 4);
      s.clienttype = b[8];
      s.username = rdstr(b, 9, kNameLength);
      s.token = rdstr(b, kSsoHeaderSize, len - kSsoHeaderSize);
      return {ClientRequest{std::move(s)}, std::nullopt, len};
    }
    default:
      return fail(DecodeError::UnknownOpcode);
  }
}

std::vector<std::uint8_t> encode_refuse_login(std::uint32_t code, const std::string& unblock_time) {
  std::vector<std::uint8_t> v;
  v.reserve(kRefuseLoginSize);
  wr16(v, AC_REFUSE_LOGIN);
#if RATHENA_PACKETVER >= 20120000
  wr32(v, code);
#else
  v.push_back(static_cast<std::uint8_t>(code));
#endif
  wrstr(v, unblock_time, 20);
  return v;
}

std::vector<std::uint8_t> encode_ack_hash(const std::string& salt) {
  std::vector<std::uint8_t> v;
  wr16(v, AC_ACK_HASH);
  wr16(v, static_cast<std::uint16_t>(4 + salt.size()));
  for (char c : salt) v.push_back(static_cast<std::uint8_t>(c));
  return v;
}

std::vector<std::uint8_t> encode_notify_ban(NotifyBan reason) {
  std::vector<std::uint8_t> v;
  wr16(v, SC_NOTIFY_BAN);
  v.push_back(static_cast<std::uint8_t>(reason));
  return v;
}

std::vector<std::uint8_t> encode_accept_login(const AcceptLogin& a) {
  std::vector<std::uint8_t> v;
  wr16(v, AC_ACCEPT_LOGIN);
  wr16(v, 0);  // patched below
  wr32(v, a.login_id1);
  wr32(v, a.account_id);
  wr32(v, a.login_id2);
  wr32(v, a.last_ip);
  wrstr(v, a.last_login, 26);
  v.push_back(a.sex == Sex::Female ? 0 : 1);
#if RATHENA_PACKETVER >= 20170315
  wrstr(v, a.web_token, kWebAuthTokenLength + 1);
#endif
  for (const auto& s : a.servers) {
    // Legacy sends the IP in network byte order inside a little-endian u32.
    v.push_back(static_cast<std::uint8_t>(s.ip >> 24));
    v.push_back(static_cast<std::uint8_t>(s.ip >> 16));
    v.push_back(static_cast<std::uint8_t>(s.ip >> 8));
    v.push_back(static_cast<std::uint8_t>(s.ip));
    wr16(v, s.port);
    wrstr(v, s.name, 20);
    wr16(v, s.users);
    wr16(v, s.type);
    wr16(v, s.is_new);
#if RATHENA_PACKETVER >= 20170315
    wrstr(v, "", 128);
#endif
  }
  patch16(v, 2, static_cast<std::uint16_t>(v.size()));
  return v;
}

}  // namespace rathena::login::proto
