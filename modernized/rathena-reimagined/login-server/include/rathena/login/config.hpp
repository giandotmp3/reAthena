#pragma once
// Typed, immutable login configuration. Shipped values are the code defaults
// (spec §5.6). Secrets never live here; see InterServerCredentials.

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "rathena/login/md5.hpp"
#include "rathena/login/types.hpp"

namespace rathena::login {

/// One `client_hash` node (BR-LOGIN-013). hash == nullopt means "disabled".
struct ClientHashNode {
  GroupId group_id = 0;
  std::optional<Md5Digest> hash;
};

/// A `subnet:` entry from lan_support.conf (BR-LOGIN-020).
struct LanSubnet {
  Ipv4 mask = 0;
  Ipv4 char_ip = 0;
  Ipv4 map_ip = 0;
};

struct LoginConfig {
  // Registration (BR-LOGIN-006/007). Shipped: new_account = no.
  bool new_account = false;
  std::size_t acc_name_min_length = 4;
  std::size_t password_min_length = 4;
  int allowed_regs = 1;
  int time_allowed_s = 10;
  UnixSeconds start_limited_time = -1;
  bool case_sensitive = true;  // BR-LOGIN-038

  // Group gating (BR-LOGIN-016). -1 = disabled.
  GroupId group_id_to_connect = -1;
  GroupId min_group_id_to_connect = -1;

  // Slots / VIP (BR-LOGIN-031/032).
  int chars_per_account = 0;  // 0 = MAX_CHARS minus VIP increase (legacy semantics)
  int max_chars = kMaxChars;
  GroupId vip_group = 5;
  int vip_char_increase = 6;

  // Client hash (BR-LOGIN-013).
  bool client_hash_check = false;
  std::vector<ClientHashNode> client_hash_nodes;

  // Population display (BR-LOGIN-021).
  bool usercount_disable = false;
  int usercount_low = 200;
  int usercount_medium = 500;
  int usercount_high = 1000;

  // IP bans (BR-LOGIN-022/023/024).
  bool ipban_enabled = true;
  bool dynamic_pass_failure_ban = true;
  int dynamic_pass_failure_ban_interval_min = 5;
  int dynamic_pass_failure_ban_limit = 7;
  int dynamic_pass_failure_ban_duration_min = 5;
  int ipban_cleanup_interval_s = 60;

  // DNSBL (BR-LOGIN-014).
  bool use_dnsbl = false;
  std::vector<std::string> dnsbl_servers;

  // LAN (BR-LOGIN-020).
  std::vector<LanSubnet> lan_subnets;

  // Logging (BR-LOGIN-039). The failure counter does NOT depend on this (BR-LOGIN-023).
  bool log_login = true;

  // MD5 challenge shim (BR-LOGIN-003, M6b): opt-in, default OFF.
  bool md5_challenge_shim = false;

  // Bounded hash worker pool (architecture §3, §7).
  unsigned hash_pool_workers = 0;  // 0 = std::thread::hardware_concurrency()
  std::size_t hash_pool_queue_depth = 256;

  // Argon2id cost (S6, M6). Defaults: OWASP Password Storage Cheat Sheet (19 MiB, 2 passes,
  // 1 lane). Rows hashed with less memory or fewer passes are rehashed on the next login.
  std::uint32_t argon2_memory_kib = 19456;
  std::uint32_t argon2_iterations = 2;
  std::uint32_t argon2_parallelism = 1;
};

enum class ConfigKeyStatus { Applied, Unknown, Removed, Invalid };

/// Apply one `key: value` pair. `use_MD5_passwords` is Removed (HITL #1,
/// BR-LOGIN-004): startup must fail loudly rather than silently ignore it.
ConfigKeyStatus apply_config_key(LoginConfig& cfg, const std::string& key, const std::string& value);

/// Inter-server credentials (BR-LOGIN-025, BR-INTER-016). Read from the
/// environment only: ${INTER_SERVER_USER} / ${INTER_SERVER_PASS}.
struct InterServerCredentials {
  std::string user;
  std::string pass;
};

/// Returns nullopt (refuse to start) when either variable is missing or empty.
/// `getenv_fn` is injectable for tests.
std::optional<InterServerCredentials> load_inter_server_credentials(
    const char* (*getenv_fn)(const char*));

}  // namespace rathena::login
