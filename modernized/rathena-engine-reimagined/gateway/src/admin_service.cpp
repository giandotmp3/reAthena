// Account administration (BR-LOGIN-026..032, 035, 037, BR-CHAR-055). From loginchrif.cpp.
#include <cctype>
#include <strings.h>

#include "gateway/services.hpp"

namespace gateway {

namespace {
struct Guard {
  std::unique_lock<std::mutex> lk;
  explicit Guard(std::mutex* m) { if (m) lk = std::unique_lock<std::mutex>(*m); }
};
}  // namespace

bool AccountAdmin::email_is_valid(const std::string& e) {
  // Port of e_mail_check (strlib.cpp): 3..39 chars, an '@', not ending in '@' or '.', and in the part
  // after the LAST '@': no "@." or "..", no control characters, no space, no ';'.
  if (e.size() < 3 || e.size() > 39) return false;
  const auto last = e.rfind('@');
  if (last == std::string::npos || e.back() == '@' || e.back() == '.') return false;
  const std::string tail = e.substr(last);
  if (tail.find("@.") != std::string::npos || tail.find("..") != std::string::npos) return false;
  for (char c : tail) {
    const unsigned char u = static_cast<unsigned char>(c);
    if ((u >= 1 && u < 32) || c == ' ' || c == ';') return false;
  }
  return true;
}

bool AccountAdmin::ban(AccountId aid, Timestamp delta) {
  Guard g(d_.strand);
  auto a = d_.accounts.find(aid);
  if (!a) return false;
  const Timestamp now = d_.clock.now();
  auto r = rules::extend_ban(now, a->unban_time, delta);  // from max(now, current); result <= now is ignored
  if (!r) return false;
  a->unban_time = *r;
  d_.accounts.update(*a);
  d_.bus.broadcast({AccountEventKind::Ban, aid, *r});
  return true;
}

void AccountAdmin::unban(AccountId aid) {
  Guard g(d_.strand);
  auto a = d_.accounts.find(aid);
  if (!a || a->unban_time == 0) return;
  a->unban_time = 0;
  d_.accounts.update(*a);
}

void AccountAdmin::set_state(AccountId aid, int state) {
  Guard g(d_.strand);
  auto a = d_.accounts.find(aid);
  if (!a || a->state == state) return;
  a->state = state;
  d_.accounts.update(*a);
  if (state != 0) d_.bus.broadcast({AccountEventKind::StateChange, aid, state});
}

bool AccountAdmin::toggle_sex(AccountId aid) {
  Guard g(d_.strand);
  auto a = d_.accounts.find(aid);
  if (!a || a->sex == Sex::Server) return false;
  a->sex = a->sex == Sex::Male ? Sex::Female : Sex::Male;
  d_.accounts.update(*a);
  d_.bus.broadcast({AccountEventKind::SexChange, aid, static_cast<std::int64_t>(a->sex)});
  return true;
}

bool AccountAdmin::change_email(AccountId aid, const std::string& current, const std::string& next) {
  Guard g(d_.strand);
  if (!email_is_valid(current) || !email_is_valid(next)) return false;
  if (strcasecmp(next.c_str(), "a@a.com") == 0) return false;
  auto a = d_.accounts.find(aid);
  if (!a || strcasecmp(a->email.c_str(), current.c_str()) != 0) return false;
  a->email = next;
  d_.accounts.update(*a);
  return true;
}

int AccountAdmin::char_slots(AccountId aid) const {
  Guard g(d_.strand);
  auto a = d_.accounts.find(aid);
  if (!a) return 0;
  const bool vip = a->vip_time > d_.clock.now();
  return rules::clamp_slots(
      rules::account_char_slots(a->char_slots, d_.cfg.chars_per_account, vip, d_.cfg.vip_char_increase, d_.cfg.max_chars),
      d_.cfg.min_chars, d_.cfg.max_chars);
}

// BR-LOGIN-032. Slot entitlement follows vip_time at read time (char_slots()/CharService), so the legacy
// "+/- slots" bookkeeping on the stored column (which double counts) is intentionally not repeated.
void AccountAdmin::apply_vip(AccountId aid, bool active) {
  Guard g(d_.strand);
  auto a = d_.accounts.find(aid);
  if (!a) return;
  const int vg = d_.cfg.vip_group;
  if (a->group_id > vg) return;  // a higher group is never touched
  if (active) {
    if (a->group_id != vg) { a->old_group = a->group_id; a->group_id = vg; }
  } else {
    if (a->group_id == vg) a->group_id = a->old_group;
    a->old_group = 0;
    a->vip_time = 0;
  }
  d_.accounts.update(*a);
}

bool AccountAdmin::delete_account(AccountId aid) {
  Guard g(d_.strand);
  if (!d_.accounts.find(aid)) return false;
  // Row first, then the `##` registry: the persistence adapter performs both in one batch; with plain
  // seams a failed purge is retried by the caller (idempotent).
  d_.accounts.remove(aid);
  d_.directory.erase(aid);
  d_.tickets.revoke(aid);
  if (d_.registry) d_.registry->purge_account_registry(aid);
  return true;
}

bool AccountAdmin::gm_authorized(AccountId requester, AccountId target) const {
  auto r = d_.accounts.find(requester);
  auto t = d_.accounts.find(target);
  if (!r || !t) return false;
  return r->group_id >= d_.cfg.gm_min_group && (requester == target || r->group_id > t->group_id);
}

bool AccountAdmin::gm_set_state(AccountId requester, AccountId target, int state) {
  Guard g(d_.strand);
  if (!gm_authorized(requester, target)) return false;
  auto a = d_.accounts.find(target);
  if (a->state == state) return true;
  a->state = state;
  d_.accounts.update(*a);
  if (state != 0) d_.bus.broadcast({AccountEventKind::StateChange, target, state});
  return true;
}

bool AccountAdmin::gm_ban(AccountId requester, AccountId target, Timestamp delta) {
  {
    Guard g(d_.strand);
    if (!gm_authorized(requester, target)) return false;
  }
  return ban(target, delta);
}
bool AccountAdmin::gm_unban(AccountId requester, AccountId target) {
  {
    Guard g(d_.strand);
    if (!gm_authorized(requester, target)) return false;
  }
  unban(target);
  return true;
}
bool AccountAdmin::gm_toggle_sex(AccountId requester, AccountId target) {
  {
    Guard g(d_.strand);
    if (!gm_authorized(requester, target)) return false;
  }
  return toggle_sex(target);
}
bool AccountAdmin::gm_apply_vip(AccountId requester, AccountId target, bool active) {
  {
    Guard g(d_.strand);
    if (!gm_authorized(requester, target)) return false;
  }
  apply_vip(target, active);
  return true;
}

void AccountAdmin::record_pin_update(AccountId aid, const std::string& pin_hash, Timestamp when) {
  Guard g(d_.strand);
  auto a = d_.accounts.find(aid);
  if (!a) return;
  a->pin = pin_hash;
  a->pin_changed = when;
  a->pin_failures = 0;
  d_.accounts.update(*a);
}

}  // namespace gateway
