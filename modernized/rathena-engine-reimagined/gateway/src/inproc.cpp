#include "gateway/inproc.hpp"

#include <algorithm>
#include <chrono>
#include <random>
#include <strings.h>

#include "gateway/rules.hpp"

namespace gateway {

// ---- clocks --------------------------------------------------------------------------------
Timestamp ManualClock::now() const {
  std::lock_guard<std::mutex> g(mu_);
  return t_;
}
void ManualClock::schedule_after(Timestamp delay_s, std::function<void()> fn) {
  std::lock_guard<std::mutex> g(mu_);
  timers_.push({t_ + std::max<Timestamp>(0, delay_s), seq_++, std::move(fn)});
}
void ManualClock::set(Timestamp t) {
  { std::lock_guard<std::mutex> g(mu_); t_ = t; }
  fire_due();
}
void ManualClock::advance(Timestamp d) {
  { std::lock_guard<std::mutex> g(mu_); t_ += d; }
  fire_due();
}
std::size_t ManualClock::pending_timers() const {
  std::lock_guard<std::mutex> g(mu_);
  return timers_.size();
}
void ManualClock::fire_due() {
  for (;;) {
    std::function<void()> fn;
    {
      std::lock_guard<std::mutex> g(mu_);
      if (timers_.empty() || timers_.top().due > t_) return;
      fn = timers_.top().fn;
      timers_.pop();
    }
    fn();  // outside the lock: callbacks may schedule more timers
  }
}

Timestamp SystemClock::now() const {
  return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}
void SystemClock::schedule_after(Timestamp delay_s, std::function<void()> fn) {
  const Timestamp due = now() + std::max<Timestamp>(0, delay_s);
  std::lock_guard<std::mutex> g(mu_);
  timers_.push({due, seq_++, std::move(fn)});
}
std::size_t SystemClock::run_due() {
  std::size_t n = 0;
  for (;;) {
    std::function<void()> fn;
    {
      std::lock_guard<std::mutex> g(mu_);
      if (timers_.empty() || timers_.top().due > now()) return n;
      fn = timers_.top().fn;
      timers_.pop();
    }
    fn();
    ++n;
  }
}

std::uint32_t OsRng::next_u32() {
  static thread_local std::random_device rd;
  return rd();
}

// ---- directory -----------------------------------------------------------------------------
void InProcSessionDirectory::mark(const OnlineEntry& e) { m_[e.account_id] = e; }
std::optional<OnlineEntry> InProcSessionDirectory::find(AccountId aid) const {
  auto it = m_.find(aid);
  if (it == m_.end()) return std::nullopt;
  return it->second;
}
void InProcSessionDirectory::erase(AccountId aid) { m_.erase(aid); }
std::vector<OnlineEntry> InProcSessionDirectory::entries() const {
  std::vector<OnlineEntry> v;
  for (auto& [k, e] : m_) v.push_back(e);
  return v;
}
int InProcSessionDirectory::user_count() const {
  int n = 0;
  for (auto& [k, e] : m_) if (e.state >= 0) ++n;
  return std::max(0, n);
}

// ---- characters ----------------------------------------------------------------------------
std::vector<Character> InProcCharacterStore::list_by_account(AccountId aid) const {
  std::vector<Character> v;
  for (auto& [id, c] : m_) if (c.account_id == aid) v.push_back(c);
  std::sort(v.begin(), v.end(), [](const Character& a, const Character& b) { return a.slot < b.slot; });
  return v;
}
std::optional<Character> InProcCharacterStore::find(CharId id) const {
  auto it = m_.find(id);
  if (it == m_.end()) return std::nullopt;
  return it->second;
}
bool InProcCharacterStore::name_exists(const std::string& name) const {
  for (auto& [id, c] : m_) if (strcasecmp(c.name.c_str(), name.c_str()) == 0) return true;
  return false;
}
CharId InProcCharacterStore::insert(Character c) {
  c.id = next_++;
  m_[c.id] = c;
  return c.id;
}
void InProcCharacterStore::update(const Character& c) { m_[c.id] = c; }
void InProcCharacterStore::erase(CharId id) { m_.erase(id); }
void InProcCharacterStore::set_all_offline() { for (auto& [id, c] : m_) c.online = false; }

// ---- accounts ------------------------------------------------------------------------------
std::vector<Account> InProcAccountStore::find_by_userid(const std::string& userid, bool cs) const {
  std::vector<Account> v;
  for (auto& [id, a] : m_) {
    if (cs ? a.userid == userid : strcasecmp(a.userid.c_str(), userid.c_str()) == 0) v.push_back(a);
  }
  return v;
}
std::optional<Account> InProcAccountStore::find(AccountId id) const {
  auto it = m_.find(id);
  if (it == m_.end()) return std::nullopt;
  return it->second;
}
std::optional<AccountId> InProcAccountStore::create(Account a) {
  if (a.id == 0 || a.id > 100000000 || m_.count(a.id)) return std::nullopt;
  m_[a.id] = a;
  return a.id;
}
void InProcAccountStore::update(const Account& a) { m_[a.id] = a; }
void InProcAccountStore::remove(AccountId id) { m_.erase(id); }
AccountId InProcAccountStore::max_id() const { return m_.empty() ? 0 : m_.rbegin()->first; }
bool InProcAccountStore::web_token_in_use(const std::string& token) const {
  for (auto& [id, a] : m_) if (!a.web_token.empty() && a.web_token == token) return true;
  return false;
}
void InProcAccountStore::clear_all_web_tokens() {
  for (auto& [id, a] : m_) { a.web_token.clear(); a.web_token_enabled = false; }
}

// ---- tickets -------------------------------------------------------------------------------
void InProcTicketStore::issue(const AuthTicket& t) { m_[t.account_id] = t; }
std::optional<AuthTicket> InProcTicketStore::consume(AccountId aid, std::uint32_t id1, std::uint32_t id2, Timestamp now) {
  return consume_for(AuthStage::Any, aid, id1, &id2, nullptr, now);
}
std::optional<AuthTicket> InProcTicketStore::consume_for(AuthStage hop, AccountId aid, std::uint32_t id1,
                                                         const std::uint32_t* id2, const Sex* sex, Timestamp now) {
  auto it = m_.find(aid);
  if (it == m_.end()) return std::nullopt;
  const AuthTicket t = it->second;
  if (now - t.issued_at > ttl_) { m_.erase(it); return std::nullopt; }
  if (t.login_id1 != id1) return std::nullopt;
  if (id2 && t.login_id2 != *id2) return std::nullopt;
  if (sex && t.sex != *sex) return std::nullopt;
  if (hop != AuthStage::Any && t.stage != AuthStage::Any && t.stage != hop) return std::nullopt;
  m_.erase(it);  // single use
  return t;
}

MembershipInfo InProcWorldBus::membership(CharId id) const {
  auto it = members.find(id);
  return it == members.end() ? MembershipInfo{} : it->second;
}

// ---- ip ban --------------------------------------------------------------------------------
bool InProcIpBanList::is_banned(const std::string& ip, Timestamp now) const {
  if (backend_failing) return true;  // fail closed (BR-LOGIN-022)
  for (auto& b : bans_) if (b.until > now && rules::ip_mask_matches(b.mask, ip)) return true;
  return false;
}
void InProcIpBanList::purge_expired(Timestamp now) {
  bans_.erase(std::remove_if(bans_.begin(), bans_.end(), [&](const Entry& e) { return e.until <= now; }), bans_.end());
}

bool InProcFailureCounter::record_failure(const std::string& ip, Timestamp now) {
  auto& h = hist_[ip];
  h.push_back(now);
  while (!h.empty() && now - h.front() >= window_) h.pop_front();
  if (static_cast<int>(h.size()) >= failures_) {
    bans_.add(rules::ip_to_class_c_mask(ip), now + ban_);
    h.clear();
    return true;
  }
  return false;
}
void InProcFailureCounter::prune(Timestamp now) {
  for (auto it = hist_.begin(); it != hist_.end();) {
    while (!it->second.empty() && now - it->second.front() >= window_) it->second.pop_front();
    it = it->second.empty() ? hist_.erase(it) : std::next(it);
  }
}

void MemoryAuditSink::login_attempt(const std::string& ip, const std::string& userid, int code, const std::string& msg) {
  logins.push_back({ip, userid, code, msg});
}
void MemoryAuditSink::char_event(AccountId aid, CharId cid, const std::string& what) {
  chars.push_back(std::to_string(aid) + "/" + std::to_string(cid) + ": " + what);
}

}  // namespace gateway
