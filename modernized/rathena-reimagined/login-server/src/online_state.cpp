#include "rathena/login/online_state.hpp"

namespace rathena::login {

std::optional<DuplicateLoginAction> OnlineRegistry::check_duplicate(AccountId aid, UnixSeconds now) {
  auto it = entries_.find(aid);
  if (it == entries_.end()) return std::nullopt;
  if (it->second.char_server > kOnlineUnknownServer) {
    if (!it->second.waiting_disconnect_until)
      it->second.waiting_disconnect_until = now + kDuplicateLoginForceClear;
    return DuplicateLoginAction{true, NotifyBan::AlreadyLoggedIn, *it->second.waiting_disconnect_until};
  }
  if (it->second.char_server == kOnlineUnknownServer) {
    // Authed but never reached a char server: the stale entry is dropped.
    entries_.erase(it);
  }
  return std::nullopt;
}

void OnlineRegistry::mark_logged_in(AccountId aid) {
  entries_[aid] = OnlineEntry{kOnlineUnknownServer, std::nullopt};
}

void OnlineRegistry::set_char_server(AccountId aid, int server) {
  auto& e = entries_[aid];
  e.char_server = server;
  e.waiting_disconnect_until.reset();
}

void OnlineRegistry::set_offline(AccountId aid) {
  entries_.erase(aid);
}

void OnlineRegistry::on_char_server_lost(int server) {
  for (auto& [aid, e] : entries_)
    if (e.char_server == server) e.char_server = kOnlineServerLost;
}

std::size_t OnlineRegistry::cleanup(UnixSeconds now) {
  return static_cast<std::size_t>(std::erase_if(entries_, [&](const auto& kv) {
    const auto& e = kv.second;
    return e.char_server == kOnlineServerLost ||
           (e.waiting_disconnect_until && *e.waiting_disconnect_until <= now);
  }));
}

void OnlineRegistry::apply_full_sync(int server, std::span<const AccountId> online) {
  on_char_server_lost(server);
  for (AccountId aid : online) set_char_server(aid, server);
}

std::optional<OnlineEntry> OnlineRegistry::get(AccountId aid) const {
  auto it = entries_.find(aid);
  if (it == entries_.end()) return std::nullopt;
  return it->second;
}

}  // namespace rathena::login
