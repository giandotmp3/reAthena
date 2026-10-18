#pragma once
// Domain model of the gateway: accounts, characters, tickets, online state.
// Rule-free: policy values (slot counts, name rules, ban order) arrive via
// GatewayConfig and PolicyHooks (supplied by the rathena.base mod).
#include <cstdint>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace gateway {

using AccountId = std::uint32_t;
using CharId = std::uint32_t;
using Timestamp = std::int64_t;  // unix seconds

// Kept for API compatibility with the scaffold; no service method throws it any more.
struct NotImplemented : std::logic_error {
  explicit NotImplemented(const std::string& what) : std::logic_error("not implemented: " + what) {}
};

// A store (persistence backend) cannot serve the request right now: database down, WAL full, ...
// The gateway turns this into a refusal; it never lets it escape to a client or crash a thread.
struct StoreUnavailable : std::runtime_error {
  explicit StoreUnavailable(const std::string& what) : std::runtime_error(what) {}
};

// Plaintext / Md5 are legacy rows awaiting lazy rehash. Argon2id means "a current PHC password hash
// string"; the algorithm that produced it is in the string prefix ($argon2id$ or $pbkdf2-sha256$).
enum class PasswordScheme { Plaintext, Md5, Argon2id };
enum class Sex : char { Male = 'M', Female = 'F', Server = 'S' };

struct Account {
  AccountId id = 0;
  std::string userid;
  std::string stored_secret;  // hash (PHC string) or legacy plaintext/MD5 awaiting rehash; never logged
  PasswordScheme scheme = PasswordScheme::Argon2id;
  // Plaintext-equivalent secret needed ONLY by the legacy MD5 challenge shim (BR-LOGIN-003). Empty
  // unless the account explicitly keeps a separate challenge secret (spec section 3, point 3).
  std::string challenge_secret;
  Sex sex = Sex::Male;
  int group_id = 0;
  int old_group = 0;              // group before VIP was applied (BR-LOGIN-032)
  int state = 0;                  // 0 = ok; state != 0 refuses with code state-1
  Timestamp unban_time = 0;
  Timestamp expiration_time = 0;  // 0 = never
  std::uint32_t login_id1 = 0, login_id2 = 0;
  std::int64_t logincount = 0;
  std::string last_ip;
  Timestamp lastlogin = 0;
  int char_slots = 0;       // 0 = use chars_per_account
  Timestamp vip_time = 0;   // 0 = not VIP
  std::string email;
  std::string birthdate;    // YYMMDD
  std::string web_token;    // 16 chars while online, empty otherwise
  bool web_token_enabled = false;
  std::string pin;          // empty = none; stored hashed
  Timestamp pin_changed = 0;
  int pin_failures = 0;
  Timestamp pin_last_failure = 0;
};

struct Character {
  CharId id = 0;
  AccountId account_id = 0;
  std::string name;
  int slot = 0;
  int base_level = 1;
  Sex sex = Sex::Male;
  int str = 1, agi = 1, vit = 1, int_ = 1, dex = 1, luk = 1;
  int max_hp = 0, max_sp = 0;
  Timestamp delete_date = 0;  // 0 = not queued for deletion
  Timestamp unban_time = 0;
  int rename_counter = 0;
  int slot_moves = 0;
  bool online = false;
  // Opaque values supplied by the starting-data hook (zeny, spawn point, ...). The gateway stores and
  // forwards them to the world; it does not interpret the keys.
  std::map<std::string, std::int64_t> extra;
};

// Which hop a ticket may be consumed at. Any (the default) is accepted by every hop and is only
// produced by tests and fakes; the services issue Login (login -> gateway char phase) and World
// (char phase -> world) tickets, and a Login ticket is NEVER accepted by the world hop, so a client
// cannot skip the PIN / char-select phase by presenting its login ids to the world directly.
enum class AuthStage : std::uint8_t { Any = 0, Login, World };

struct AuthTicket {
  AccountId account_id = 0;
  std::uint32_t login_id1 = 0, login_id2 = 0;
  Sex sex = Sex::Male;
  Timestamp issued_at = 0;
  CharId char_id = 0;             // World tickets are bound to the selected character
  AuthStage stage = AuthStage::Any;
  bool returning = false;         // Login ticket re-issued when coming back from the world (PIN counts as entered)
};

// -1 = login phase, >=0 = in world / char phase, -2 = server lost (BR-LOGIN-033).
constexpr int kOnlineLoginPhase = -1;
constexpr int kOnlineLost = -2;
constexpr int kOnlineHandoff = -3;  // char selected, waiting for the world to take the ticket
struct OnlineEntry {
  AccountId account_id = 0;
  int state = kOnlineLoginPhase;
  CharId char_id = 0;
  Timestamp since = 0;
  std::uint64_t conn_id = 0;  // connection that holds the char-select slot (0 = unknown)
  std::uint32_t gen = 0;      // login id1 of the login that created the entry; 0 once the char phase took over
};

// Client-visible login refusal codes (AC_REFUSE_LOGIN).
namespace refuse {
constexpr int UnregisteredId = 0;
constexpr int IncorrectPassword = 1;
constexpr int ServerClosed = 1;  // also: world/database unavailable (BR-LOGIN-016/017)
constexpr int Expired = 2;
constexpr int Rejected = 3;      // DNSBL, IP ban, flood, encrypted-register refusal
constexpr int BlockedByGm = 4;
constexpr int ClientHash = 5;
constexpr int Banned = 6;
constexpr int AlreadyOnline = 8;
}  // namespace refuse

enum class LoginKind { Plaintext, Md5Challenge, Sso };

// Why a login was refused, finer than the client code (which cannot say "database down").
enum class Reason {
  None, Dnsbl, IpBanned, RegistrationLimit, RegistrationInvalid, RegistrationEncrypted, Unknown,
  ServerAccount, BadPassword, Expired, Banned, State, ClientHash, GroupGate, WorldDown, AlreadyOnline,
  DatabaseDown, Overloaded, RateLimited, ChallengeUnavailable, Malformed, Internal
};

struct LoginRequest {
  std::string userid;
  std::string secret;       // plaintext password, or md5 hex digest for Md5Challenge
  std::string ip;
  LoginKind kind = LoginKind::Plaintext;
  std::string challenge_salt;
  std::string client_hash;  // optional client exe MD5 (32 lowercase hex chars)
};

struct LoginOutcome {
  bool ok = false;
  int refusal_code = 0;
  Reason reason = Reason::None;
  Timestamp unban_time = 0;            // set with Banned
  std::optional<AuthTicket> ticket;    // set when ok
  AccountId account_id = 0;
  Sex sex = Sex::Male;
  std::string web_token;               // set when ok and tokens are enabled
  bool kicked_existing_session = false;
};

}  // namespace gateway
