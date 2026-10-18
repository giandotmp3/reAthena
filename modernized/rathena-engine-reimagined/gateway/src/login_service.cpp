// Login pipeline (BR-LOGIN-001..019, 022, 023, 033, 034, 039). Derived from legacy login.cpp
// (login_mmo_auth, login_mmo_auth_new, login_check_password), loginclif.cpp (logclif_auth_ok,
// logclif_parse), ipban.cpp and loginchrif.cpp.
#include <algorithm>
#include <cstring>
#include <strings.h>

#include "gateway/crypto.hpp"
#include "gateway/services.hpp"

namespace gateway {

namespace {

constexpr AccountId kStartAccountNum = 2000000;  // BR-LOGIN-008

const char* code_message(int code) {
  switch (code) {
    case 0: return "Unregistered ID";
    case 1: return "Incorrect Password";
    case 2: return "Account Expired";
    case 3: return "Rejected from Server";
    case 4: return "Blocked by GM";
    case 5: return "Not latest game EXE";
    case 6: return "Banned";
    case 8: return "Already online";
    case 100: return "login ok";
    default: return "refused";
  }
}

bool counts_as_password_failure(Reason r) {
  switch (r) {
    case Reason::Unknown: case Reason::ServerAccount: case Reason::BadPassword:
    case Reason::ChallengeUnavailable: case Reason::RegistrationInvalid: case Reason::RegistrationEncrypted:
      return true;
    default: return false;
  }
}

std::string lower(std::string s) {
  for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

}  // namespace

struct LoginService::Ctx {
  std::string userid;
  std::optional<Account> acc;
  std::string new_hash;  // lazy rehash computed outside the strand
  bool password_ok = false;
};

LoginService::LoginService(GatewayDeps d) : d_(d) {}

LoginOutcome LoginService::refuse(const LoginRequest&, int code, Reason why, Timestamp unban) {
  LoginOutcome o;
  o.ok = false;
  o.refusal_code = code;
  o.reason = why;
  o.unban_time = unban;
  return o;
}

void LoginService::audit_result(const LoginRequest& req, const LoginOutcome& o) {
  const Timestamp now = d_.clock.now();
  // Abuse counter first and unconditionally: BR-LOGIN-023 defect fix, no dependency on any log.
  if (!o.ok && d_.cfg.ip_ban_enabled && d_.cfg.dynamic_pass_failure_ban && counts_as_password_failure(o.reason))
    d_.failures.record_failure(req.ip, now);
  if (!d_.audit) return;
  try {
    if (o.reason == Reason::IpBanned) d_.audit->login_attempt(req.ip, "unknown", -3, "ip banned");  // BR-LOGIN-039
    else if (o.ok) d_.audit->login_attempt(req.ip, req.userid, 100, "login ok");
    else d_.audit->login_attempt(req.ip, req.userid, o.refusal_code, code_message(o.refusal_code));
  } catch (...) {
    // an audit failure must never change the login result
  }
}

std::string LoginService::make_challenge_salt(std::size_t len) {
  std::string s;
  for (std::size_t i = 0; i < len; ++i) {
    unsigned v = d_.rng.next_u32() >> 24;  // high byte: the low bits of simple generators are weak
    if (v == 0) v = 1 + (d_.rng.next_u32() >> 25);  // never NUL
    s.push_back(static_cast<char>(v));
  }
  return s;
}

std::string LoginService::new_web_token(AccountId) {
  static const char hex[] = "0123456789abcdef";
  const int max_tries = std::max(1, d_.cfg.web_token_max_retries);
  for (int attempt = 0; attempt < max_tries; ++attempt) {  // BR-LOGIN-034 defect fix: the counter advances
    std::string t;
    for (int i = 0; i < 16; ++i) t.push_back(hex[(d_.rng.next_u32() >> 24) & 0xF]);
    if (!d_.accounts.web_token_in_use(t)) return t;
  }
  return {};  // gave up after the cap: the login proceeds without a token (legacy behaviour on save failure)
}

void LoginService::arm_login_timeout(AccountId aid, std::uint32_t gen) {
  d_.clock.schedule_after(d_.cfg.auth_timeout_s, [this, aid, gen] {
    std::unique_lock<std::mutex> lk;
    if (d_.strand) lk = std::unique_lock<std::mutex>(*d_.strand);
    try {
      auto e = d_.directory.find(aid);
      if (e && e->state == kOnlineLoginPhase && e->gen == gen) {  // never reached the char phase
        d_.directory.erase(aid);
        d_.tickets.revoke(aid);
      }
    } catch (const StoreUnavailable&) {}
  });
}

void LoginService::arm_forced_clear(AccountId aid) {
  if (!clear_pending_.insert(aid).second) return;  // one pending clear per account (waiting_disconnect)
  d_.clock.schedule_after(d_.cfg.auth_timeout_s, [this, aid] {
    std::unique_lock<std::mutex> lk;
    if (d_.strand) lk = std::unique_lock<std::mutex>(*d_.strand);
    clear_pending_.erase(aid);
    d_.directory.erase(aid);
    d_.tickets.revoke(aid);
  });
}

std::size_t LoginService::pending_timers() const { return clear_pending_.size(); }

LoginOutcome LoginService::login(const LoginRequest& req) {
  std::unique_lock<std::mutex> lk;
  if (d_.strand) lk = std::unique_lock<std::mutex>(*d_.strand);
  LoginOutcome o;
  try {
    o = run(lk, req);
  } catch (const StoreUnavailable&) {
    o = refuse(req, refuse::ServerClosed, Reason::DatabaseDown);
  } catch (const HashOverloaded&) {
    o = refuse(req, refuse::ServerClosed, Reason::Overloaded);
  } catch (...) {
    o = refuse(req, refuse::ServerClosed, Reason::Internal);
  }
  if (d_.strand && !lk.owns_lock()) lk.lock();
  audit_result(req, o);
  return o;
}

// Registration flood window + creation. Returns an outcome when registration fails; nullopt when the
// account exists now and normal authentication continues with `userid` (suffix stripped).
std::optional<LoginOutcome> LoginService::try_register(std::unique_lock<std::mutex>& lk, const LoginRequest& req,
                                                       std::string& userid) {
  const GatewayConfig& cfg = d_.cfg;
  if (!cfg.new_account) return std::nullopt;
  const std::size_t len = userid.size();
  if (!(len > 2 && !req.secret.empty() && userid[len - 2] == '_' && std::strchr("FfMm", userid[len - 1]) != nullptr))
    return std::nullopt;
  if (req.kind == LoginKind::Md5Challenge)  // client side encryption: cannot create from a digest
    return refuse(req, refuse::UnregisteredId, Reason::RegistrationEncrypted);
  const char sex_c = static_cast<char>(std::toupper(static_cast<unsigned char>(userid[len - 1])));
  const std::string name = userid.substr(0, len - 2);
  const Timestamp now = d_.clock.now();

  // Flood protection, server-wide (BR-LOGIN-007; Q2 of the spec keeps it server-wide). The window opens
  // at the first registration (the legacy tick comparison never opened it on the first one).
  if (reg_window_end_ != 0 && now >= reg_window_end_) { reg_window_end_ = 0; reg_count_ = 0; }
  if (reg_window_end_ != 0 && reg_count_ >= cfg.allowed_regs)
    return refuse(req, refuse::Rejected, Reason::RegistrationLimit);

  if (name.size() < static_cast<std::size_t>(cfg.acc_name_min_length) ||
      req.secret.size() < static_cast<std::size_t>(cfg.password_min_length) || name.size() >= 24)
    return refuse(req, refuse::IncorrectPassword, Reason::RegistrationInvalid);
  if (!d_.accounts.find_by_userid(name, cfg.case_sensitive_userid).empty())
    return refuse(req, refuse::IncorrectPassword, Reason::RegistrationInvalid);

  std::string phc;
  if (lk.owns_lock()) lk.unlock();
  try { phc = d_.hasher.hash(req.secret); } catch (...) { if (d_.strand) lk.lock(); throw; }
  if (d_.strand) lk.lock();
  if (!d_.accounts.find_by_userid(name, cfg.case_sensitive_userid).empty())  // lost a race
    return refuse(req, refuse::IncorrectPassword, Reason::RegistrationInvalid);

  Account a;
  a.id = std::max<AccountId>(d_.accounts.max_id() + 1, kStartAccountNum);  // BR-LOGIN-008
  a.userid = name;
  a.stored_secret = phc;
  a.scheme = PasswordScheme::Argon2id;
  a.sex = sex_c == 'F' ? Sex::Female : Sex::Male;
  a.email = "a@a.com";
  a.expiration_time = cfg.start_limited_time != -1 ? now + cfg.start_limited_time : 0;
  a.last_ip = req.ip;
  a.char_slots = cfg.min_chars;
  if (!d_.accounts.create(a)) return refuse(req, refuse::UnregisteredId, Reason::RegistrationInvalid);

  if (reg_window_end_ == 0) reg_window_end_ = now + cfg.reg_window_s;
  ++reg_count_;
  userid = name;
  return std::nullopt;
}

bool LoginService::check_password(std::unique_lock<std::mutex>& lk, Ctx& c, const LoginRequest& req) {
  const Account snap = *c.acc;
  if (req.kind == LoginKind::Md5Challenge) {
    if (!d_.cfg.md5_challenge_enabled || req.secret.empty()) return false;
    // BR-LOGIN-003: the digest is over a plaintext-equivalent secret, so it needs one (spec section 3).
    std::string secret = snap.challenge_secret;
    if (secret.empty() && snap.scheme == PasswordScheme::Plaintext) secret = snap.stored_secret;
    if (secret.empty()) return false;
    const std::string& salt = req.challenge_salt;
    if (salt.empty()) return false;
    const std::string want = lower(req.secret);
    const bool a = crypto::constant_time_equal(want, lower(d_.hasher.md5_hex(salt + secret)));  // passwdenc & 1
    const bool b = crypto::constant_time_equal(want, lower(d_.hasher.md5_hex(secret + salt)));  // passwdenc & 2
    return a || b;
  }
  // Plaintext and SSO (the token is used as the password, as in legacy): case-sensitive.
  VerifyResult r;
  std::string new_hash;
  if (lk.owns_lock()) lk.unlock();   // hashing happens outside the strand, on the bounded pool
  try {
    r = d_.hasher.verify(snap.scheme, snap.stored_secret, req.secret);
    if (r.ok && r.needs_rehash) new_hash = d_.hasher.hash(req.secret);
  } catch (...) {
    if (d_.strand) lk.lock();
    throw;
  }
  if (d_.strand) lk.lock();
  if (!r.ok) return false;
  // The account may have changed while we were hashing: re-read, and only trust the result if the
  // credential is the one we verified.
  auto fresh = d_.accounts.find(snap.id);
  if (!fresh || fresh->stored_secret != snap.stored_secret || fresh->scheme != snap.scheme) return false;
  c.acc = fresh;
  c.new_hash = new_hash;
  return true;
}

LoginOutcome LoginService::run(std::unique_lock<std::mutex>& lk, const LoginRequest& req) {
  const GatewayConfig& cfg = d_.cfg;
  Timestamp now = d_.clock.now();
  Ctx c;
  c.userid = req.userid.substr(0, 23);  // NAME_LENGTH - 1, like safestrncpy in legacy

  // IP ban: checked before anything else, fails closed (BR-LOGIN-022).
  if (cfg.ip_ban_enabled && d_.ip_bans.is_banned(req.ip, now)) return refuse(req, refuse::Rejected, Reason::IpBanned);

  // Validate the configured order: it must be a permutation of all checks, else use the default.
  std::vector<LoginCheck> order = d_.policy.login_check_order();
  {
    std::vector<LoginCheck> def = default_login_check_order();
    std::vector<LoginCheck> a = order, b = def;
    auto key = [](LoginCheck x) { return static_cast<int>(x); };
    std::sort(a.begin(), a.end(), [&](LoginCheck x, LoginCheck y) { return key(x) < key(y); });
    std::sort(b.begin(), b.end(), [&](LoginCheck x, LoginCheck y) { return key(x) < key(y); });
    if (a != b) order = def;
  }

  auto need_account = [&]() -> const Account* {
    if (c.acc) return &*c.acc;
    auto v = d_.accounts.find_by_userid(c.userid, cfg.case_sensitive_userid);
    if (v.size() == 1) c.acc = v[0];  // BR-LOGIN-038: more than one match => deny
    return c.acc ? &*c.acc : nullptr;
  };

  for (LoginCheck chk : order) {
    switch (chk) {
      case LoginCheck::Dnsbl: {
        if (!cfg.use_dnsbl || !d_.dnsbl) break;
        bool hit;
        if (lk.owns_lock()) lk.unlock();
        try { hit = d_.dnsbl->listed(req.ip); } catch (...) { if (d_.strand) lk.lock(); throw; }
        if (d_.strand) lk.lock();
        if (hit) return refuse(req, refuse::Rejected, Reason::Dnsbl);
        break;
      }
      case LoginCheck::AutoCreate: {
        if (auto r = try_register(lk, req, c.userid)) return *r;
        break;
      }
      case LoginCheck::UnknownAccount:
        if (!need_account()) return refuse(req, refuse::UnregisteredId, Reason::Unknown);
        break;
      case LoginCheck::ServerAccount: {
        const Account* a = need_account();
        if (!a) return refuse(req, refuse::UnregisteredId, Reason::Unknown);
        if (a->sex == Sex::Server) return refuse(req, refuse::UnregisteredId, Reason::ServerAccount);  // BR-LOGIN-009
        break;
      }
      case LoginCheck::Password: {
        if (!need_account()) return refuse(req, refuse::UnregisteredId, Reason::Unknown);
        if (!check_password(lk, c, req))
          return refuse(req, refuse::IncorrectPassword,
                        req.kind == LoginKind::Md5Challenge ? Reason::ChallengeUnavailable : Reason::BadPassword);
        c.password_ok = true;
        now = d_.clock.now();
        break;
      }
      case LoginCheck::Expired: {
        const Account* a = need_account();
        if (!a) return refuse(req, refuse::UnregisteredId, Reason::Unknown);
        if (a->expiration_time != 0 && a->expiration_time < now) return refuse(req, refuse::Expired, Reason::Expired);
        break;
      }
      case LoginCheck::Banned: {
        const Account* a = need_account();
        if (!a) return refuse(req, refuse::UnregisteredId, Reason::Unknown);
        if (a->unban_time != 0 && a->unban_time > now) return refuse(req, refuse::Banned, Reason::Banned, a->unban_time);
        break;
      }
      case LoginCheck::State: {
        const Account* a = need_account();
        if (!a) return refuse(req, refuse::UnregisteredId, Reason::Unknown);
        if (a->state != 0) return refuse(req, a->state - 1, Reason::State);
        break;
      }
      case LoginCheck::ClientHash: {
        const Account* a = need_account();
        if (!a) return refuse(req, refuse::UnregisteredId, Reason::Unknown);
        if (!cfg.client_hash_check) break;
        bool match = false;
        for (const auto& n : cfg.client_hash_nodes) {
          if (a->group_id < n.group_id) continue;
          if (n.hash.empty() || (!req.client_hash.empty() && lower(n.hash) == lower(req.client_hash))) { match = true; break; }
        }
        if (!match) return refuse(req, refuse::ClientHash, Reason::ClientHash);
        break;
      }
    }
  }
  if (!c.password_ok) return refuse(req, refuse::IncorrectPassword, Reason::BadPassword);  // order without a password check never authenticates
  Account a = *c.acc;

  // ---- legacy logclif_auth_ok: gates after a successful authentication ----------------------------
  if (cfg.group_id_to_connect >= 0 && a.group_id != cfg.group_id_to_connect)
    return refuse(req, refuse::ServerClosed, Reason::GroupGate);
  if (cfg.min_group_id_to_connect >= 0 && cfg.group_id_to_connect == -1 && a.group_id < cfg.min_group_id_to_connect)
    return refuse(req, refuse::ServerClosed, Reason::GroupGate);
  if (!d_.bus.available()) return refuse(req, refuse::ServerClosed, Reason::WorldDown);  // BR-LOGIN-017

  if (auto e = d_.directory.find(a.id)) {
    if (e->state >= 0) {  // already in the world: kick, answer 8, force-clear later (BR-LOGIN-018)
      d_.bus.kick(a.id);
      arm_forced_clear(a.id);
      LoginOutcome o = refuse(req, refuse::AlreadyOnline, Reason::AlreadyOnline);
      o.kicked_existing_session = true;
      return o;
    }
    // login phase / lost / handoff entry: wipe the previous session and continue (later login wins)
    d_.tickets.revoke(a.id);
    d_.directory.erase(a.id);
  }

  // ---- commit (BR-LOGIN-015): only now, so refused logins leave no trace on the account ---------
  auto nonzero = [&] { std::uint32_t v = d_.rng.next_u32(); return v ? v : 1u; };
  a.login_id1 = nonzero();
  a.login_id2 = nonzero();
  a.lastlogin = now;
  a.last_ip = req.ip;
  a.unban_time = 0;
  a.logincount += 1;
  if (!c.new_hash.empty()) {  // lazy rehash of a legacy row / weaker hash (ARCH-HASH-LAZY)
    a.stored_secret = c.new_hash;
    a.scheme = PasswordScheme::Argon2id;
  }
  std::string token;
  if (cfg.use_web_auth_token && a.sex != Sex::Server) {
    token = new_web_token(a.id);
    a.web_token = token;
    a.web_token_enabled = !token.empty();
  }
  d_.accounts.update(a);

  AuthTicket t{a.id, a.login_id1, a.login_id2, a.sex, now, 0, AuthStage::Login, false};
  d_.tickets.issue(t);
  OnlineEntry oe;
  oe.account_id = a.id;
  oe.state = kOnlineLoginPhase;
  oe.since = now;
  oe.gen = a.login_id1;
  d_.directory.mark(oe);
  arm_login_timeout(a.id, a.login_id1);

  LoginOutcome o;
  o.ok = true;
  o.ticket = t;
  o.account_id = a.id;
  o.sex = a.sex;
  o.web_token = token;
  return o;
}

void LoginService::on_logout(AccountId aid) {
  std::unique_lock<std::mutex> lk;
  if (d_.strand) lk = std::unique_lock<std::mutex>(*d_.strand);
  d_.directory.erase(aid);
  d_.tickets.revoke(aid);
  const int gen = ++logout_gen_[aid];
  d_.clock.schedule_after(d_.cfg.web_token_disable_delay_s, [this, aid, gen] {
    std::unique_lock<std::mutex> lk2;
    if (d_.strand) lk2 = std::unique_lock<std::mutex>(*d_.strand);
    try {
      auto it = logout_gen_.find(aid);
      if (it == logout_gen_.end() || it->second != gen) return;  // a newer logout owns the timer
      logout_gen_.erase(it);
      if (d_.directory.find(aid)) return;  // logged in again meanwhile: the new session keeps its token
      if (auto a = d_.accounts.find(aid)) {
        if (!a->web_token.empty() || a->web_token_enabled) {
          a->web_token.clear();
          a->web_token_enabled = false;
          d_.accounts.update(*a);
        }
      }
    } catch (const StoreUnavailable&) {}
  });
}

void LoginService::sweep_duplicate_clears() {
  std::unique_lock<std::mutex> lk;
  if (d_.strand) lk = std::unique_lock<std::mutex>(*d_.strand);
  const Timestamp now = d_.clock.now();
  for (const OnlineEntry& e : d_.directory.entries()) {
    const bool lost_old = e.state == kOnlineLost && now - e.since >= d_.cfg.online_lost_purge_s;  // BR-LOGIN-033
    const bool stuck_login = e.state == kOnlineLoginPhase && e.gen != 0 && e.conn_id == 0 &&
                             now - e.since > d_.cfg.auth_timeout_s;  // backstop for a lost timer
    if (lost_old || stuck_login) { d_.directory.erase(e.account_id); d_.tickets.revoke(e.account_id); }
  }
  d_.ip_bans.purge_expired(now);  // BR-LOGIN-024
  d_.failures.prune(now);
}

void LoginService::startup() {
  std::unique_lock<std::mutex> lk;
  if (d_.strand) lk = std::unique_lock<std::mutex>(*d_.strand);
  d_.accounts.clear_all_web_tokens();
}

}  // namespace gateway
