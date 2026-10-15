#include "rathena/netproto/inter/handshake.hpp"

#include <cstdio>

#include "fixed_string.hpp"

namespace rathena::netproto::inter {

namespace {

constexpr std::size_t kOffUserid = 2;
constexpr std::size_t kOffPasswd = 26;
constexpr std::size_t kOffVersion = 50;
constexpr std::size_t kOffIp = 54;
constexpr std::size_t kOffPort = 58;
constexpr std::size_t kOffName = 60;
constexpr std::size_t kOffReserved = 80;
constexpr std::size_t kOffType = 82;
constexpr std::size_t kOffNew = 84;

bool write_credentials(MutableByteView out, std::string_view userid, std::string_view passwd) {
  return detail::write_fixed(out, kOffUserid, kNameLength, userid) &&
         detail::write_fixed(out, kOffPasswd, kNameLength, passwd);
}

std::string ip_to_string(std::uint32_t ip) {
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%u.%u.%u.%u", (ip >> 24) & 0xFFu, (ip >> 16) & 0xFFu, (ip >> 8) & 0xFFu,
                ip & 0xFFu);
  return buf;
}

HandshakeDecision refuse(RefuseReason r) { return {AckCode::kRefused, r, std::nullopt}; }

}  // namespace

CodecResult<std::vector<std::uint8_t>> encode(const CharServerLogin& p) {
  std::vector<std::uint8_t> out(kCharServerLoginSize, 0);
  MutableByteView b(out);
  write_u16le(b, 0, kOpCharServerLogin);
  if (!write_credentials(b, p.userid, p.passwd)) return CodecError::kFieldTooLong;
  write_u32le(b, kOffVersion, p.version);
  write_u32be(b, kOffIp, p.ip);
  write_u16be(b, kOffPort, p.port);
  if (!detail::write_fixed(b, kOffName, kServerNameLength, p.server_name)) return CodecError::kFieldTooLong;
  write_u16le(b, kOffReserved, 0);
  write_u16le(b, kOffType, p.type);
  write_u16le(b, kOffNew, p.new_display);
  return out;
}

CodecResult<std::vector<std::uint8_t>> encode(const MapServerLogin& p) {
  std::vector<std::uint8_t> out(kMapServerLoginSize, 0);
  MutableByteView b(out);
  write_u16le(b, 0, kOpMapServerLogin);
  if (!write_credentials(b, p.userid, p.passwd)) return CodecError::kFieldTooLong;
  write_u32le(b, kOffVersion, p.version);
  write_u32be(b, kOffIp, p.ip);
  write_u16be(b, kOffPort, p.port);
  return out;
}

std::vector<std::uint8_t> encode(const ServerLoginAck& p) {
  std::vector<std::uint8_t> out(kServerLoginAckSize, 0);
  write_u16le(MutableByteView(out), 0, p.opcode);
  out[2] = static_cast<std::uint8_t>(p.code);
  return out;
}

CodecResult<CharServerLogin> decode_char_server_login(ByteView b) {
  if (b.size() < kCharServerLoginSize) return CodecError::kTruncated;
  if (read_u16le(b, 0) != kOpCharServerLogin) return CodecError::kWrongOpcode;
  CharServerLogin p;
  p.userid = detail::read_fixed(b, kOffUserid, kNameLength);
  p.passwd = detail::read_fixed(b, kOffPasswd, kNameLength);
  p.version = read_u32le(b, kOffVersion);
  p.ip = read_u32be(b, kOffIp);
  p.port = read_u16be(b, kOffPort);
  p.server_name = detail::read_fixed(b, kOffName, kServerNameLength);
  p.type = read_u16le(b, kOffType);
  p.new_display = read_u16le(b, kOffNew);
  return p;
}

CodecResult<MapServerLogin> decode_map_server_login(ByteView b) {
  if (b.size() < kMapServerLoginSize) return CodecError::kTruncated;
  if (read_u16le(b, 0) != kOpMapServerLogin) return CodecError::kWrongOpcode;
  MapServerLogin p;
  p.userid = detail::read_fixed(b, kOffUserid, kNameLength);
  p.passwd = detail::read_fixed(b, kOffPasswd, kNameLength);
  p.version = read_u32le(b, kOffVersion);
  p.ip = read_u32be(b, kOffIp);
  p.port = read_u16be(b, kOffPort);
  return p;
}

CodecResult<ServerLoginAck> decode_server_login_ack(ByteView b, std::uint16_t expected_opcode) {
  if (b.size() < kServerLoginAckSize) return CodecError::kTruncated;
  if (read_u16le(b, 0) != expected_opcode) return CodecError::kWrongOpcode;
  // Any non-zero result is a refusal (legacy treats it as fatal).
  return ServerLoginAck{expected_opcode, b[2] == 0 ? AckCode::kAccepted : AckCode::kRefused};
}

PacketTable char_server_handshake_table() {
  PacketTable t("login<-char (pre-registration)", FrameLimits::server_link());
  t.add(PacketSpec::fixed(kOpCharServerLogin, static_cast<std::uint16_t>(kCharServerLoginSize)));
  return t;
}

PacketTable map_server_handshake_table() {
  PacketTable t("char<-map (pre-registration)", FrameLimits::server_link());
  t.add(PacketSpec::fixed(kOpMapServerLogin, static_cast<std::uint16_t>(kMapServerLoginSize)));
  return t;
}

std::string describe(const CharServerLogin& p) {
  return "char-server '" + p.server_name + "' @ " + ip_to_string(p.ip) + ":" + std::to_string(p.port) +
         " (account '" + p.userid + "', password [REDACTED], proto v" + std::to_string(p.version) + ")";
}

std::string describe(const MapServerLogin& p) {
  return "map-server @ " + ip_to_string(p.ip) + ":" + std::to_string(p.port) + " (account '" + p.userid +
         "', password [REDACTED], proto v" + std::to_string(p.version) + ")";
}

std::optional<std::size_t> ServerSlots::first_free() const noexcept {
  for (std::size_t i = 0; i < occupied_.size(); ++i) {
    if (!occupied_[i]) return i;
  }
  return std::nullopt;
}

bool constant_time_equals(std::string_view a, std::string_view b) noexcept {
  // Compare over the full fixed field width so timing does not leak length.
  unsigned diff = static_cast<unsigned>(a.size() ^ b.size());
  for (std::size_t i = 0; i < kNameLength; ++i) {
    const unsigned ca = i < a.size() ? static_cast<unsigned char>(a[i]) : 0u;
    const unsigned cb = i < b.size() ? static_cast<unsigned char>(b[i]) : 0u;
    diff |= ca ^ cb;
  }
  // Inputs longer than a field never match a decoded field; still compare tails.
  const std::size_t longest = a.size() > b.size() ? a.size() : b.size();
  for (std::size_t i = kNameLength; i < longest; ++i) {
    const unsigned ca = i < a.size() ? static_cast<unsigned char>(a[i]) : 0u;
    const unsigned cb = i < b.size() ? static_cast<unsigned char>(b[i]) : 0u;
    diff |= ca ^ cb;
  }
  return diff == 0;
}

HandshakeDecision evaluate_char_server_login(const CharServerLogin& req, const ServerAccountAuth& auth,
                                             const ServerSlots& slots, const HandshakePolicy& policy) {
  if (!auth.credentials_ok) return refuse(RefuseReason::kBadCredentials);
  if (!is_known_version(req.version) || req.version < policy.min_version) {
    return refuse(RefuseReason::kUnsupportedVersion);
  }
  if (auth.sex != 'S') return refuse(RefuseReason::kNotServerAccount);
  const std::size_t cap = slots.capacity() < kMaxCharServers ? slots.capacity() : kMaxCharServers;
  if (auth.account_id >= cap) return refuse(RefuseReason::kServerIdOutOfRange);
  if (slots.occupied(auth.account_id)) return refuse(RefuseReason::kSlotOccupied);
  return {AckCode::kAccepted, RefuseReason::kNone, static_cast<std::size_t>(auth.account_id)};
}

HandshakeDecision evaluate_map_server_login(const MapServerLogin& req, const InterServerCredentials& expected,
                                            const ServerSlots& slots, const HandshakePolicy& policy) {
  if (!expected.configured()) return refuse(RefuseReason::kCredentialsNotConfigured);
  // Evaluate both comparisons unconditionally (no short-circuit timing).
  const bool user_ok = constant_time_equals(req.userid, expected.userid);
  const bool pass_ok = constant_time_equals(req.passwd, expected.passwd);
  if (!(user_ok & pass_ok)) return refuse(RefuseReason::kBadCredentials);
  if (!is_known_version(req.version) || req.version < policy.min_version) {
    return refuse(RefuseReason::kUnsupportedVersion);
  }
  const auto slot = slots.first_free();
  if (!slot) return refuse(RefuseReason::kNoFreeSlot);
  return {AckCode::kAccepted, RefuseReason::kNone, slot};
}

}  // namespace rathena::netproto::inter
