#include "rathena/login/ip_ban.hpp"

#include <algorithm>

namespace rathena::login {

bool ip_ban_covers(const IpBan& ban, Ipv4 ip) {
  const unsigned octets = std::clamp<unsigned>(ban.octets, 1u, 4u);
  const Ipv4 mask = octets == 4 ? 0xFFFFFFFFu : ~(0xFFFFFFFFu >> (8u * octets));
  return (ban.base & mask) == (ip & mask);
}

bool is_ip_banned(const IpBanLookup& lookup, Ipv4 ip, UnixSeconds now) {
  if (lookup.error) return true;  // fail closed
  return std::any_of(lookup.rows.begin(), lookup.rows.end(),
                     [&](const IpBan& b) { return b.rtime > now && ip_ban_covers(b, ip); });
}

PasswordFailureCounter::PasswordFailureCounter(const LoginConfig& cfg)
    : enabled_(cfg.ipban_enabled && cfg.dynamic_pass_failure_ban),
      interval_s_(UnixSeconds{cfg.dynamic_pass_failure_ban_interval_min} * 60),
      limit_(static_cast<std::size_t>(std::max(1, cfg.dynamic_pass_failure_ban_limit))),
      duration_s_(UnixSeconds{cfg.dynamic_pass_failure_ban_duration_min} * 60) {}

std::optional<IpBan> PasswordFailureCounter::record_failure(Ipv4 ip, UnixSeconds now) {
  if (!enabled_) return std::nullopt;
  auto& q = failures_[ip];
  q.push_back(now);
  while (!q.empty() && q.front() <= now - interval_s_) q.pop_front();
  if (q.size() < limit_) return std::nullopt;
  q.clear();
  return IpBan{ip & 0xFFFFFF00u, 3, now, now + duration_s_, "Password error ban"};
}

void PasswordFailureCounter::record_success(Ipv4 /*ip*/) {
  // Legacy counts failures in a sliding window regardless of later successes.
}

std::size_t PasswordFailureCounter::failures_in_window(Ipv4 ip, UnixSeconds now) const {
  auto it = failures_.find(ip);
  if (it == failures_.end()) return 0;
  return static_cast<std::size_t>(std::count_if(it->second.begin(), it->second.end(),
                                                [&](UnixSeconds t) { return t > now - interval_s_; }));
}

std::size_t purge_expired_ip_bans(std::vector<IpBan>& bans, UnixSeconds now) {
  const auto before = bans.size();
  std::erase_if(bans, [&](const IpBan& b) { return b.rtime <= now; });
  return before - bans.size();
}

}  // namespace rathena::login
