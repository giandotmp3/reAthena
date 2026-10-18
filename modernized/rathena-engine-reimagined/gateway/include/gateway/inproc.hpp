#pragma once
// In-process implementations of the seams (architecture section 3) and of the abuse-prevention
// state. Single-owner, not thread-safe by design: the gateway serialises access on its own strand
// (GatewayDeps::strand). Nothing here is shared with the simulation thread.
#include <deque>
#include <map>
#include <mutex>
#include <queue>
#include <string>
#include <vector>

#include "gateway/clock.hpp"
#include "gateway/seams.hpp"

namespace gateway {

// Deterministic clock for tests: timers fire inside advance()/set() in due order. Callbacks run on the
// calling thread WITHOUT the clock's own lock held, so they may call back into services.
class ManualClock : public Clock {
 public:
  Timestamp now() const override;
  void schedule_after(Timestamp delay_s, std::function<void()> fn) override;
  void set(Timestamp t);
  void advance(Timestamp d);
  std::size_t pending_timers() const;
 private:
  void fire_due();
  mutable std::mutex mu_;
  Timestamp t_ = 1700000000;
  struct Timer { Timestamp due; std::uint64_t seq; std::function<void()> fn; };
  struct Later { bool operator()(const Timer& a, const Timer& b) const {
    return a.due != b.due ? a.due > b.due : a.seq > b.seq; } };
  std::priority_queue<Timer, std::vector<Timer>, Later> timers_;
  std::uint64_t seq_ = 0;
};

// Wall clock + timer heap drained by run_due() (called by the gateway maintenance thread).
class SystemClock : public Clock {
 public:
  Timestamp now() const override;
  void schedule_after(Timestamp delay_s, std::function<void()> fn) override;
  std::size_t run_due();
 private:
  mutable std::mutex mu_;
  struct Timer { Timestamp due; std::uint64_t seq; std::function<void()> fn; };
  struct Later { bool operator()(const Timer& a, const Timer& b) const {
    return a.due != b.due ? a.due > b.due : a.seq > b.seq; } };
  std::priority_queue<Timer, std::vector<Timer>, Later> timers_;
  std::uint64_t seq_ = 0;
};

class SequenceRng : public Rng {
 public:
  explicit SequenceRng(std::uint32_t seed = 1) : v_(seed) {}
  std::uint32_t next_u32() override { return v_ = v_ * 1664525u + 1013904223u; }
 private:
  std::uint32_t v_;
};

// Rng backed by the OS CSPRNG, for production ids, salts and tokens.
class OsRng : public Rng {
 public:
  std::uint32_t next_u32() override;
};

class InProcSessionDirectory : public SessionDirectory {
 public:
  void mark(const OnlineEntry& e) override;
  std::optional<OnlineEntry> find(AccountId aid) const override;
  void erase(AccountId aid) override;
  std::vector<OnlineEntry> entries() const override;
  int user_count() const override;
 private:
  std::map<AccountId, OnlineEntry> m_;
};

class InProcCharacterStore : public CharacterStore {
 public:
  std::vector<Character> list_by_account(AccountId aid) const override;
  std::optional<Character> find(CharId id) const override;
  bool name_exists(const std::string& name) const override;
  CharId insert(Character c) override;
  void update(const Character& c) override;
  void erase(CharId id) override;
  void set_all_offline() override;
 private:
  std::map<CharId, Character> m_;
  CharId next_ = 150000;
};

class InProcAccountStore : public AccountStore {
 public:
  std::vector<Account> find_by_userid(const std::string& userid, bool case_sensitive) const override;
  std::optional<Account> find(AccountId id) const override;
  std::optional<AccountId> create(Account a) override;
  void update(const Account& a) override;
  void remove(AccountId id) override;
  AccountId max_id() const override;
  bool web_token_in_use(const std::string& token) const override;
  void clear_all_web_tokens() override;
 private:
  std::map<AccountId, Account> m_;
};

class InProcTicketStore : public TicketStore {
 public:
  explicit InProcTicketStore(int ttl_s = 30) : ttl_(ttl_s) {}
  void issue(const AuthTicket& t) override;
  std::optional<AuthTicket> consume(AccountId aid, std::uint32_t id1, std::uint32_t id2, Timestamp now) override;
  std::optional<AuthTicket> consume_for(AuthStage hop, AccountId aid, std::uint32_t id1,
                                        const std::uint32_t* id2, const Sex* sex, Timestamp now) override;
  void revoke(AccountId aid) override { m_.erase(aid); }
  std::size_t size() const { return m_.size(); }
 private:
  int ttl_;
  std::map<AccountId, AuthTicket> m_;
};

// Loopback WorldBus test double: records what a real world would receive.
class InProcWorldBus : public WorldBus {
 public:
  void kick(AccountId aid) override { kicks.push_back(aid); }
  void broadcast(const AccountEvent& ev) override { events.push_back(ev); }
  MembershipInfo membership(CharId id) const override;
  void purge_character_data(CharId id, const std::vector<std::string>& plan) override {
    purged.push_back(id);
    purge_plans.push_back(plan);
    if (on_purge) on_purge(id);
  }
  bool available() const override { return up; }
  std::vector<AccountId> kicks;
  std::vector<AccountEvent> events;
  std::vector<CharId> purged;
  std::vector<std::vector<std::string>> purge_plans;
  std::map<CharId, MembershipInfo> members;
  std::function<void(CharId)> on_purge;  // lets a test observe store state at purge time
  bool up = true;
};

// IP ban list (BR-LOGIN-022/024). Fails closed when the backend errors.
class InProcIpBanList {
 public:
  void add(const std::string& mask, Timestamp until) { bans_.push_back({mask, until}); }
  bool is_banned(const std::string& ip, Timestamp now) const;
  void purge_expired(Timestamp now);
  std::size_t size() const { return bans_.size(); }
  bool backend_failing = false;  // simulates an SQL error
 private:
  struct Entry { std::string mask; Timestamp until; };
  std::vector<Entry> bans_;
};

// Password-failure counter (BR-LOGIN-023). Independent of any login log setting.
class InProcFailureCounter {
 public:
  InProcFailureCounter(InProcIpBanList& bans, int failures, int window_s, int ban_s)
      : bans_(bans), failures_(failures), window_(window_s), ban_(ban_s) {}
  // Returns true if this failure triggered a ban of a.b.c.*.
  bool record_failure(const std::string& ip, Timestamp now);
  void reset(const std::string& ip) { hist_.erase(ip); }
  std::size_t tracked() const { return hist_.size(); }
  void prune(Timestamp now);  // bounds memory: drops idle addresses
 private:
  InProcIpBanList& bans_;
  int failures_, window_, ban_;
  std::map<std::string, std::deque<Timestamp>> hist_;
};

// Audit sink that remembers everything (tests, and a bounded ring in the runtime).
class MemoryAuditSink : public AuditSink {
 public:
  struct Login { std::string ip, userid; int code; std::string msg; };
  void login_attempt(const std::string& ip, const std::string& userid, int code, const std::string& msg) override;
  void char_event(AccountId aid, CharId cid, const std::string& what) override;
  std::vector<Login> logins;
  std::vector<std::string> chars;
};

}  // namespace gateway
