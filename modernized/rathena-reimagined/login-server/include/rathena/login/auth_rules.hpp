#pragma once
// Pure, IO-free authentication rules (architecture §2 `rules/auth`).
// Inputs are domain values + LoginConfig + Rng&; no sockets, SQL or clocks.

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "rathena/login/account.hpp"
#include "rathena/login/config.hpp"
#include "rathena/login/md5.hpp"
#include "rathena/login/rng.hpp"
#include "rathena/login/types.hpp"

namespace rathena::login {

// ---------------------------------------------------------------------------
// BR-LOGIN-001: fixed check order, first failure wins.
//   DNSBL(3) -> _M/_F auto-create -> unknown(0) -> server account(0)
//   -> bad password(1) -> expired(2) -> banned(6) -> state!=0 (state-1)
//   -> client hash(5) -> OK
// ---------------------------------------------------------------------------

/// Facts gathered by the (effectful) handler. Fields that are only consulted
/// after an earlier gate are optional so a short-circuit never needs them.
struct AuthFacts {
  bool dnsbl_listed = false;
  /// Set when an `_M`/`_F` auto-create was attempted and refused (BR-LOGIN-006).
  std::optional<std::uint32_t> autocreate_refusal;
  std::optional<Account> account;      // nullopt = unknown userid
  std::optional<bool> password_ok;     // result of password verification
  std::optional<bool> client_hash_ok;  // BR-LOGIN-013 result (nullopt = check off)
  UnixSeconds now = 0;
};

struct AuthDecision {
  bool accepted = false;
  std::uint32_t code = 0;                  // AC_REFUSE_LOGIN.error when !accepted
  std::optional<UnixSeconds> unblock_time;  // BR-LOGIN-011: shown with code 6
};

AuthDecision decide_login(const AuthFacts& facts);

/// The ordered gate names, exposed so tests can pin the order itself.
enum class AuthGate {
  Dnsbl, AutoCreate, Unknown, ServerAccount, Password, Expired, Banned, State, ClientHash,
};
std::span<const AuthGate> auth_gate_order();

// ---------------------------------------------------------------------------
// Password checks
// ---------------------------------------------------------------------------

/// BR-LOGIN-002: case-sensitive, constant-time comparison for legacy plaintext rows.
bool plaintext_password_matches(std::string_view stored, std::string_view presented);

/// BR-LOGIN-003 (shim, opt-in, default OFF). passwdenc=3 accepts either
/// md5(salt || secret) or md5(secret || salt). When the shim is disabled every
/// challenge login is refused with code 3.
struct ChallengeResult {
  bool accepted = false;
  std::uint32_t refuse_code = 0;
};
ChallengeResult verify_md5_challenge(const LoginConfig& cfg,
                                     std::string_view salt,
                                     const std::optional<std::string>& challenge_secret,
                                     const Md5Digest& client_digest);

/// BR-LOGIN-005: challenge salt bytes in 1..255, never NUL.
std::string generate_challenge_salt(Rng& rng, std::size_t length = 12);

// ---------------------------------------------------------------------------
// Registration (BR-LOGIN-006/007/008)
// ---------------------------------------------------------------------------

struct AutoCreateRequest {
  std::string base_userid;
  Sex sex = Sex::Male;
};

/// Recognise `name_M` / `name_F` (case-insensitive suffix letter). Returns
/// nullopt when the feature is off or there is no suffix.
std::optional<AutoCreateRequest> parse_autocreate(const LoginConfig& cfg, std::string_view userid);

/// BR-LOGIN-006 refusal mapping. Returns nullopt when creation may proceed.
/// encrypted password -> 0; too short / exists -> 1; flood -> 3.
std::optional<std::uint32_t> autocreate_refusal(const LoginConfig& cfg,
                                                const AutoCreateRequest& req,
                                                std::string_view password,
                                                bool password_is_encrypted,
                                                bool userid_exists,
                                                bool flood_limited);

/// BR-LOGIN-007: server-wide fixed-window limiter, `allowed_regs` per `time_allowed`.
/// Mirrors legacy semantics: the window opens at the first successful
/// registration after the previous window ended; only successes are counted.
class RegistrationFloodLimiter {
 public:
  RegistrationFloodLimiter(int allowed_regs, int time_allowed_s)
      : allowed_(allowed_regs), window_s_(time_allowed_s) {}
  bool allowed(UnixSeconds now) const;
  void record(UnixSeconds now);

 private:
  int allowed_;
  int window_s_;
  int count_ = 0;
  std::optional<UnixSeconds> window_end_;
};

/// BR-LOGIN-008: new ID = max(existing max + 1, 2,000,000); nullopt when out of range.
std::optional<AccountId> allocate_account_id(std::optional<AccountId> current_max);
bool is_valid_account_id(AccountId id);

// ---------------------------------------------------------------------------
// Gates around the core decision
// ---------------------------------------------------------------------------

/// BR-LOGIN-013: a node with group <= account group whose hash is disabled or
/// equals the client MD5 must exist; otherwise refusal 5. nullopt = check off.
std::optional<bool> client_hash_ok(const LoginConfig& cfg, GroupId account_group,
                                   const std::optional<Md5Digest>& client_hash);

/// BR-LOGIN-014: DNSBL resolver port (implemented with the platform resolver).
class DnsResolver {
 public:
  virtual ~DnsResolver() = default;
  virtual bool resolves(const std::string& hostname) = 0;
};
/// Builds "d.c.b.a.<server>" queries; any resolution means listed.
bool dnsbl_listed(const LoginConfig& cfg, Ipv4 ip, DnsResolver& resolver);

/// BR-LOGIN-016 / BR-LOGIN-017: post-auth gates. nullopt = allowed.
std::optional<NotifyBan> group_gate(const LoginConfig& cfg, GroupId group);
std::optional<NotifyBan> char_server_gate(int connected_char_servers);

/// BR-LOGIN-015: success side effects on the account row.
struct SuccessUpdate {
  std::uint32_t login_id1 = 0;
  std::uint32_t login_id2 = 0;
};
SuccessUpdate apply_login_success(Account& acc, UnixSeconds now, Ipv4 ip,
                                  std::string lastlogin_text, std::string new_web_token, Rng& rng);

/// BR-LOGIN-038: userid resolution. case_sensitive -> byte-exact (SQL BINARY);
/// otherwise ASCII case-insensitive. More than one match -> deny (nullopt).
std::optional<Account> resolve_userid(const LoginConfig& cfg,
                                      std::span<const Account> candidates,
                                      std::string_view userid);

}  // namespace rathena::login
