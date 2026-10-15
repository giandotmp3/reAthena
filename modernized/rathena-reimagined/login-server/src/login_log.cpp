#include "rathena/login/login_log.hpp"

namespace rathena::login {

LoginLogEntry make_login_log(UnixSeconds now, Ipv4 ip, std::string_view userid, std::int32_t rcode,
                             std::string_view message) {
  return LoginLogEntry{now, ip, std::string(userid), rcode, std::string(message)};
}

LoginLogEntry make_ip_banned_log(UnixSeconds now, Ipv4 ip) {
  return make_login_log(now, ip, "unknown", kLogRcodeIpBanned, "ip banned");
}

}  // namespace rathena::login
