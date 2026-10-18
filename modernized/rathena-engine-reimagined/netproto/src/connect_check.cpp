#include "netproto/connect_check.hpp"

namespace netproto {

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
