#include "rathena/login/config.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>

namespace rathena::login {
namespace {

std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

std::optional<bool> parse_switch(const std::string& v) {
  const auto l = lower(v);
  if (l == "yes" || l == "on" || l == "true" || l == "1") return true;
  if (l == "no" || l == "off" || l == "false" || l == "0") return false;
  return std::nullopt;
}

template <typename T>
std::optional<T> parse_int(const std::string& v) {
  T out{};
  auto [p, ec] = std::from_chars(v.data(), v.data() + v.size(), out);
  if (ec != std::errc{} || p != v.data() + v.size()) return std::nullopt;
  return out;
}

template <typename T>
ConfigKeyStatus set_int(T& field, const std::string& v) {
  auto n = parse_int<T>(v);
  if (!n) return ConfigKeyStatus::Invalid;
  field = *n;
  return ConfigKeyStatus::Applied;
}

ConfigKeyStatus set_bool(bool& field, const std::string& v) {
  auto b = parse_switch(v);
  if (!b) return ConfigKeyStatus::Invalid;
  field = *b;
  return ConfigKeyStatus::Applied;
}

}  // namespace

ConfigKeyStatus apply_config_key(LoginConfig& cfg, const std::string& raw_key, const std::string& value) {
  const auto key = lower(raw_key);
  // HITL #1: BR-LOGIN-004 removed. Reject rather than silently ignore.
  if (key == "use_md5_passwords") return ConfigKeyStatus::Removed;

  if (key == "new_account") return set_bool(cfg.new_account, value);
  if (key == "acc_name_min_length") return set_int(cfg.acc_name_min_length, value);
  if (key == "password_min_length") return set_int(cfg.password_min_length, value);
  if (key == "allowed_regs") return set_int(cfg.allowed_regs, value);
  if (key == "time_allowed") return set_int(cfg.time_allowed_s, value);
  if (key == "start_limited_time") return set_int(cfg.start_limited_time, value);
  if (key == "case_sensitive") return set_bool(cfg.case_sensitive, value);
  if (key == "group_id_to_connect") return set_int(cfg.group_id_to_connect, value);
  if (key == "min_group_id_to_connect") return set_int(cfg.min_group_id_to_connect, value);
  if (key == "chars_per_account") return set_int(cfg.chars_per_account, value);
  if (key == "vip_group") return set_int(cfg.vip_group, value);
  if (key == "vip_char_increase") return set_int(cfg.vip_char_increase, value);
  if (key == "client_hash_check") return set_bool(cfg.client_hash_check, value);
  if (key == "usercount_disable") return set_bool(cfg.usercount_disable, value);
  if (key == "usercount_low") return set_int(cfg.usercount_low, value);
  if (key == "usercount_medium") return set_int(cfg.usercount_medium, value);
  if (key == "usercount_high") return set_int(cfg.usercount_high, value);
  if (key == "ipban_enable") return set_bool(cfg.ipban_enabled, value);
  if (key == "dynamic_pass_failure_ban") return set_bool(cfg.dynamic_pass_failure_ban, value);
  if (key == "dynamic_pass_failure_ban_interval") return set_int(cfg.dynamic_pass_failure_ban_interval_min, value);
  if (key == "dynamic_pass_failure_ban_limit") return set_int(cfg.dynamic_pass_failure_ban_limit, value);
  if (key == "dynamic_pass_failure_ban_duration") return set_int(cfg.dynamic_pass_failure_ban_duration_min, value);
  if (key == "ipban_cleanup_interval") return set_int(cfg.ipban_cleanup_interval_s, value);
  if (key == "use_dnsbl") return set_bool(cfg.use_dnsbl, value);
  if (key == "log_login") return set_bool(cfg.log_login, value);
  if (key == "md5_challenge_shim") return set_bool(cfg.md5_challenge_shim, value);
  if (key == "hash_pool_workers") return set_int(cfg.hash_pool_workers, value);
  if (key == "hash_pool_queue_depth") return set_int(cfg.hash_pool_queue_depth, value);
  return ConfigKeyStatus::Unknown;
}

std::optional<InterServerCredentials> load_inter_server_credentials(
    const char* (*getenv_fn)(const char*)) {
  const char* u = getenv_fn("INTER_SERVER_USER");
  const char* p = getenv_fn("INTER_SERVER_PASS");
  if (u == nullptr || p == nullptr || *u == '\0' || *p == '\0') return std::nullopt;
  return InterServerCredentials{u, p};
}

}  // namespace rathena::login
