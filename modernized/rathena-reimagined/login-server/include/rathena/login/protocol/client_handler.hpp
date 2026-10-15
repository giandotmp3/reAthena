#pragma once
// Client session handler (STUB). Wires decode -> rules -> repositories -> encode.
// The orchestration of BR-LOGIN-001 against live repositories and the hash
// pool is pending; the pure decisions it composes are implemented and tested.

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "rathena/login/config.hpp"
#include "rathena/login/protocol/client_packets.hpp"
#include "rathena/login/rng.hpp"

namespace rathena::login::proto {

struct HandlerOutput {
  std::vector<std::vector<std::uint8_t>> to_client;
  bool disconnect = false;
};

class LoginClientHandler {
 public:
  LoginClientHandler(const LoginConfig& cfg, Rng& rng) : cfg_(cfg), rng_(rng) {}

  /// Processes bytes from the client. Malformed framing disconnects.
  HandlerOutput on_bytes(std::span<const std::uint8_t> bytes);

  const std::string& challenge_salt() const { return salt_; }

 private:
  HandlerOutput on_req_hash();
  HandlerOutput on_challenge(const ChallengeLogin& req);
  HandlerOutput on_login_pending();

  const LoginConfig& cfg_;
  Rng& rng_;
  std::string salt_;
};

}  // namespace rathena::login::proto
