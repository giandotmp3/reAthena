#pragma once
// IP ban rules (CAP-03). BR-LOGIN-022/023/024.

#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "rathena/login/account.hpp"
#include "rathena/login/config.hpp"
#include "rathena/login/types.hpp"

namespace rathena::login {

/// Mask matching for a/ab/abc/abcd entries.
bool ip_ban_covers(const IpBan& ban, Ipv4 ip);

/// Repository lookup result; `error` models an SQL failure.
struct IpBanLookup {
  bool error = false;
  std::vector<IpBan> rows;
};

/// BR-LOGIN-022: any covering row with rtime > now -> banned (code 3).
/// Fails CLOSED: a lookup error is treated as banned.
bool is_ip_banned(const IpBanLookup& lookup, Ipv4 ip, UnixSeconds now);

/// BR-LOGIN-023 [DEFECT fix]: per-IP failure counter that is independent of
/// loginlog. `limit` failures within `interval` -> ban a.b.c.* for `duration`.
class PasswordFailureCounter {
 public:
  explicit PasswordFailureCounter(const LoginConfig& cfg);
  /// Records a failure; returns the ban to persist when the limit is reached.
  std::optional<IpBan> record_failure(Ipv4 ip, UnixSeconds now);
  void record_success(Ipv4 ip);
  std::size_t failures_in_window(Ipv4 ip, UnixSeconds now) const;

 private:
  bool enabled_;
  UnixSeconds interval_s_;
  std::size_t limit_;
  UnixSeconds duration_s_;
  std::unordered_map<Ipv4, std::deque<UnixSeconds>> failures_;
};

/// BR-LOGIN-024: remove rows with rtime <= now. Returns the number removed.
std::size_t purge_expired_ip_bans(std::vector<IpBan>& bans, UnixSeconds now);
inline constexpr UnixSeconds kIpBanPurgeInterval = 60;

}  // namespace rathena::login
