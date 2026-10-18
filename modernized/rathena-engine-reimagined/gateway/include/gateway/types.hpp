#pragma once
// Domain model of the gateway: accounts, characters, tickets, online state.
// Rule-free: policy values (slot counts, name rules, ban order) arrive via
// GatewayConfig and PolicyHooks (supplied by the rathena.base mod).
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace gateway {

using AccountId = std::uint32_t;
using CharId = std::uint32_t;
using Timestamp = std::int64_t;  // unix seconds

// Thrown by every API stub whose behavior-contract rule is not yet implemented.
struct NotImplemented : std::logic_error {
  explicit NotImplemented(const std::string& what) : std::logic_error("not implemented: " + what) {}
};

enum class PasswordScheme { Plaintext, Md5, Argon2id };  // Plaintext/Md5 are legacy, upgraded lazily
enum class Sex : char { Male = 'M', Female = 'F', Server = 'S' };

struct Account {
  AccountId id = 0;
  std::string userid;
  std::string stored_secret;  // hash (Argon2id) or legacy plaintext/MD5 awaiting rehash; never logged
  PasswordScheme scheme = PasswordScheme::Argon2id;
  Sex sex = Sex::Male;
  int group_id = 0;
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
  std::string web_token;    // 16 chars while online
  std::string pin;          // empty = none; stored hashed in production
  Timestamp pin_changed = 0;
  int pin_failures = 0;
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
};

struct AuthTicket {
  AccountId account_id = 0;
  std::uint32_t login_id1 = 0, login_id2 = 0;
  Sex sex = Sex::Male;
  Timestamp issued_at = 0;
};

// -1 = login phase, >=0 = in world / char phase, -2 = server lost (BR-LOGIN-033).
constexpr int kOnlineLoginPhase = -1;
constexpr int kOnlineLost = -2;
struct OnlineEntry {
  AccountId account_id = 0;
  int state = kOnlineLoginPhase;
  CharId char_id = 0;
  Timestamp since = 0;
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

enum class LoginKind { Plaintext, Md5Challenge };

struct LoginRequest {
  std::string userid;
  std::string secret;       // plaintext password, or md5 hex digest for Md5Challenge
  std::string ip;
  LoginKind kind = LoginKind::Plaintext;
  std::string challenge_salt;
  std::string client_hash;  // optional client exe MD5
};

struct LoginOutcome {
  bool ok = false;
  int refusal_code = 0;
  Timestamp unban_time = 0;            // set with Banned
  std::optional<AuthTicket> ticket;    // set when ok
  AccountId account_id = 0;
  bool kicked_existing_session = false;
};

}  // namespace gateway
