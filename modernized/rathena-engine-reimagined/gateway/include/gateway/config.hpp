#pragma once
// Typed gateway configuration. Secrets are NEVER literals: they are read from
// the environment only, via ${VAR} placeholders resolved by expand_env().
#include <string>

#include "gateway/types.hpp"

namespace gateway {

struct GatewayConfig {
  // Connection details: placeholders, resolved at runtime from the environment.
  std::string database_url = "${DATABASE_URL}";
  std::string login_bind = "${LOGIN_BIND}";

  // Policy defaults (overridable by rathena.base through config/hooks).
  int ticket_ttl_s = 30;
  int chars_per_account = 9;
  int min_chars = 3;   // assumption: legacy MIN_CHARS not in spec; operator-configurable
  int max_chars = 15;
  int vip_char_increase = 6;
  bool char_new = true;
  Timestamp char_del_delay_s = 86400;
  int char_del_level = 0;  // 0 = no limit
  bool new_account = false;
  int ip_ban_failures = 7;
  int ip_ban_window_s = 300;
  int ip_ban_duration_s = 300;
  int pin_max_tries = 3;
  bool pin_forced = false;
  Timestamp pin_expire_s = 0;  // 0 = never expires
  int argon2_pool_threads = 2;
  int argon2_queue_cap = 256;  // gateway overload: bounded queue, refuse beyond
  int dup_login_force_clear_s = 30;
};

// Replace ${NAME} with getenv(NAME). Unset variables expand to "" (caller must
// treat an empty required secret as a startup error). No default literals.
std::string expand_env(const std::string& value);

}  // namespace gateway
