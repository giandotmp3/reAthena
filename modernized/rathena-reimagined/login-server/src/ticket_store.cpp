#include "rathena/login/ticket_store.hpp"

#include <iterator>

namespace rathena::login {

void LoginTicketStore::issue(const LoginTicket& t) {
  tickets_[t.account_id] = t;
}

TicketVerdict LoginTicketStore::verify(AccountId aid, std::uint32_t id1, std::uint32_t id2, Sex sex,
                                       UnixSeconds now) {
  auto it = tickets_.find(aid);
  if (it == tickets_.end()) return TicketVerdict::Unknown;
  const LoginTicket t = it->second;
  tickets_.erase(it);  // single use
  if (now - t.issued_at > ttl_) return TicketVerdict::Expired;
  if (t.login_id1 != id1 || t.login_id2 != id2 || t.sex != sex) return TicketVerdict::Mismatch;
  return TicketVerdict::Accepted;
}

std::size_t LoginTicketStore::purge_expired(UnixSeconds now) {
  return static_cast<std::size_t>(
      std::erase_if(tickets_, [&](const auto& kv) { return now - kv.second.issued_at > ttl_; }));
}

}  // namespace rathena::login
