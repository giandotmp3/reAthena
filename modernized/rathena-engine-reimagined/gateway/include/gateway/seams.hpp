#pragma once
// The three architecture seams (REIMAGINED_ARCHITECTURE section 3) plus the stores and hooks the
// gateway needs. In-process implementations live in inproc.hpp / loopback.hpp / persist_adapter.hpp;
// a future gateway/world split is a new implementation of these, not a redesign.
//
// Threading contract: every method may be called with the gateway strand held (one gateway-owned
// mutex, see GatewayDeps::strand). Implementations that talk to the world must not block on the
// simulation thread; WorldBus implementations hand messages over through a bounded mailbox.
// Methods that touch a persistence backend throw StoreUnavailable when it is down.
#include <optional>
#include <string>
#include <vector>

#include "gateway/types.hpp"

namespace gateway {

// Who is online where.
struct SessionDirectory {
  virtual ~SessionDirectory() = default;
  virtual void mark(const OnlineEntry& e) = 0;
  virtual std::optional<OnlineEntry> find(AccountId aid) const = 0;
  virtual void erase(AccountId aid) = 0;
  virtual std::vector<OnlineEntry> entries() const = 0;
  virtual int user_count() const = 0;  // never below 0 (BR-CHAR-027)
};

// Character persistence (system of record is the persistence module).
struct CharacterStore {
  virtual ~CharacterStore() = default;
  virtual std::vector<Character> list_by_account(AccountId aid) const = 0;
  virtual std::optional<Character> find(CharId id) const = 0;
  virtual bool name_exists(const std::string& name) const = 0;
  virtual CharId insert(Character c) = 0;  // assigns id
  virtual void update(const Character& c) = 0;
  virtual void erase(CharId id) = 0;
  virtual void set_all_offline() = 0;  // BR-CHAR-028
};

struct MembershipInfo {
  int party_id = 0;
  int guild_id = 0;
};

enum class AccountEventKind { Ban, Unban, StateChange, SexChange };
struct AccountEvent {
  AccountEventKind kind;
  AccountId account_id = 0;
  std::int64_t value = 0;  // new unban_time / state
};

// Cross-world messaging: kicks, broadcasts, handoff, membership queries.
struct WorldBus {
  virtual ~WorldBus() = default;
  virtual void kick(AccountId aid) = 0;  // BR-LOGIN-018
  virtual void broadcast(const AccountEvent& ev) = 0;
  virtual MembershipInfo membership(CharId id) const = 0;
  // World-owned cleanup for BR-CHAR-013. `plan` is the ordered list of cleanup step ids the policy
  // hook supplies (divorce, party, guild, mail, ... are world vocabulary, not the gateway's); the
  // world executes them in that order. The gateway deletes the character row AFTER this returns.
  virtual void purge_character_data(CharId id, const std::vector<std::string>& plan) = 0;
  virtual bool available() const = 0;  // false => refuse logins (failure-mode table)
};

struct AccountStore {
  virtual ~AccountStore() = default;
  virtual std::vector<Account> find_by_userid(const std::string& userid, bool case_sensitive) const = 0;
  virtual std::optional<Account> find(AccountId id) const = 0;
  virtual std::optional<AccountId> create(Account a) = 0;  // nullopt if id out of range / exists
  virtual void update(const Account& a) = 0;
  virtual void remove(AccountId id) = 0;
  virtual AccountId max_id() const = 0;
  virtual bool web_token_in_use(const std::string& token) const = 0;
  virtual void clear_all_web_tokens() = 0;  // BR-LOGIN-034: wiped at startup
};

// One-time auth ticket (30 s, single use).
struct TicketStore {
  virtual ~TicketStore() = default;
  virtual void issue(const AuthTicket& t) = 0;
  // Succeeds at most once per issued ticket and only inside the TTL. `hop` selects the stage that is
  // acceptable (Any tickets satisfy every hop; Login tickets are refused at the World hop and vice versa).
  virtual std::optional<AuthTicket> consume(AccountId aid, std::uint32_t id1, std::uint32_t id2, Timestamp now) = 0;
  // id2 / sex are checked only when non-null. A mismatch leaves the ticket in place.
  virtual std::optional<AuthTicket> consume_for(AuthStage hop, AccountId aid, std::uint32_t id1,
                                                const std::uint32_t* id2, const Sex* sex, Timestamp now) = 0;
  virtual void revoke(AccountId aid) = 0;
};

// Password hashing. Implementations may run on a bounded pool (see PooledHasher); hash()/verify()
// then throw HashOverloaded when the queue is full, and the login is refused (SEC-OVERLOAD).
struct VerifyResult {
  bool ok = false;
  bool needs_rehash = false;  // legacy plaintext/MD5 matched, or an older/weaker PHC string => upgrade
};
struct HashOverloaded : std::runtime_error {
  HashOverloaded() : std::runtime_error("hash queue full") {}
};
struct PasswordHasher {
  virtual ~PasswordHasher() = default;
  virtual std::string hash(const std::string& password) = 0;                 // -> PHC string
  virtual VerifyResult verify(PasswordScheme scheme, const std::string& stored,
                              const std::string& password) = 0;               // case-sensitive
  virtual std::string md5_hex(const std::string& data) = 0;                  // legacy shim only
  virtual std::string algorithm_name() const { return "unspecified"; }
};

// DNS blacklist lookup seam (BR-LOGIN-014). Production wraps the resolver on the gateway pool.
struct DnsblResolver {
  virtual ~DnsblResolver() = default;
  virtual bool listed(const std::string& ip) = 0;
};

// Audit sink for the login/char log (CAP-25, BR-LOGIN-039). The IP failure counter does NOT depend on it.
struct AuditSink {
  virtual ~AuditSink() = default;
  virtual void login_attempt(const std::string& ip, const std::string& userid, int code, const std::string& msg) = 0;
  virtual void char_event(AccountId aid, CharId cid, const std::string& what) = 0;
};

// Account-scoped global registry (legacy `##` variables) that must go with a deleted account
// (BR-LOGIN-037). Optional: the persistence adapter wires it to the GlobalAccount KV scope.
struct AccountRegistryPurger {
  virtual ~AccountRegistryPurger() = default;
  virtual void purge_account_registry(AccountId aid) = 0;
};

}  // namespace gateway
