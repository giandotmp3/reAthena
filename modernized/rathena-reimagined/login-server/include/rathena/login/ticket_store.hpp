#pragma once
// One-time auth tickets (CAP-04). BR-LOGIN-019, TTL from BR-INTER-008.

#include <optional>
#include <unordered_map>

#include "rathena/login/account.hpp"
#include "rathena/login/types.hpp"

namespace rathena::login {

enum class TicketVerdict { Accepted, Unknown, Mismatch, Expired };

class LoginTicketStore {
 public:
  explicit LoginTicketStore(UnixSeconds ttl = kAuthTicketTtl) : ttl_(ttl) {}

  void issue(const LoginTicket& t);
  /// Char-side verify (0x2712 -> 0x2713). Single use: any verdict other than
  /// Unknown consumes the ticket, so a wrong id2 cannot be retried (hardening
  /// over legacy, which kept the node on mismatch). The client IP is not
  /// compared (legacy parity: the IP check is disabled upstream).
  TicketVerdict verify(AccountId aid, std::uint32_t id1, std::uint32_t id2, Sex sex,
                       UnixSeconds now);
  std::size_t purge_expired(UnixSeconds now);
  std::size_t size() const { return tickets_.size(); }

 private:
  UnixSeconds ttl_;
  std::unordered_map<AccountId, LoginTicket> tickets_;
};

}  // namespace rathena::login
