#include "netproto/connect_check.hpp"

namespace netproto {

std::optional<ConnectPacket> connect_packet_from(const DecodedPacket& p) {
  if (p.name != "CZ_ENTER") return std::nullopt;
  auto get = [&](const char* k) -> std::optional<std::int64_t> {
    const auto it = p.fields.find(k);
    if (it == p.fields.end()) return std::nullopt;
    if (const auto* v = std::get_if<std::int64_t>(&it->second)) return *v;
    return std::nullopt;
  };
  const auto a = get("account_id"), c = get("char_id"), l = get("login_id1"), t = get("client_tick"), x = get("sex");
  if (!a || !c || !l || !t || !x) return std::nullopt;
  ConnectPacket out;
  out.account_id = *a;
  out.char_id = *c;
  out.login_id1 = static_cast<std::uint32_t>(*l);
  out.client_tick = static_cast<std::uint32_t>(*t);
  out.sex = static_cast<std::uint8_t>(*x);
  return out;
}

ConnectError validate_connect(std::size_t packet_len, std::size_t expected_len,
                              const ConnectPacket& p, bool session_already_authed) {
  if (session_already_authed) return ConnectError::kAlreadyLoggedIn;
  if (packet_len != expected_len) return ConnectError::kWrongLength;
  if (p.account_id < kStartAccountNum || p.account_id > kEndAccountNum) return ConnectError::kInvalidAccount;
  if (p.char_id <= 0) return ConnectError::kInvalidChar;
  if (p.sex != 0 && p.sex != 1) return ConnectError::kInvalidSex;
  return ConnectError::kNone;
}

std::uint8_t refusal_code(ConnectError e) {
  switch (e) {
    case ConnectError::kWrongLength: return 1;
    case ConnectError::kInvalidAccount: return 2;
    case ConnectError::kInvalidChar: return 3;
    case ConnectError::kInvalidSex: return 6;
    case ConnectError::kNone:
    case ConnectError::kAlreadyLoggedIn: return 0;
  }
  return 0;
}

}  // namespace netproto
