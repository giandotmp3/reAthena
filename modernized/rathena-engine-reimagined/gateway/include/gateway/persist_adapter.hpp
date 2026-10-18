#pragma once
// CharacterStore / AccountStore / AccountRegistryPurger over the persistence unit (SQLite or
// PostgreSQL behind SqlConnection). Reads go to the database through RecordStore / KvStore; writes go
// through WriteBehindService (bounded WAL while the database is down). Read-your-writes is kept by a
// small "pending" overlay that is dropped once the write is durable in the database.
//
// Failure behaviour (architecture 3.1): while the database is unreachable every read throws
// StoreUnavailable (the login service turns that into a refusal), while writes made before the outage
// was noticed stay queued in the WAL and replay exactly once when the database returns. When the WAL is
// full, writes throw StoreUnavailable too (refused, never silently dropped).
//
// Not thread-safe by itself: callers serialise on the gateway strand, like the in-process stores.
// Lifetime: shut the WriteBehindService down (or destroy it) BEFORE the backbone; its writer thread calls
// back into the backbone after every batch.
#include <atomic>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "gateway/seams.hpp"
#include "rathena/persist/service.hpp"
#include "rathena/persist/writer.hpp"

namespace gateway {

class PersistBackbone {
 public:
  PersistBackbone(rathena::persist::SqlConnection& db, rathena::persist::WriteBehindService& wb,
                  rathena::persist::RecordStore& records, rathena::persist::KvStore& kv);
  /// Throws StoreUnavailable when the database cannot be reached or writes are being refused.
  void require_up();
  bool reads_available() noexcept;

  std::optional<std::string> get_record(rathena::persist::OwnerScope scope, std::uint64_t id, const std::string& kind);
  void put_record(rathena::persist::OwnerScope scope, std::uint64_t id, const std::string& kind, std::string payload);
  std::optional<std::string> get_kv(const std::string& key);
  void put_kv(const std::string& key, const std::string& value);
  void erase_kv(const std::string& key);
  /// Erase every KV row of one owner and mod (registry purge); returns how many rows were erased.
  std::size_t erase_owner_kv(rathena::persist::OwnerScope scope, const std::string& owner_ref, const std::string& mod_id);
  /// Keys with the given prefix, database plus pending overlay.
  std::vector<std::string> kv_keys(const std::string& prefix);

  /// Highest epoch handed off so far; wait_durable(epoch) blocks until the batch is in the database or WAL.
  std::uint64_t last_epoch() const { return last_epoch_; }
  rathena::persist::WriteBehindService& writer() { return wb_; }
  /// A write is refused BEFORE it is marked when the writer's queue already holds this many batches
  /// (WriteBehindOptions::max_queued_batches is 256 by default), so a refused write is never applied later.
  void set_backlog_limit(std::size_t n) { backlog_limit_ = n; }
  static constexpr const char* kMod = "gateway";

 private:
  struct PendingRec { std::string payload; std::uint64_t epoch; std::uint64_t revision; };
  struct PendingKv { std::optional<std::string> value; std::uint64_t epoch; std::uint64_t revision; };
  void prune();
  void check_can_write();
  std::uint64_t next_revision(std::uint64_t prev);
  rathena::persist::SqlConnection& db_;
  rathena::persist::WriteBehindService& wb_;
  rathena::persist::RecordStore& records_;
  rathena::persist::KvStore& kv_;
  std::map<std::string, PendingRec> pending_rec_;
  std::map<std::string, PendingKv> pending_kv_;
  std::map<std::string, std::uint64_t> rev_rec_, rev_kv_;
  std::uint64_t last_epoch_ = 0;
  std::size_t backlog_limit_ = 256;
  std::atomic<std::uint64_t> applied_epoch_{0};   // highest batch epoch that reached the DATABASE (not only the WAL)
};

class PersistCharacterStore : public CharacterStore {
 public:
  explicit PersistCharacterStore(PersistBackbone& bb) : bb_(bb) {}
  std::vector<Character> list_by_account(AccountId aid) const override;
  std::optional<Character> find(CharId id) const override;
  bool name_exists(const std::string& name) const override;
  CharId insert(Character c) override;
  void update(const Character& c) override;
  void erase(CharId id) override;
  void set_all_offline() override;
 private:
  PersistBackbone& bb_;  // a reference: const methods may still call through it
  std::optional<CharId> next_;
};

class PersistAccountStore : public AccountStore {
 public:
  explicit PersistAccountStore(PersistBackbone& bb) : bb_(bb) {}
  std::vector<Account> find_by_userid(const std::string& userid, bool case_sensitive) const override;
  std::optional<Account> find(AccountId id) const override;
  std::optional<AccountId> create(Account a) override;
  void update(const Account& a) override;
  void remove(AccountId id) override;
  AccountId max_id() const override;
  bool web_token_in_use(const std::string& token) const override;
  void clear_all_web_tokens() override;
 private:
  PersistBackbone& bb_;  // a reference: const methods may still call through it
  mutable std::optional<AccountId> max_id_;
};

// BR-LOGIN-036/037: the `##` registry of an account lives in the GlobalAccount KV scope, per mod.
class PersistRegistryPurger : public AccountRegistryPurger {
 public:
  PersistRegistryPurger(PersistBackbone& bb, std::vector<std::string> mod_ids) : bb_(bb), mods_(std::move(mod_ids)) {}
  void purge_account_registry(AccountId aid) override;
 private:
  PersistBackbone& bb_;
  std::vector<std::string> mods_;
};

// Serialisation (also used by tests).
std::string encode_character(const Character& c);
std::optional<Character> decode_character(const std::string& bytes);
std::string encode_account(const Account& a);
std::optional<Account> decode_account(const std::string& bytes);

}  // namespace gateway
