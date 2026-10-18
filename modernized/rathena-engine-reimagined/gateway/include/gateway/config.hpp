#pragma once
// Typed gateway configuration. Secrets are NEVER literals: they are read from
// the environment only, via ${VAR} placeholders resolved by expand_env().
#include <string>
#include <vector>

#include "gateway/types.hpp"

namespace gateway {

struct ClientHashNode {
  int group_id = 0;     // applies to accounts whose group >= this
  std::string hash;     // 32 lowercase hex chars, or empty = "disabled" (any client may log in)
};

struct GatewayConfig {
  // Connection details: placeholders, resolved at runtime from the environment.
  std::string database_url = "${DATABASE_URL}";
  std::string login_bind = "${LOGIN_BIND}";
  std::string pin_pepper = "${GATEWAY_PIN_PEPPER}";  // optional server-side secret mixed into stored PIN hashes

  // ---- login ----------------------------------------------------------------------------------
  int ticket_ttl_s = 30;
  bool new_account = false;          // BR-LOGIN-006 (`_M` / `_F` registration)
  int acc_name_min_length = 4;
  int password_min_length = 4;
  int allowed_regs = 1;              // BR-LOGIN-007: registrations allowed per window, server-wide
  int reg_window_s = 10;
  Timestamp start_limited_time = -1; // seconds of validity for new accounts, -1 = never expires
  bool case_sensitive_userid = true; // BR-LOGIN-038
  bool md5_challenge_enabled = true; // wire-compat shim only; needs Account::challenge_secret
  int group_id_to_connect = -1;      // BR-LOGIN-016
  int min_group_id_to_connect = -1;
  bool use_dnsbl = false;            // BR-LOGIN-014
  bool client_hash_check = false;    // BR-LOGIN-013
  std::vector<ClientHashNode> client_hash_nodes;
  bool use_web_auth_token = true;    // BR-LOGIN-034
  int web_token_disable_delay_s = 10;
  int web_token_max_retries = 20;
  int auth_timeout_s = 30;           // BR-LOGIN-018 forced clear; login-phase entry lifetime
  int online_lost_purge_s = 600;     // BR-LOGIN-033
  int vip_group = 5;                 // BR-LOGIN-032
  int vip_char_increase = 6;
  int chars_per_account = 9;
  int min_chars = 3;                 // assumption: legacy MIN_CHARS not in spec; operator-configurable
  int max_chars = 15;
  int dup_login_force_clear_s = 30;  // alias kept from the scaffold; auth_timeout_s is the one used

  // IP ban (BR-LOGIN-022..024)
  bool ip_ban_enabled = true;
  bool dynamic_pass_failure_ban = true;
  int ip_ban_failures = 7;
  int ip_ban_window_s = 300;
  int ip_ban_duration_s = 300;
  int ip_ban_cleanup_s = 60;

  // ---- char -----------------------------------------------------------------------------------
  bool char_new = true;
  Timestamp char_del_delay_s = 86400;
  int char_del_level = 0;            // 0 = no limit; +N: cannot delete level >= N; -N: cannot delete level <= N
  int char_del_restriction = 3;      // bit 1 = party, bit 2 = guild (legacy order: guild answers 4, party 5)
  int char_del_option = 2;           // BR-CHAR-011: bit 1 = e-mail, bit 2 = birthdate
  bool char_rename_party = false;
  bool char_rename_guild = false;
  bool char_move_enabled = true;
  bool char_moves_unlimited = false;
  bool char_movetoused = true;
  int max_connect_user = -1;         // BR-CHAR-023: -1 unlimited, 0 closed
  bool char_maintenance = false;
  int gm_allow_group = 99;
  int gm_min_group = 1;              // BR-CHAR-055: lowest group that may run account GM operations

  // PIN (BR-CHAR-019..022)
  bool pin_enabled = true;
  bool pin_forced = false;
  Timestamp pin_expire_s = 0;        // 0 = never expires
  int pin_max_tries = 3;
  int pin_failure_window_s = 900;    // account-level PIN failures older than this are forgotten

  // ---- hashing / overload ---------------------------------------------------------------------
  int argon2_pool_threads = 2;
  int argon2_queue_cap = 256;        // gateway overload: bounded queue, refuse beyond
  std::uint32_t pbkdf2_iterations = 600000;  // fallback hasher; OWASP guidance for PBKDF2-HMAC-SHA256
  std::uint32_t argon2_time_cost = 2;
  std::uint32_t argon2_memory_kib = 19456;
  std::uint32_t argon2_parallelism = 1;
};

// Replace ${NAME} with getenv(NAME). Unset variables expand to "" (caller must
// treat an empty required secret as a startup error). No default literals.
std::string expand_env(const std::string& value);

// Applies a TOML-subset document (sections, key = value, # comments, "strings", ints, bools) over `cfg`.
// Returns an empty string on success, otherwise a one-line error naming the line. Unknown keys are errors.
std::string apply_config_text(GatewayConfig& cfg, const std::string& text);

}  // namespace gateway
