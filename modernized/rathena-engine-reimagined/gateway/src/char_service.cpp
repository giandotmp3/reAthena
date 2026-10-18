// Char-select plumbing and character lifecycle (BR-CHAR-001..032). From char.cpp, char_clif.cpp,
// char_logif.cpp, char_mapif.cpp. Game data (starting items, spawn point, job/class numbers) is not
// here: it arrives through PolicyHooks::starting_package and is forwarded as opaque `extra` values.
#include <algorithm>
#include <cctype>
#include <strings.h>

#include "gateway/services.hpp"

namespace gateway {

namespace {

struct Guard {
  std::unique_lock<std::mutex> lk;
  explicit Guard(std::mutex* m) { if (m) lk = std::unique_lock<std::mutex>(*m); }
};

std::string trim(const std::string& s) {
  std::size_t b = 0, e = s.size();
  while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
  while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
  return s.substr(b, e - b);
}

constexpr std::size_t kMaxNameLen = 23;  // NAME_LENGTH - 1

}  // namespace

CharService::CharService(GatewayDeps d) : d_(d), pepper_(expand_env(d.cfg.pin_pepper)) {}

void CharService::audit(AccountId aid, CharId cid, const std::string& what) {
  if (!d_.audit) return;
  try { d_.audit->char_event(aid, cid, what); } catch (...) {}
}

int CharService::slots_for(const Account& a) const {
  const bool vip = a.vip_time > d_.clock.now();
  return rules::clamp_slots(
      rules::account_char_slots(a.char_slots, d_.cfg.chars_per_account, vip, d_.cfg.vip_char_increase, d_.cfg.max_chars),
      d_.cfg.min_chars, d_.cfg.max_chars);
}
int CharService::visible_slots(const Account& a) const { return slots_for(a); }

// ---- PIN (BR-CHAR-019..021) ---------------------------------------------------------------------
PinState CharService::pin_state(const Account& a, bool returning) const {
  const GatewayConfig& c = d_.cfg;
  if (!c.pin_enabled) return PinState::Ok;
  if (a.pin.empty()) return c.pin_forced ? PinState::New : PinState::Passed;
  if (c.pin_expire_s != 0 && a.pin_changed + c.pin_expire_s <= d_.clock.now()) return PinState::Expired;
  return returning ? PinState::Passed : PinState::Ask;
}

std::string CharService::pin_material(const std::string& pin) const {
  return pepper_.empty() ? pin : pepper_ + ":" + pin;
}

PinOutcome CharService::pin_fail(const Account& acc_in, CharSession& s) {
  Account acc = acc_in;
  const Timestamp now = d_.clock.now();
  if (acc.pin_failures > 0 && now - acc.pin_last_failure > d_.cfg.pin_failure_window_s) acc.pin_failures = 0;
  acc.pin_failures += 1;
  acc.pin_last_failure = now;
  s.pin_tries += 1;
  d_.accounts.update(acc);
  PinOutcome o;
  o.ok = false;
  const int max = d_.cfg.pin_max_tries;
  o.attempts_left = max > 0 ? std::max(0, max - acc.pin_failures) : 1000;
  if (max > 0 && acc.pin_failures >= max) {  // lockout: login is notified, the account goes offline (BR-LOGIN-035)
    o.account_offlined = true;
    d_.bus.kick(acc.id);
    d_.directory.erase(acc.id);
    d_.tickets.revoke(acc.id);
    s.phase = rules::SessionPhase::Unauthenticated;
    audit(acc.id, 0, "PIN Code check failed");
  }
  return o;
}

PinOutcome CharService::pin_check(CharSession& s, const std::string& pin) {
  std::unique_lock<std::mutex> lk;
  if (d_.strand) lk = std::unique_lock<std::mutex>(*d_.strand);
  PinOutcome bad;
  bad.bad_format = true;
  if (!d_.cfg.pin_enabled || s.phase == rules::SessionPhase::Unauthenticated) return bad;
  auto acc = d_.accounts.find(s.account_id);
  if (!acc || acc->pin.empty() || pin.size() != 4) return bad;
  const std::string stored = acc->pin;
  const std::string material = pin_material(pin);
  if (lk.owns_lock()) lk.unlock();
  VerifyResult v;
  try { v = d_.hasher.verify(PasswordScheme::Argon2id, stored, material); } catch (...) { if (d_.strand) lk.lock(); throw; }
  if (d_.strand) lk.lock();
  acc = d_.accounts.find(s.account_id);
  if (!acc) return bad;
  if (v.ok) {
    acc->pin_failures = 0;
    d_.accounts.update(*acc);
    s.pin_tries = 0;
    s.pin_verified = true;
    s.phase = rules::SessionPhase::Ready;
    PinOutcome o;
    o.ok = true;
    o.attempts_left = d_.cfg.pin_max_tries;
    return o;
  }
  return pin_fail(*acc, s);
}

bool CharService::pin_set(CharSession& s, const std::string& old_pin, const std::string& new_pin) {
  std::unique_lock<std::mutex> lk;
  if (d_.strand) lk = std::unique_lock<std::mutex>(*d_.strand);
  if (!d_.cfg.pin_enabled || s.phase == rules::SessionPhase::Unauthenticated) return false;
  if (!d_.policy.pin_is_legal(new_pin)) return false;  // BR-CHAR-020
  auto acc = d_.accounts.find(s.account_id);
  if (!acc) return false;
  const std::string stored = acc->pin;
  if (!stored.empty()) {  // changing an existing PIN needs the old one, with the same lockout counter
    if (old_pin.size() != 4) return false;
    const std::string old_material = pin_material(old_pin);
    if (lk.owns_lock()) lk.unlock();
    VerifyResult v;
    try { v = d_.hasher.verify(PasswordScheme::Argon2id, stored, old_material); } catch (...) { if (d_.strand) lk.lock(); throw; }
    if (d_.strand) lk.lock();
    acc = d_.accounts.find(s.account_id);
    if (!acc) return false;
    if (!v.ok) { pin_fail(*acc, s); return false; }
  }
  const std::string material = pin_material(new_pin);
  if (lk.owns_lock()) lk.unlock();
  std::string h;
  try { h = d_.hasher.hash(material); } catch (...) { if (d_.strand) lk.lock(); throw; }
  if (d_.strand) lk.lock();
  acc = d_.accounts.find(s.account_id);
  if (!acc) return false;
  acc->pin = h;                    // BR-LOGIN-035: stores pin and change time
  acc->pin_changed = d_.clock.now();
  acc->pin_failures = 0;
  d_.accounts.update(*acc);
  s.pin_verified = true;
  s.pin_tries = 0;
  s.phase = rules::SessionPhase::Ready;
  return true;
}

// ---- enter / handoff -----------------------------------------------------------------------------
EnterOutcome CharService::enter(CharSession& s, AccountId aid, std::uint32_t id1, std::uint32_t id2, Sex sex) {
  Guard g(d_.strand);
  EnterOutcome out;
  if (s.phase != rules::SessionPhase::Unauthenticated) { out.ignored = true; return out; }  // already authed: discard
  auto reject = [&](int code, bool notify) { out.ok = false; out.refusal_code = code; out.via_notify_ban = notify; return out; };

  if (!d_.bus.available()) return reject(0, false);  // no world / login: "rejected from server" (BR-CHAR-026)
  // The ticket may be a login ticket or a "returning from the world" re-issue (also Login stage).
  auto t = d_.tickets.consume_for(AuthStage::Login, aid, id1, &id2, &sex, d_.clock.now());
  if (!t) return reject(0, false);

  const Timestamp now = d_.clock.now();
  if (auto e = d_.directory.find(aid)) {
    if (e->state >= 0) {  // BR-CHAR-025: already in the world => kick, 8, forced offline after 30 s
      d_.bus.kick(aid);
      d_.clock.schedule_after(d_.cfg.auth_timeout_s, [this, aid, since = e->since] {
        std::unique_lock<std::mutex> lk;
        if (d_.strand) lk = std::unique_lock<std::mutex>(*d_.strand);
        auto cur = d_.directory.find(aid);
        if (cur && cur->state >= 0 && cur->since == since) {
          if (cur->char_id) if (auto c = d_.chars.find(cur->char_id)) { Character cc = *c; cc.online = false; d_.chars.update(cc); }
          d_.directory.erase(aid);
        }
      });
      return reject(refuse::AlreadyOnline, true);
    }
    if (e->state == kOnlineLoginPhase && e->conn_id != 0 && s.conn_id != 0 && e->conn_id != s.conn_id)
      return reject(refuse::AlreadyOnline, true);  // another connection sits in char select
  }

  auto acc = d_.accounts.find(aid);
  if (!acc) return reject(0, false);

  // BR-CHAR-023: capacity / maintenance gate, exempt for groups >= gm_allow_group.
  const GatewayConfig& c = d_.cfg;
  const bool closed = c.max_connect_user == 0 || c.char_maintenance ||
                      (c.max_connect_user > 0 && d_.directory.user_count() >= c.max_connect_user);
  if (closed && acc->group_id < c.gm_allow_group) return reject(0, false);

  s.account_id = aid;
  s.login_id1 = t->login_id1;
  s.login_id2 = t->login_id2;
  s.sex = t->sex;
  s.returning_from_world = t->returning;
  OnlineEntry oe;
  oe.account_id = aid;
  oe.state = kOnlineLoginPhase;
  oe.since = now;
  oe.conn_id = s.conn_id;
  oe.gen = 0;  // the char phase took over: the login-phase timeout no longer applies
  d_.directory.mark(oe);

  out.pin = pin_state(*acc, t->returning);
  s.pin_verified = out.pin == PinState::Ok || out.pin == PinState::Passed;
  s.phase = s.pin_verified ? rules::SessionPhase::Ready : rules::SessionPhase::AwaitingPin;
  out.slots = slots_for(*acc);
  out.ok = true;
  return out;
}

void CharService::prepare_return_from_world(AccountId aid, std::uint32_t id1, std::uint32_t id2, Sex sex) {
  Guard g(d_.strand);
  AuthTicket t{aid, id1, id2, sex, d_.clock.now(), 0, AuthStage::Login, true};
  d_.tickets.issue(t);
  d_.directory.erase(aid);
}

void CharService::on_disconnect(const CharSession& s) {
  Guard g(d_.strand);
  if (s.conn_id == 0) return;
  auto e = d_.directory.find(s.account_id);
  if (e && e->state == kOnlineLoginPhase && e->conn_id == s.conn_id) d_.directory.erase(s.account_id);
}

std::vector<Character> CharService::list(const CharSession& s) const {
  Guard g(d_.strand);
  return d_.chars.list_by_account(s.account_id);
}

void CharService::arm_handoff_expiry(AccountId aid, CharId cid, std::uint32_t gen) {
  d_.clock.schedule_after(d_.cfg.ticket_ttl_s, [this, aid, cid, gen] {
    std::unique_lock<std::mutex> lk;
    if (d_.strand) lk = std::unique_lock<std::mutex>(*d_.strand);
    try {
      auto e = d_.directory.find(aid);
      if (e && e->state == kOnlineHandoff && e->char_id == cid && e->gen == gen) {  // the world never took the ticket
        d_.directory.erase(aid);
        d_.tickets.revoke(aid);   // the unused world ticket goes with the entry
        if (auto c = d_.chars.find(cid)) { Character cc = *c; cc.online = false; d_.chars.update(cc); }
      }
    } catch (const StoreUnavailable&) {}
  });
}

SelectOutcome CharService::select(CharSession& s, int slot) {
  Guard g(d_.strand);
  SelectOutcome o;
  if (!rules::char_packet_allowed(s.phase, rules::CharPacket::SelectChar)) return o;
  if (!d_.bus.available()) { o.code = 1; return o; }  // world closed: "server closed"
  const Timestamp now = d_.clock.now();
  std::optional<Character> pick;
  for (const Character& c : d_.chars.list_by_account(s.account_id))
    if (c.slot == slot && c.delete_date == 0) pick = c;          // BR-CHAR-015
  if (!pick) return o;
  if (pick->unban_time > now) return o;                          // BR-CHAR-016
  Character c = *pick;
  c.online = true;
  d_.chars.update(c);
  AuthTicket t{s.account_id, s.login_id1, s.login_id2, s.sex, now, c.id, AuthStage::World, false};
  d_.tickets.issue(t);
  OnlineEntry oe;
  oe.account_id = s.account_id;
  oe.state = kOnlineHandoff;
  oe.char_id = c.id;
  oe.since = now;
  oe.conn_id = s.conn_id;
  oe.gen = s.login_id1 ? s.login_id1 : 1u;
  d_.directory.mark(oe);
  arm_handoff_expiry(s.account_id, c.id, oe.gen);
  audit(s.account_id, c.id, "select char");
  o.ok = true;
  o.ticket = t;
  o.ch = c;
  return o;
}

bool CharService::authenticate_from_world(AccountId aid, CharId cid, std::uint32_t id1, const Sex* sex) {
  Guard g(d_.strand);
  // BR-CHAR-031: ticket must match account and login id1 (+sex is checked by the world connect packet
  // layer), is single use, and a World-stage ticket is bound to the selected character.
  auto t = d_.tickets.consume_for(AuthStage::World, aid, id1, nullptr, sex, d_.clock.now());
  if (!t) return false;
  if (t->char_id != 0 && t->char_id != cid) return false;
  auto c = d_.chars.find(cid);
  if (!c || c->account_id != aid) return false;
  Character cc = *c;
  cc.online = true;
  d_.chars.update(cc);
  OnlineEntry oe;
  oe.account_id = aid;
  oe.state = 0;
  oe.char_id = cid;
  oe.since = d_.clock.now();
  d_.directory.mark(oe);
  return true;
}

bool CharService::accept_save(CharId id, bool is_final, std::size_t payload, std::size_t expected) {
  Guard g(d_.strand);
  auto c = d_.chars.find(id);
  if (!c) return false;
  if (!(c->online || is_final)) return false;  // BR-CHAR-032
  return payload == expected;
}

void CharService::startup_set_offline() {
  Guard g(d_.strand);
  d_.chars.set_all_offline();
}

int CharService::cleanup_stale_online() {
  Guard g(d_.strand);
  int purged = 0;
  const Timestamp now = d_.clock.now();
  for (const OnlineEntry& e : d_.directory.entries()) {
    // BR-CHAR-029 defect fix: an entry that is still connected is SKIPPED, the loop continues.
    if (e.state >= 0) continue;
    if (e.state == kOnlineLoginPhase && e.conn_id != 0) continue;
    const bool stale = e.state == kOnlineLost ||
                       (e.state == kOnlineHandoff && now - e.since > d_.cfg.ticket_ttl_s) ||
                       (e.state == kOnlineLoginPhase && now - e.since > d_.cfg.auth_timeout_s);
    if (!stale) continue;
    if (e.char_id) if (auto c = d_.chars.find(e.char_id)) { Character cc = *c; cc.online = false; d_.chars.update(cc); }
    d_.directory.erase(e.account_id);
    ++purged;
  }
  return purged;
}

// ---- create (BR-CHAR-001..008) ---------------------------------------------------------------------
int CharService::check_name_code(const std::string& raw, CharId) const {
  const std::string name = trim(raw);
  if (name.size() > kMaxNameLen) return -2;
  switch (d_.policy.check_name(name)) {
    case NameVerdict::Rejected: return -1;     // BR-CHAR-001: short / control / '#' / reserved
    case NameVerdict::BadLetters: return -2;   // letter policy
    case NameVerdict::Ok: break;
  }
  if (d_.chars.name_exists(name)) return -1;
  return 0;
}

MakeCharOutcome CharService::make(const CharSession& s, const MakeCharRequest& r) {
  Guard g(d_.strand);
  MakeCharOutcome o;
  auto fail = [&](int e) { o.ok = false; o.error = e; return o; };
  if (!d_.cfg.char_new) return fail(-2);  // BR-CHAR-008
  auto acc = d_.accounts.find(s.account_id);
  if (!acc) return fail(-2);
  const std::string name = trim(r.name);
  if (int n = check_name_code(name, 0)) return fail(n);
  const int slots = slots_for(*acc);
  if (r.slot < 0 || r.slot >= slots) return fail(-4);  // BR-CHAR-002

  Sex sex;
  if (r.new_client) {                                   // BR-CHAR-004
    if (r.sex != Sex::Male && r.sex != Sex::Female) return fail(-2);
    sex = r.sex;
  } else {
    if (acc->sex == Sex::Server) return fail(-2);
    sex = acc->sex;
  }
  if (!d_.policy.starting_stats_valid(r.new_client, r.stats)) return fail(-2);  // BR-CHAR-005

  auto existing = d_.chars.list_by_account(s.account_id);
  if (static_cast<int>(existing.size()) >= slots) return fail(-2);              // BR-CHAR-003
  for (const Character& c : existing) if (c.slot == r.slot) return fail(-2);    // BR-CHAR-002
  if (!d_.policy.start_job_allowed(r.start_job_flag)) return fail(-2);          // BR-CHAR-006
  auto pkg = d_.policy.starting_package(r.start_job_flag, r.stats);
  if (!pkg) return fail(-2);

  Character c;
  c.account_id = s.account_id;
  c.name = name;
  c.slot = r.slot;
  c.sex = sex;
  c.str = r.stats.str; c.agi = r.stats.agi; c.vit = r.stats.vit;
  c.int_ = r.stats.int_; c.dex = r.stats.dex; c.luk = r.stats.luk;
  c.max_hp = pkg->max_hp;                                                        // BR-CHAR-007
  c.max_sp = pkg->max_sp;
  c.extra = pkg->numbers;
  for (const auto& [k, v] : r.client_choices) c.extra["client." + k] = v;
  c.id = d_.chars.insert(c);
  audit(s.account_id, c.id, "make new char");
  o.ok = true;
  o.ch = c;
  return o;
}

// ---- delete (BR-CHAR-009..014) ---------------------------------------------------------------------
DeleteOutcome CharService::request_delete(const CharSession& s, CharId id) {
  Guard g(d_.strand);
  DeleteOutcome o;
  auto c = d_.chars.find(id);
  if (!c || c->account_id != s.account_id) { o.code = 3; o.result = DeleteResult::NotFound; return o; }
  if (c->delete_date != 0) { o.code = 0; o.result = DeleteResult::AlreadyQueued; return o; }
  const MembershipInfo m = d_.bus.membership(id);
  if ((d_.cfg.char_del_restriction & 2) && m.guild_id != 0) { o.code = 4; o.result = DeleteResult::Guild; return o; }
  if ((d_.cfg.char_del_restriction & 1) && m.party_id != 0) { o.code = 5; o.result = DeleteResult::Party; return o; }
  Character cc = *c;
  cc.delete_date = d_.clock.now() + d_.cfg.char_del_delay_s;
  d_.chars.update(cc);
  o.ok = true; o.code = 1; o.delete_date = cc.delete_date; o.remaining = cc.delete_date - d_.clock.now(); o.result = DeleteResult::Ok;
  audit(s.account_id, id, "delete requested");
  return o;
}

DeleteOutcome CharService::confirm_delete(const CharSession& s, CharId id, const std::string& code, int option_mask) {
  Guard g(d_.strand);
  DeleteOutcome o;
  auto acc = d_.accounts.find(s.account_id);
  if (!acc) { o.code = 3; o.result = DeleteResult::NotFound; return o; }
  const int mask = option_mask ? option_mask : d_.cfg.char_del_option;
  // BR-CHAR-010 / 011: e-mail (case-insensitive; the default address with an empty answer) and/or YYMMDD.
  bool code_ok = false;
  if ((mask & 1) && (strcasecmp(code.c_str(), acc->email.c_str()) == 0 ||
                     (strcasecmp(acc->email.c_str(), "a@a.com") == 0 && code.empty())))
    code_ok = true;
  if ((mask & 2) && (code == acc->birthdate || (acc->birthdate.empty() && code.empty()))) code_ok = true;
  if (!code_ok) { o.code = 5; o.result = DeleteResult::BadCode; return o; }

  auto c = d_.chars.find(id);
  if (!c || c->account_id != s.account_id) { o.code = 3; o.result = DeleteResult::NotFound; return o; }
  const GatewayConfig& cfg = d_.cfg;
  // BR-CHAR-012 gate order (char_delete): level, guild, party, delay.
  if ((cfg.char_del_level > 0 && c->base_level >= cfg.char_del_level) ||
      (cfg.char_del_level < 0 && c->base_level <= -cfg.char_del_level)) {
    o.code = 2; o.result = DeleteResult::BaseLevel; return o;
  }
  const MembershipInfo m = d_.bus.membership(id);
  if ((cfg.char_del_restriction & 2) && m.guild_id != 0) { o.code = 2; o.result = DeleteResult::Guild; return o; }
  if ((cfg.char_del_restriction & 1) && m.party_id != 0) { o.code = 2; o.result = DeleteResult::Party; return o; }
  if (cfg.char_del_delay_s > 0 && (c->delete_date == 0 || c->delete_date > d_.clock.now())) {
    o.code = 4; o.result = DeleteResult::Time; return o;
  }
  // BR-CHAR-013: world-owned data first, in the policy's order (divorce, party, ... see
  // PolicyHooks::delete_cleanup_plan); the character row goes last.
  d_.bus.purge_character_data(id, d_.policy.delete_cleanup_plan());
  d_.chars.erase(id);
  audit(s.account_id, id, "Deleted char");
  o.ok = true; o.code = 1; o.result = DeleteResult::Ok;
  return o;
}

DeleteOutcome CharService::cancel_delete(const CharSession& s, CharId id) {
  Guard g(d_.strand);
  DeleteOutcome o;
  auto c = d_.chars.find(id);
  if (!c || c->account_id != s.account_id) { o.code = 2; o.result = DeleteResult::NotFound; return o; }
  Character cc = *c;
  cc.delete_date = 0;
  d_.chars.update(cc);
  o.ok = true; o.code = 1; o.result = DeleteResult::Ok;
  return o;
}

// ---- ban / rename / move ------------------------------------------------------------------------------
Timestamp CharService::ban_character(CharId id, Timestamp delta) {
  Guard g(d_.strand);
  auto c = d_.chars.find(id);
  if (!c) return 0;
  Character cc = *c;
  const Timestamp now = d_.clock.now();
  cc.unban_time = rules::adjust_char_ban(now, cc.unban_time, delta);
  d_.chars.update(cc);
  if (cc.unban_time > now && cc.online) d_.bus.kick(cc.account_id);  // an active ban kicks the player
  return cc.unban_time;
}

int CharService::rename_code(const CharSession& s, CharId id, const std::string& new_name) {
  Guard g(d_.strand);
  auto c = d_.chars.find(id);
  if (!c || c->account_id != s.account_id) return 2;
  const std::string name = trim(new_name);
  if (name == c->name) return 0;                   // identical: nothing to do, no counter spent
  if (c->rename_counter <= 0) return 1;
  const MembershipInfo m = d_.bus.membership(id);
  if (!d_.cfg.char_rename_party && m.party_id) return 6;
  if (!d_.cfg.char_rename_guild && m.guild_id) return 5;
  const int n = check_name_code(name, id);
  if (n == -1) return 4;
  if (n != 0) return 8;
  Character cc = *c;
  cc.name = name;
  cc.rename_counter -= 1;
  d_.chars.update(cc);
  audit(s.account_id, id, "change char name");
  return 0;
}
bool CharService::rename(const CharSession& s, CharId id, const std::string& new_name) {
  return rename_code(s, id, new_name) == 0;
}

bool CharService::move_slot(const CharSession& s, CharId id, int to, bool swap_allowed) {
  Guard g(d_.strand);
  const GatewayConfig& cfg = d_.cfg;
  if (!cfg.char_move_enabled) return false;
  auto c = d_.chars.find(id);
  if (!c || c->account_id != s.account_id) return false;
  if (!cfg.char_moves_unlimited && c->slot_moves <= 0) return false;
  auto acc = d_.accounts.find(s.account_id);
  if (!acc) return false;
  if (to < 0 || to >= cfg.max_chars || to >= slots_for(*acc)) return false;
  std::optional<Character> other;
  for (const Character& x : d_.chars.list_by_account(s.account_id)) if (x.slot == to) other = x;
  if (other && other->id == c->id) return false;
  Character moved = *c;
  const int from = moved.slot;
  if (other) {
    if (!swap_allowed) return false;               // admin forbids moving onto a used slot
    Character o2 = *other;
    o2.slot = from;
    d_.chars.update(o2);
  }
  moved.slot = to;
  if (!cfg.char_moves_unlimited) moved.slot_moves -= 1;
  d_.chars.update(moved);
  return true;
}

}  // namespace gateway

namespace gateway {

PinState CharService::pin_window_state(const CharSession& s) const {
  Guard g(d_.strand);
  if (!d_.cfg.pin_enabled) return PinState::Ok;
  auto acc = d_.accounts.find(s.account_id);
  return (acc && !acc->pin.empty()) ? PinState::Ask : PinState::New;
}

bool CharService::name_acceptable(const CharSession& s, CharId id, const std::string& name) const {
  Guard g(d_.strand);
  auto c = d_.chars.find(id);
  if (!c || c->account_id != s.account_id) return false;
  return check_name_code(name, id) == 0;
}

int CharService::move_slot_by_slot(const CharSession& s, int from, int to, int* moves_left) {
  CharId id = 0;
  {
    Guard g(d_.strand);
    for (const Character& c : d_.chars.list_by_account(s.account_id)) if (c.slot == from) id = c.id;
  }
  if (id == 0) return 1;
  const bool ok = move_slot(s, id, to, d_.cfg.char_movetoused);
  Guard g(d_.strand);
  if (moves_left) { auto c = d_.chars.find(id); *moves_left = c ? c->slot_moves : 0; }
  return ok ? 0 : 1;
}

}  // namespace gateway
