#include "rathena/login/protocol/client_handler.hpp"

#include "rathena/login/auth_rules.hpp"

namespace rathena::login::proto {

HandlerOutput LoginClientHandler::on_bytes(std::span<const std::uint8_t> bytes) {
  const auto r = decode_client_packet(bytes);
  if (r.error == DecodeError::Incomplete) return {};
  if (r.error) return {{}, true};  // malformed/unknown -> disconnect
  return std::visit(
      [this](const auto& req) -> HandlerOutput {
        using T = std::decay_t<decltype(req)>;
        if constexpr (std::is_same_v<T, ReqHash>) return on_req_hash();
        else if constexpr (std::is_same_v<T, ChallengeLogin>) return on_challenge(req);
        else return on_login_pending();
      },
      *r.request);
}

HandlerOutput LoginClientHandler::on_req_hash() {
  salt_ = generate_challenge_salt(rng_);
  return {{encode_ack_hash(salt_)}, false};
}

HandlerOutput LoginClientHandler::on_challenge(const ChallengeLogin& /*req*/) {
  // Shim off (default): refuse immediately with code 3 (BR-LOGIN-003 / M6b).
  if (!cfg_.md5_challenge_shim) return {{encode_refuse_login(3, "")}, false};
  return on_login_pending();
}

HandlerOutput LoginClientHandler::on_login_pending() {
  // TODO(BR-LOGIN-001): orchestrate ipban -> dnsbl -> lookup -> pool verify ->
  // decide_login -> gates -> ticket/online/web token -> AC_ACCEPT_LOGIN.
  // Until wired, fail closed: refuse with "rejected from server" (3).
  return {{encode_refuse_login(3, "")}, false};
}

}  // namespace rathena::login::proto
