#pragma once
// Login audit log (CAP-25). BR-LOGIN-039.

#include <string>
#include <string_view>

#include "rathena/login/account.hpp"

namespace rathena::login {

inline constexpr std::int32_t kLogRcodeIpBanned = -3;
inline constexpr std::int32_t kLogRcodeSuccess = 100;  // legacy "login ok" marker

/// Build the row for an attempt. `rcode` is the refusal code, or kLogRcodeSuccess.
LoginLogEntry make_login_log(UnixSeconds now, Ipv4 ip, std::string_view userid,
                             std::int32_t rcode, std::string_view message);
LoginLogEntry make_ip_banned_log(UnixSeconds now, Ipv4 ip);

}  // namespace rathena::login
