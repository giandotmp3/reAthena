#include "gateway/persist_adapter.hpp"

#include <algorithm>
#include <chrono>
#include <strings.h>

#include "rathena/persist/codec.hpp"

namespace gateway {

namespace rp = rathena::persist;

namespace {

constexpr const char* kCharKind = "gateway:char";
constexpr const char* kIndexKind = "gateway:char_index";
constexpr const char* kAccountKind = "gateway:account";
constexpr CharId kFirstCharId = 150000;
constexpr std::uint8_t kFormat = 1;
const char kTombstone[] = "\x00T";  // two bytes: deleted marker (never a valid serialised record)

std::string tomb() { return std::string(kTombstone, 2); }
bool is_tomb(const std::string& s) { return s.size() == 2 && s[0] == '\0' && s[1] == 'T'; }

std::string lower(std::string s) {
  for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

template <class F>
auto guarded(F&& f) -> decltype(f()) {
  try {
    return f();
  } catch (const rp::WalFullError& e) {
    throw StoreUnavailable(std::string("writes refused: ") + e.what());
  } catch (const rp::WriteBacklogError& e) {
    throw StoreUnavailable(std::string("write backlog: ") + e.what());
  } catch (const rp::ServiceClosedError& e) {
    throw StoreUnavailable(std::string("persistence closed: ") + e.what());
  } catch (const rp::RepositoryError& e) {
    throw StoreUnavailable(std::string("database unavailable: ") + e.what());
  }
}

}  // namespace

// ---- (de)serialisation ------------------------------------------------------------------------------
std::string encode_character(const Character& c) {
  rp::ByteWriter w;
  w.u8(kFormat);
  w.u32(c.id); w.u32(c.account_id); w.str(c.name);
  w.i64(c.slot); w.i64(c.base_level); w.u8(static_cast<std::uint8_t>(c.sex));
  for (int v : {c.str, c.agi, c.vit, c.int_, c.dex, c.luk, c.max_hp, c.max_sp}) w.i64(v);
  w.i64(c.delete_date); w.i64(c.unban_time); w.i64(c.rename_counter); w.i64(c.slot_moves);
  w.u8(c.online ? 1 : 0);
  w.u32(static_cast<std::uint32_t>(c.extra.size()));
  for (const auto& [k, v] : c.extra) { w.str(k); w.i64(v); }
  return w.bytes();
}

std::optional<Character> decode_character(const std::string& bytes) {
  try {
    rp::ByteReader r(bytes);
    if (r.u8() != kFormat) return std::nullopt;
    Character c;
    c.id = r.u32(); c.account_id = r.u32(); c.name = r.str();
    c.slot = static_cast<int>(r.i64()); c.base_level = static_cast<int>(r.i64());
    c.sex = static_cast<Sex>(r.u8());
    int* f[] = {&c.str, &c.agi, &c.vit, &c.int_, &c.dex, &c.luk, &c.max_hp, &c.max_sp};
    for (int* p : f) *p = static_cast<int>(r.i64());
    c.delete_date = r.i64(); c.unban_time = r.i64();
    c.rename_counter = static_cast<int>(r.i64()); c.slot_moves = static_cast<int>(r.i64());
    c.online = r.u8() != 0;
    const std::uint32_t n = r.count(12);
    for (std::uint32_t i = 0; i < n; ++i) { std::string k = r.str(); c.extra[k] = r.i64(); }
    if (!r.done()) return std::nullopt;
    return c;
  } catch (const rp::DecodeError&) {
    return std::nullopt;
  }
}

std::string encode_account(const Account& a) {
  rp::ByteWriter w;
  w.u8(kFormat);
  w.u32(a.id); w.str(a.userid); w.str(a.stored_secret); w.u8(static_cast<std::uint8_t>(a.scheme));
  w.str(a.challenge_secret); w.u8(static_cast<std::uint8_t>(a.sex));
  w.i64(a.group_id); w.i64(a.old_group); w.i64(a.state); w.i64(a.unban_time); w.i64(a.expiration_time);
  w.u32(a.login_id1); w.u32(a.login_id2); w.i64(a.logincount); w.str(a.last_ip); w.i64(a.lastlogin);
  w.i64(a.char_slots); w.i64(a.vip_time); w.str(a.email); w.str(a.birthdate); w.str(a.web_token);
  w.u8(a.web_token_enabled ? 1 : 0); w.str(a.pin); w.i64(a.pin_changed); w.i64(a.pin_failures);
  w.i64(a.pin_last_failure);
  return w.bytes();
}

std::optional<Account> decode_account(const std::string& bytes) {
  try {
    rp::ByteReader r(bytes);
    if (r.u8() != kFormat) return std::nullopt;
    Account a;
    a.id = r.u32(); a.userid = r.str(); a.stored_secret = r.str();
    a.scheme = static_cast<PasswordScheme>(r.u8());
    a.challenge_secret = r.str(); a.sex = static_cast<Sex>(r.u8());
    a.group_id = static_cast<int>(r.i64()); a.old_group = static_cast<int>(r.i64());
    a.state = static_cast<int>(r.i64()); a.unban_time = r.i64(); a.expiration_time = r.i64();
    a.login_id1 = r.u32(); a.login_id2 = r.u32(); a.logincount = r.i64(); a.last_ip = r.str(); a.lastlogin = r.i64();
    a.char_slots = static_cast<int>(r.i64()); a.vip_time = r.i64(); a.email = r.str(); a.birthdate = r.str();
    a.web_token = r.str(); a.web_token_enabled = r.u8() != 0; a.pin = r.str(); a.pin_changed = r.i64();
    a.pin_failures = static_cast<int>(r.i64()); a.pin_last_failure = r.i64();
    if (!r.done()) return std::nullopt;
    return a;
  } catch (const rp::DecodeError&) {
    return std::nullopt;
  }
}

// ---- backbone ------------------------------------------------------------------------------------------
PersistBackbone::PersistBackbone(rp::SqlConnection& db, rp::WriteBehindService& wb, rp::RecordStore& records,
                                 rp::KvStore& kv)
    : db_(db), wb_(wb), records_(records), kv_(kv) {
  // The overlay may only forget a write once the database itself has it: a batch parked in the WAL is
  // durable but not readable, so read-your-writes must keep serving it until it is applied.
  wb_.set_applied_callback([this](const rp::DirtyBatch& b) {
    std::uint64_t cur = applied_epoch_.load();
    while (b.epoch > cur && !applied_epoch_.compare_exchange_weak(cur, b.epoch)) {}
  });
}

bool PersistBackbone::reads_available() noexcept {
  try {
    db_.query({"SELECT 1 AS ok", {}});
    return true;
  } catch (...) {
    return false;
  }
}

void PersistBackbone::require_up() {
  if (wb_.refusing_writes()) throw StoreUnavailable("persistence is refusing writes (WAL full)");
  if (!reads_available()) throw StoreUnavailable("database unavailable");
}

void PersistBackbone::check_can_write() {
  if (wb_.refusing_writes()) throw StoreUnavailable("writes refused: WAL full");
  if (wb_.stats().queued >= backlog_limit_) throw StoreUnavailable("write backlog: writer queue full");
}

std::uint64_t PersistBackbone::next_revision(std::uint64_t prev) {
  const auto now = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
  return std::max(prev + 1, now);
}

void PersistBackbone::prune() {
  const std::uint64_t durable = applied_epoch_.load();
  for (auto it = pending_rec_.begin(); it != pending_rec_.end();)
    it = (it->second.epoch <= durable) ? pending_rec_.erase(it) : std::next(it);
  for (auto it = pending_kv_.begin(); it != pending_kv_.end();)
    it = (it->second.epoch <= durable) ? pending_kv_.erase(it) : std::next(it);
}

std::optional<std::string> PersistBackbone::get_record(rp::OwnerScope scope, std::uint64_t id, const std::string& kind) {
  const std::string pk = std::to_string(static_cast<int>(scope)) + ":" + std::to_string(id) + ":" + kind;
  prune();
  if (auto it = pending_rec_.find(pk); it != pending_rec_.end()) return it->second.payload;
  return guarded([&]() -> std::optional<std::string> {
    try {
      auto rec = records_.load(rp::RecordKey{rp::OwnerRef{scope, id}, kind});
      if (!rec) return std::nullopt;
      rev_rec_[pk] = std::max(rev_rec_[pk], rec->revision);
      return rec->payload.bytes;
    } catch (const rp::QuarantinedRowError&) {
      return std::nullopt;  // a quarantined row is treated as missing; the operator releases it
    }
  });
}

void PersistBackbone::put_record(rp::OwnerScope scope, std::uint64_t id, const std::string& kind, std::string payload) {
  const std::string pk = std::to_string(static_cast<int>(scope)) + ":" + std::to_string(id) + ":" + kind;
  rp::Record rec;
  rec.key = rp::RecordKey{rp::OwnerRef{scope, id}, kind};
  rec.payload = rp::Blob{payload};
  check_can_write();
  rec.revision = next_revision(rev_rec_[pk]);
  rev_rec_[pk] = rec.revision;
  guarded([&] {
    wb_.mark(rec);
    const auto h = wb_.handoff();
    const std::uint64_t epoch = h.epoch ? h.epoch : last_epoch_;
    last_epoch_ = std::max(last_epoch_, epoch);
    pending_rec_[pk] = PendingRec{std::move(payload), epoch, rec.revision};
    return 0;
  });
}

std::optional<std::string> PersistBackbone::get_kv(const std::string& key) {
  prune();
  if (auto it = pending_kv_.find(key); it != pending_kv_.end()) return it->second.value;
  return guarded([&]() -> std::optional<std::string> {
    try {
      auto v = kv_.get(rp::KvKey{rp::OwnerRef{rp::OwnerScope::World, 0}, kMod, key});
      if (!v) return std::nullopt;
      if (const auto* s = std::get_if<std::string>(&v->data)) return *s;
      return std::nullopt;
    } catch (const rp::QuarantinedRowError&) {
      return std::nullopt;
    }
  });
}

void PersistBackbone::put_kv(const std::string& key, const std::string& value) {
  rp::KvEntry e;
  e.key = rp::KvKey{rp::OwnerRef{rp::OwnerScope::World, 0}, kMod, key};
  e.value = rp::Value(value);
  check_can_write();
  e.revision = next_revision(rev_kv_[key]);
  rev_kv_[key] = e.revision;
  guarded([&] {
    wb_.mark(e);
    const auto h = wb_.handoff();
    const std::uint64_t epoch = h.epoch ? h.epoch : last_epoch_;
    last_epoch_ = std::max(last_epoch_, epoch);
    pending_kv_[key] = PendingKv{value, epoch, e.revision};
    return 0;
  });
}

void PersistBackbone::erase_kv(const std::string& key) {
  rp::KvEntry e;
  e.key = rp::KvKey{rp::OwnerRef{rp::OwnerScope::World, 0}, kMod, key};
  e.erased = true;
  check_can_write();
  e.revision = next_revision(rev_kv_[key]);
  rev_kv_[key] = e.revision;
  guarded([&] {
    wb_.mark(e);
    const auto h = wb_.handoff();
    const std::uint64_t epoch = h.epoch ? h.epoch : last_epoch_;
    last_epoch_ = std::max(last_epoch_, epoch);
    pending_kv_[key] = PendingKv{std::nullopt, epoch, e.revision};
    return 0;
  });
}

std::vector<std::string> PersistBackbone::kv_keys(const std::string& prefix) {
  prune();
  std::vector<std::string> out = guarded([&] { return kv_.keys(rp::OwnerScope::World, "0", kMod); });
  out.erase(std::remove_if(out.begin(), out.end(), [&](const std::string& k) { return k.rfind(prefix, 0) != 0; }), out.end());
  for (const auto& [k, v] : pending_kv_) {
    if (k.rfind(prefix, 0) != 0) continue;
    auto it = std::find(out.begin(), out.end(), k);
    if (v.value && it == out.end()) out.push_back(k);
    if (!v.value && it != out.end()) out.erase(it);
  }
  std::sort(out.begin(), out.end());
  return out;
}

std::size_t PersistBackbone::erase_owner_kv(rp::OwnerScope scope, const std::string& owner_ref, const std::string& mod_id) {
  const auto keys = guarded([&] { return kv_.keys(scope, owner_ref, mod_id); });
  for (const std::string& k : keys) {
    rp::KvEntry e;
    e.key = rp::KvKey{rp::OwnerRef{scope, 0}, mod_id, k, owner_ref};
    e.erased = true;
    const std::string rk = std::to_string(static_cast<int>(scope)) + "/" + owner_ref + "/" + mod_id + "/" + k;
    e.revision = next_revision(rev_kv_[rk]);
    rev_kv_[rk] = e.revision;
    guarded([&] { wb_.mark(e); return 0; });
  }
  if (!keys.empty()) guarded([&] { const auto h = wb_.handoff(); last_epoch_ = std::max(last_epoch_, h.epoch); return 0; });
  return keys.size();
}

// ---- characters -------------------------------------------------------------------------------------------
namespace {
std::vector<CharId> decode_index(const std::optional<std::string>& s) {
  std::vector<CharId> ids;
  if (!s) return ids;
  try {
    rp::ByteReader r(*s);
    const auto n = r.count(4);
    for (std::uint32_t i = 0; i < n; ++i) ids.push_back(r.u32());
  } catch (const rp::DecodeError&) {
    ids.clear();
  }
  return ids;
}
std::string encode_index(const std::vector<CharId>& ids) {
  rp::ByteWriter w;
  w.u32(static_cast<std::uint32_t>(ids.size()));
  for (CharId i : ids) w.u32(i);
  return w.bytes();
}
}  // namespace

std::optional<Character> PersistCharacterStore::find(CharId id) const {
  bb_.require_up();
  auto s = bb_.get_record(rp::OwnerScope::Character, id, kCharKind);
  if (!s || is_tomb(*s)) return std::nullopt;
  return decode_character(*s);
}

std::vector<Character> PersistCharacterStore::list_by_account(AccountId aid) const {
  bb_.require_up();
  std::vector<Character> out;
  for (CharId id : decode_index(bb_.get_record(rp::OwnerScope::Account, aid, kIndexKind)))
    if (auto c = find(id)) out.push_back(*c);
  std::sort(out.begin(), out.end(), [](const Character& a, const Character& b) { return a.slot < b.slot; });
  return out;
}

bool PersistCharacterStore::name_exists(const std::string& name) const {
  bb_.require_up();
  return bb_.get_kv("name/" + lower(name)).has_value();
}

CharId PersistCharacterStore::insert(Character c) {
  bb_.require_up();
  if (!next_) {
    auto v = bb_.get_kv("next_char_id");
    next_ = v ? static_cast<CharId>(std::stoul(*v)) : kFirstCharId;
  }
  c.id = (*next_)++;
  bb_.put_kv("next_char_id", std::to_string(*next_));
  bb_.put_record(rp::OwnerScope::Character, c.id, kCharKind, encode_character(c));
  bb_.put_kv("name/" + lower(c.name), std::to_string(c.id));
  auto idx = decode_index(bb_.get_record(rp::OwnerScope::Account, c.account_id, kIndexKind));
  idx.push_back(c.id);
  bb_.put_record(rp::OwnerScope::Account, c.account_id, kIndexKind, encode_index(idx));
  return c.id;
}

void PersistCharacterStore::update(const Character& c) {
  bb_.require_up();
  if (auto old = find(c.id); old && lower(old->name) != lower(c.name)) {
    bb_.erase_kv("name/" + lower(old->name));
    bb_.put_kv("name/" + lower(c.name), std::to_string(c.id));
  }
  bb_.put_record(rp::OwnerScope::Character, c.id, kCharKind, encode_character(c));
}

void PersistCharacterStore::erase(CharId id) {
  bb_.require_up();
  auto old = find(id);
  if (!old) return;
  bb_.put_record(rp::OwnerScope::Character, id, kCharKind, tomb());
  bb_.erase_kv("name/" + lower(old->name));
  auto idx = decode_index(bb_.get_record(rp::OwnerScope::Account, old->account_id, kIndexKind));
  idx.erase(std::remove(idx.begin(), idx.end(), id), idx.end());
  bb_.put_record(rp::OwnerScope::Account, old->account_id, kIndexKind, encode_index(idx));
}

void PersistCharacterStore::set_all_offline() {
  bb_.require_up();
  for (const std::string& k : bb_.kv_keys("name/")) {
    auto v = bb_.get_kv(k);
    if (!v) continue;
    if (auto c = find(static_cast<CharId>(std::stoul(*v))); c && c->online) {
      Character cc = *c;
      cc.online = false;
      update(cc);
    }
  }
}

// ---- accounts ----------------------------------------------------------------------------------------------
std::optional<Account> PersistAccountStore::find(AccountId id) const {
  bb_.require_up();
  auto s = bb_.get_record(rp::OwnerScope::Account, id, kAccountKind);
  if (!s || is_tomb(*s)) return std::nullopt;
  return decode_account(*s);
}

std::vector<Account> PersistAccountStore::find_by_userid(const std::string& userid, bool cs) const {
  bb_.require_up();
  std::vector<Account> out;
  if (cs) {
    if (auto v = bb_.get_kv("uid/" + userid))
      if (auto a = find(static_cast<AccountId>(std::stoul(*v))); a && a->userid == userid) out.push_back(*a);
    return out;
  }
  if (auto v = bb_.get_kv("uidl/" + lower(userid))) {
    std::size_t pos = 0;
    while (pos < v->size()) {
      const std::size_t comma = v->find(',', pos);
      const std::string tok = v->substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
      if (!tok.empty())
        if (auto a = find(static_cast<AccountId>(std::stoul(tok)))) out.push_back(*a);
      if (comma == std::string::npos) break;
      pos = comma + 1;
    }
  }
  return out;
}

AccountId PersistAccountStore::max_id() const {
  bb_.require_up();
  if (!max_id_) {
    auto v = bb_.get_kv("max_account_id");
    max_id_ = v ? static_cast<AccountId>(std::stoul(*v)) : 0;
  }
  return *max_id_;
}

std::optional<AccountId> PersistAccountStore::create(Account a) {
  bb_.require_up();
  if (a.id == 0 || a.id > 100000000) return std::nullopt;
  if (find(a.id)) return std::nullopt;
  const AccountId top = max_id();
  bb_.put_record(rp::OwnerScope::Account, a.id, kAccountKind, encode_account(a));
  bb_.put_kv("uid/" + a.userid, std::to_string(a.id));
  std::string list = bb_.get_kv("uidl/" + lower(a.userid)).value_or("");
  list += (list.empty() ? "" : ",") + std::to_string(a.id);
  bb_.put_kv("uidl/" + lower(a.userid), list);
  if (!a.web_token.empty()) bb_.put_kv("wt/" + a.web_token, std::to_string(a.id));
  if (a.id > top) { bb_.put_kv("max_account_id", std::to_string(a.id)); max_id_ = a.id; }
  return a.id;
}

void PersistAccountStore::update(const Account& a) {
  bb_.require_up();
  const auto old = find(a.id);
  if (old && old->web_token != a.web_token) {
    if (!old->web_token.empty()) bb_.erase_kv("wt/" + old->web_token);
    if (!a.web_token.empty()) bb_.put_kv("wt/" + a.web_token, std::to_string(a.id));
  } else if (!old && !a.web_token.empty()) {
    bb_.put_kv("wt/" + a.web_token, std::to_string(a.id));
  }
  bb_.put_record(rp::OwnerScope::Account, a.id, kAccountKind, encode_account(a));
}

void PersistAccountStore::remove(AccountId id) {
  bb_.require_up();
  auto old = find(id);
  if (!old) return;
  bb_.put_record(rp::OwnerScope::Account, id, kAccountKind, tomb());
  bb_.erase_kv("uid/" + old->userid);
  std::string list = bb_.get_kv("uidl/" + lower(old->userid)).value_or("");
  std::string kept;
  std::size_t pos = 0;
  while (pos < list.size()) {
    const std::size_t comma = list.find(',', pos);
    const std::string tok = list.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
    if (!tok.empty() && tok != std::to_string(id)) kept += (kept.empty() ? "" : ",") + tok;
    if (comma == std::string::npos) break;
    pos = comma + 1;
  }
  if (kept.empty()) bb_.erase_kv("uidl/" + lower(old->userid)); else bb_.put_kv("uidl/" + lower(old->userid), kept);
  if (!old->web_token.empty()) bb_.erase_kv("wt/" + old->web_token);
}

bool PersistAccountStore::web_token_in_use(const std::string& token) const {
  bb_.require_up();
  return bb_.get_kv("wt/" + token).has_value();
}

void PersistAccountStore::clear_all_web_tokens() {
  bb_.require_up();
  for (const std::string& k : bb_.kv_keys("wt/")) {
    auto v = bb_.get_kv(k);
    if (v) {
      if (auto a = find(static_cast<AccountId>(std::stoul(*v)))) {
        Account aa = *a;
        aa.web_token.clear();
        aa.web_token_enabled = false;
        bb_.put_record(rp::OwnerScope::Account, aa.id, kAccountKind, encode_account(aa));
      }
    }
    bb_.erase_kv(k);
  }
}

void PersistRegistryPurger::purge_account_registry(AccountId aid) {
  bb_.require_up();
  for (const std::string& mod : mods_) bb_.erase_owner_kv(rp::OwnerScope::GlobalAccount, std::to_string(aid), mod);
}

}  // namespace gateway
