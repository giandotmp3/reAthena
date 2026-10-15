#pragma once
// Online-state tracking (CAP-04). BR-LOGIN-018, BR-LOGIN-033, BR-INTER-004.

#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

#include "rathena/login/types.hpp"

namespace rathena::login {

/// -1 = logged in, not yet on a char server; >= 0 = char server slot; -2 = that server was lost.
inline constexpr int kOnlineUnknownServer = -1;
inline constexpr int kOnlineServerLost = -2;
inline constexpr UnixSeconds kDuplicateLoginForceClear = 30;
inline constexpr UnixSeconds kOnlineCleanupInterval = 600;

struct OnlineEntry {
  int char_server = kOnlineUnknownServer;
  std::optional<UnixSeconds> waiting_disconnect_until;
};

struct DuplicateLoginAction {
  bool kick_broadcast = false;            // 0x2734 to all char servers
  NotifyBan client_notify = NotifyBan::AlreadyLoggedIn;  // code 8
  UnixSeconds force_clear_at = 0;
};

class OnlineRegistry {
 public:
  /// BR-LOGIN-018: returns an action when the account is already online.
  std::optional<DuplicateLoginAction> check_duplicate(AccountId aid, UnixSeconds now);
  void mark_logged_in(AccountId aid);                  // -1
  void set_char_server(AccountId aid, int server);     // >= 0
  void set_offline(AccountId aid);
  /// BR-LOGIN-033: char server lost -> its users become -2.
  void on_char_server_lost(int server);
  /// BR-LOGIN-033: purge -2 entries (every 10 min) and expired force-clears.
  std::size_t cleanup(UnixSeconds now);
  /// BR-INTER-004: full online list from `server`: mark its users -2 first,
  /// then set every listed account to `server`.
  void apply_full_sync(int server, std::span<const AccountId> online);

  std::optional<OnlineEntry> get(AccountId aid) const;
  std::size_t size() const { return entries_.size(); }

 private:
  std::unordered_map<AccountId, OnlineEntry> entries_;
};

}  // namespace rathena::login
