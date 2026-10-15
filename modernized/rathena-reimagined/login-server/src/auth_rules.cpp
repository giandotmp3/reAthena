#include "rathena/login/auth_rules.hpp"

#include <algorithm>
#include <array>
#include <cctype>

#include "rathena/login/web_token.hpp"

namespace rathena::login {

// --- BR-LOGIN-001 ----------------------------------------------------------

std::span<const AuthGate> auth_gate_order() {
  static constexpr std::array<AuthGate, 9> kOrder = {
      AuthGate::Dnsbl,    AuthGate::AutoCreate, AuthGate::Unknown,
      AuthGate::ServerAccount, AuthGate::Password, AuthGate::Expired,
      AuthGate::Banned,   AuthGate::State,      AuthGate::ClientHash,
  };
  return kOrder;
}

namespace {
AuthDecision refuse(std::uint32_t code, std::optional<UnixSeconds> unblock = std::nullopt) {
  return AuthDecision{false, code, unblock};
}
AuthDecision refuse(RefuseCode code) {
  return refuse(static_cast<std::uint32_t>(code));
}
}  // namespace

AuthDecision decide_login(const AuthFacts& f) {
  for (const AuthGate gate : auth_gate_order()) {
    switch (gate) {
      case AuthGate::Dnsbl:
        if (f.dnsbl_listed) return refuse(RefuseCode::RejectedFromServer);
        break;
      case AuthGate::AutoCreate:
        if (f.autocreate_refusal) return refuse(*f.autocreate_refusal);
        break;
      case AuthGate::Unknown:
        if (!f.account) return refuse(RefuseCode::UnregisteredId);
        break;
      case AuthGate::ServerAccount:
        if (f.account->sex == Sex::Server) return refuse(RefuseCode::UnregisteredId);
        break;
      case AuthGate::Password:
        // Fails closed: an unverified password is a wrong password.
        if (f.password_ok != true) return refuse(RefuseCode::IncorrectPassword);
        break;
      case AuthGate::Expired:
        if (f.account->expiration_time != 0 && f.account->expiration_time < f.now)
          return refuse(RefuseCode::Expired);
        break;
      case AuthGate::Banned:
        if (f.account->unban_time != 0 && f.account->unban_time > f.now)
          return refuse(static_cast<std::uint32_t>(RefuseCode::BannedUntil), f.account->unban_time);
        break;
      case AuthGate::State:
        if (f.account->state != 0) return refuse(f.account->state - 1);
        break;
      case AuthGate::ClientHash:
        if (f.client_hash_ok == false) return refuse(RefuseCode::ClientVersion);
        break;
    }
  }
  return AuthDecision{true, 0, std::nullopt};
}

// --- BR-LOGIN-002 ----------------------------------------------------------

bool plaintext_password_matches(std::string_view stored, std::string_view presented) {
  if (stored.size() != presented.size()) return false;
  unsigned char diff = 0;
  for (std::size_t i = 0; i < stored.size(); ++i)
    diff |= static_cast<unsigned char>(stored[i] ^ presented[i]);
  return diff == 0;
}

// --- BR-LOGIN-003 / 005 ----------------------------------------------------

ChallengeResult verify_md5_challenge(const LoginConfig& cfg, std::string_view salt,
                                     const std::optional<std::string>& secret,
                                     const Md5Digest& client_digest) {
  if (!cfg.md5_challenge_shim)
    return {false, static_cast<std::uint32_t>(RefuseCode::RejectedFromServer)};
  if (!secret || salt.empty())
    return {false, static_cast<std::uint32_t>(RefuseCode::IncorrectPassword)};
  const std::string a = std::string(salt) + *secret;
  const std::string b = *secret + std::string(salt);
  if (md5(a) == client_digest || md5(b) == client_digest) return {true, 0};
  return {false, static_cast<std::uint32_t>(RefuseCode::IncorrectPassword)};
}

std::string generate_challenge_salt(Rng& rng, std::size_t length) {
  std::string salt(length, '\x01');
  for (auto& c : salt) c = static_cast<char>(static_cast<unsigned char>(rng.next_u32() % 255u + 1u));
  return salt;
}

// --- BR-LOGIN-006 / 007 / 008 ----------------------------------------------

std::optional<AutoCreateRequest> parse_autocreate(const LoginConfig& cfg, std::string_view userid) {
  if (!cfg.new_account || userid.size() < 3) return std::nullopt;
  const char sep = userid[userid.size() - 2];
  const char s = static_cast<char>(std::toupper(static_cast<unsigned char>(userid.back())));
  if (sep != '_' || (s != 'M' && s != 'F')) return std::nullopt;
  return AutoCreateRequest{std::string(userid.substr(0, userid.size() - 2)),
                           s == 'M' ? Sex::Male : Sex::Female};
}

std::optional<std::uint32_t> autocreate_refusal(const LoginConfig& cfg, const AutoCreateRequest& req,
                                                std::string_view password, bool password_is_encrypted,
                                                bool userid_exists, bool flood_limited) {
  if (password_is_encrypted) return static_cast<std::uint32_t>(RefuseCode::UnregisteredId);
  if (flood_limited) return static_cast<std::uint32_t>(RefuseCode::RejectedFromServer);
  if (req.base_userid.size() < cfg.acc_name_min_length || password.size() < cfg.password_min_length)
    return static_cast<std::uint32_t>(RefuseCode::IncorrectPassword);
  if (userid_exists) return static_cast<std::uint32_t>(RefuseCode::IncorrectPassword);
  return std::nullopt;
}

bool RegistrationFloodLimiter::allowed(UnixSeconds now) const {
  return !(window_end_ && now < *window_end_ && count_ >= allowed_);
}

void RegistrationFloodLimiter::record(UnixSeconds now) {
  if (!window_end_ || now > *window_end_) {
    count_ = 0;
    window_end_ = now + window_s_;
  }
  ++count_;
}

std::optional<AccountId> allocate_account_id(std::optional<AccountId> current_max) {
  std::uint64_t next = current_max ? std::uint64_t{*current_max} + 1 : kStartAccountNum;
  next = std::max<std::uint64_t>(next, kStartAccountNum);
  if (next > kEndAccountNum) return std::nullopt;
  return static_cast<AccountId>(next);
}

bool is_valid_account_id(AccountId id) {
  return id != 0 && id <= kEndAccountNum;
}

// --- BR-LOGIN-013 / 014 / 016 / 017 ----------------------------------------

std::optional<bool> client_hash_ok(const LoginConfig& cfg, GroupId account_group,
                                   const std::optional<Md5Digest>& client_hash) {
  if (!cfg.client_hash_check) return std::nullopt;
  for (const auto& node : cfg.client_hash_nodes) {
    if (node.group_id > account_group) continue;
    if (!node.hash) return true;
    if (client_hash && *client_hash == *node.hash) return true;
  }
  return false;
}

bool dnsbl_listed(const LoginConfig& cfg, Ipv4 ip, DnsResolver& resolver) {
  if (!cfg.use_dnsbl) return false;
  const std::string rev = std::to_string(ip & 0xFF) + "." + std::to_string((ip >> 8) & 0xFF) + "." +
                          std::to_string((ip >> 16) & 0xFF) + "." + std::to_string(ip >> 24);
  for (const auto& server : cfg.dnsbl_servers)
    if (resolver.resolves(rev + "." + server)) return true;
  return false;
}

std::optional<NotifyBan> group_gate(const LoginConfig& cfg, GroupId group) {
  if (cfg.group_id_to_connect >= 0 && group != cfg.group_id_to_connect) return NotifyBan::ServerClosed;
  if (cfg.min_group_id_to_connect >= 0 && cfg.group_id_to_connect == -1 &&
      group < cfg.min_group_id_to_connect)
    return NotifyBan::ServerClosed;
  return std::nullopt;
}

std::optional<NotifyBan> char_server_gate(int connected_char_servers) {
  if (connected_char_servers <= 0) return NotifyBan::ServerClosed;
  return std::nullopt;
}

// --- BR-LOGIN-015 ----------------------------------------------------------

SuccessUpdate apply_login_success(Account& acc, UnixSeconds /*now*/, Ipv4 ip, std::string lastlogin_text,
                                  std::string new_web_token, Rng& rng) {
  SuccessUpdate u;
  u.login_id1 = rng.next_u32();
  u.login_id2 = rng.next_u32();
  acc.lastlogin = std::move(lastlogin_text);
  acc.last_ip = ipv4_to_string(ip);
  acc.unban_time = 0;
  acc.logincount += 1;
  enable_web_token(acc, std::move(new_web_token));
  return u;
}

// --- BR-LOGIN-038 ----------------------------------------------------------

namespace {
bool iequals(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i)
    if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
      return false;
  return true;
}
}  // namespace

std::optional<Account> resolve_userid(const LoginConfig& cfg, std::span<const Account> candidates,
                                      std::string_view userid) {
  std::optional<Account> found;
  int matches = 0;
  for (const auto& c : candidates) {
    const bool m = cfg.case_sensitive ? (c.userid == userid) : iequals(c.userid, userid);
    if (m) {
      ++matches;
      found = c;
    }
  }
  if (matches != 1) return std::nullopt;
  return found;
}

}  // namespace rathena::login
