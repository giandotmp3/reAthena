#pragma once
// The three architecture seams (REIMAGINED_ARCHITECTURE section 3) plus the
// stores the gateway needs. In-process implementations live in inproc.hpp; a
// future gateway/world split is a new implementation of these, not a redesign.
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
  // World-owned cleanup for BR-CHAR-013 (divorce, party, guild, mail, ...).
  virtual void purge_character_data(CharId id) = 0;
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
};

// One-time auth ticket (30 s, single use).
struct TicketStore {
  virtual ~TicketStore() = default;
  virtual void issue(const AuthTicket& t) = 0;
  // Succeeds at most once per issued ticket and only inside the TTL.
  virtual std::optional<AuthTicket> consume(AccountId aid, std::uint32_t id1, std::uint32_t id2, Timestamp now) = 0;
};

// Argon2id hashing pool seam. Implementations run on a bounded gateway-owned
// pool so hashing never touches the simulation thread.
struct VerifyResult {
  bool ok = false;
  bool needs_rehash = false;  // legacy plaintext/MD5 matched => upgrade (lazy rehash)
};
struct PasswordHasher {
  virtual ~PasswordHasher() = default;
  virtual std::string hash(const std::string& password) = 0;                 // -> Argon2id string
  virtual VerifyResult verify(PasswordScheme scheme, const std::string& stored,
                              const std::string& password) = 0;               // case-sensitive
  virtual std::string md5_hex(const std::string& data) = 0;                  // legacy shim only
};

}  // namespace gateway
