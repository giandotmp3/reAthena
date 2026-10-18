#include <cstdlib>
#include <sstream>

#include "gateway/config.hpp"

namespace gateway {

namespace {

std::string trim(const std::string& s) {
  std::size_t b = 0, e = s.size();
  while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r')) ++b;
  while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r')) --e;
  return s.substr(b, e - b);
}

bool parse_int(const std::string& v, long long& out) {
  if (v.empty() || v.size() > 18) return false;
  std::size_t i = v[0] == '-' ? 1 : 0;
  if (i == v.size()) return false;
  long long n = 0;
  for (; i < v.size(); ++i) {
    if (v[i] < '0' || v[i] > '9') return false;
    n = n * 10 + (v[i] - '0');
  }
  out = v[0] == '-' ? -n : n;
  return true;
}

}  // namespace

std::string apply_config_text(GatewayConfig& cfg, const std::string& text) {
  std::istringstream in(text);
  std::string line, section;
  int lineno = 0;
  while (std::getline(in, line)) {
    ++lineno;
    const std::size_t hash = line.find('#');
    // a '#' inside a quoted value is not a comment; values here never contain one, secrets are ${VAR}
    if (hash != std::string::npos && line.find('"') == std::string::npos) line.erase(hash);
    else if (hash != std::string::npos && hash > line.rfind('"')) line.erase(hash);
    line = trim(line);
    if (line.empty()) continue;
    auto err = [&](const std::string& m) { return "line " + std::to_string(lineno) + ": " + m; };
    if (line.front() == '[') {
      if (line.back() != ']') return err("bad section header");
      section = trim(line.substr(1, line.size() - 2));
      continue;
    }
    const auto eq = line.find('=');
    if (eq == std::string::npos) return err("expected key = value");
    const std::string key = trim(line.substr(0, eq));
    std::string val = trim(line.substr(eq + 1));
    bool quoted = false;
    if (val.size() >= 2 && val.front() == '"' && val.back() == '"') { val = val.substr(1, val.size() - 2); quoted = true; }
    long long n = 0;
    const bool is_int = !quoted && parse_int(val, n);
    const bool is_bool = !quoted && (val == "true" || val == "false");
    const bool b = val == "true";

    auto set_int = [&](int& f) { if (!is_int) return false; f = static_cast<int>(n); return true; };
    auto set_i64 = [&](Timestamp& f) { if (!is_int) return false; f = n; return true; };
    auto set_u32 = [&](std::uint32_t& f) { if (!is_int || n < 0) return false; f = static_cast<std::uint32_t>(n); return true; };
    auto set_bool = [&](bool& f) { if (!is_bool) return false; f = b; return true; };
    auto set_str = [&](std::string& f) { if (!quoted) return false; f = val; return true; };

    const std::string full = section.empty() ? key : section + "." + key;
    bool ok = false;
    if (full == "process.role") { std::string ignored; ok = set_str(ignored); }   // read by role_from_config_text
    else if (full == "storage.database_url") ok = set_str(cfg.database_url);
    else if (full == "listen.bind") ok = set_str(cfg.login_bind);
    else if (full == "pin.pepper") ok = set_str(cfg.pin_pepper);
    else if (full == "policy.ticket_ttl_s") ok = set_int(cfg.ticket_ttl_s);
    else if (full == "policy.new_account") ok = set_bool(cfg.new_account);
    else if (full == "policy.chars_per_account") ok = set_int(cfg.chars_per_account);
    else if (full == "policy.min_chars") ok = set_int(cfg.min_chars);
    else if (full == "policy.max_chars") ok = set_int(cfg.max_chars);
    else if (full == "policy.vip_char_increase") ok = set_int(cfg.vip_char_increase);
    else if (full == "policy.char_new") ok = set_bool(cfg.char_new);
    else if (full == "policy.char_del_delay_s") ok = set_i64(cfg.char_del_delay_s);
    else if (full == "policy.char_del_level") ok = set_int(cfg.char_del_level);
    else if (full == "policy.char_del_restriction") ok = set_int(cfg.char_del_restriction);
    else if (full == "policy.char_del_option") ok = set_int(cfg.char_del_option);
    else if (full == "policy.ip_ban_failures") ok = set_int(cfg.ip_ban_failures);
    else if (full == "policy.ip_ban_window_s") ok = set_int(cfg.ip_ban_window_s);
    else if (full == "policy.ip_ban_duration_s") ok = set_int(cfg.ip_ban_duration_s);
    else if (full == "policy.pin_max_tries") ok = set_int(cfg.pin_max_tries);
    else if (full == "policy.pin_forced") ok = set_bool(cfg.pin_forced);
    else if (full == "policy.pin_enabled") ok = set_bool(cfg.pin_enabled);
    else if (full == "policy.pin_expire_s") ok = set_i64(cfg.pin_expire_s);
    else if (full == "policy.max_connect_user") ok = set_int(cfg.max_connect_user);
    else if (full == "policy.md5_challenge_enabled") ok = set_bool(cfg.md5_challenge_enabled);
    else if (full == "policy.min_group_id_to_connect") ok = set_int(cfg.min_group_id_to_connect);
    else if (full == "policy.group_id_to_connect") ok = set_int(cfg.group_id_to_connect);
    else if (full == "hashing.argon2_pool_threads") ok = set_int(cfg.argon2_pool_threads);
    else if (full == "hashing.argon2_queue_cap") ok = set_int(cfg.argon2_queue_cap);
    else if (full == "hashing.pbkdf2_iterations") ok = set_u32(cfg.pbkdf2_iterations);
    else if (full == "hashing.argon2_time_cost") ok = set_u32(cfg.argon2_time_cost);
    else if (full == "hashing.argon2_memory_kib") ok = set_u32(cfg.argon2_memory_kib);
    else if (full == "hashing.argon2_parallelism") ok = set_u32(cfg.argon2_parallelism);
    else return err("unknown key '" + full + "'");
    if (!ok) return err("wrong value type for '" + full + "'");
  }
  return {};
}

}  // namespace gateway
