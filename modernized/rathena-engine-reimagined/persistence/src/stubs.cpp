// SCAFFOLD: every method below is declared by the interface contract and not built yet.
// Acceptance tests in tests/ exercise these and are skipped (tagged PENDING <rule id>) until
// the bodies exist. Replace a stub, then remove its pending gate in tests/.
#include "rathena/persist/service.hpp"

namespace rathena::persist {
namespace {
[[noreturn]] void todo(const char* what) { throw NotImplementedError(std::string("not implemented: ") + what); }
} // namespace

Quarantine::Quarantine(SqlConnection& db) : db_(db) {}
std::uint64_t Quarantine::put(QuarantinedRow) { todo("Quarantine::put"); }
std::vector<QuarantinedRow> Quarantine::list() const { todo("Quarantine::list"); }
bool Quarantine::contains(const std::string&, const std::string&) const { todo("Quarantine::contains"); }
std::optional<QuarantinedRow> Quarantine::release(std::uint64_t) { todo("Quarantine::release"); }

void OnlineLeases::acquire(const OwnerRef&) { todo("OnlineLeases::acquire"); }
void OnlineLeases::release(const OwnerRef&) { todo("OnlineLeases::release"); }
bool OnlineLeases::held(const OwnerRef&) const { todo("OnlineLeases::held"); }

RecordStore::RecordStore(SqlConnection& db, const OnlineLeases& l, Quarantine& q) : db_(db), leases_(l), quarantine_(q) {}
void RecordStore::set_collection_policy(const std::string&, CollectionPolicy) { todo("RecordStore::set_collection_policy"); }
SaveOutcome RecordStore::save(const SaveRequest&) { todo("RecordStore::save"); }
std::optional<Record> RecordStore::load(const RecordKey&) { todo("RecordStore::load"); }
void RecordStore::save_collection(const RecordKey&, const std::vector<Blob>&) { todo("RecordStore::save_collection"); }
std::vector<Blob> RecordStore::load_collection(const RecordKey&) { todo("RecordStore::load_collection"); }

void KvMigrationRegistry::register_step(const std::string&, std::uint32_t, Fn) { todo("KvMigrationRegistry::register_step"); }
bool KvMigrationRegistry::has_path(const std::string&, std::uint32_t, std::uint32_t) const { todo("KvMigrationRegistry::has_path"); }
Value KvMigrationRegistry::upgrade(const std::string&, std::uint32_t, std::uint32_t, Value) const { todo("KvMigrationRegistry::upgrade"); }

KvStore::KvStore(SqlConnection& db, Quarantine& q, KvPolicy p) : db_(db), quarantine_(q), policy_(p) {}
std::optional<Value> KvStore::get(const KvKey&) { todo("KvStore::get"); }
void KvStore::set(const KvKey&, Value) { todo("KvStore::set"); }
bool KvStore::erase(const KvKey&) { todo("KvStore::erase"); }
std::uint64_t KvStore::bytes_used(const std::string&) const { todo("KvStore::bytes_used"); }

void DirtyTracker::mark(Record) { todo("DirtyTracker::mark(Record)"); }
void DirtyTracker::mark(KvEntry) { todo("DirtyTracker::mark(KvEntry)"); }
DirtyBatchPtr DirtyTracker::swap_out() { todo("DirtyTracker::swap_out"); }
std::size_t DirtyTracker::pending() const { todo("DirtyTracker::pending"); }

Wal::Wal(WalConfig cfg) : cfg_(std::move(cfg)) {}
void Wal::append(const DirtyBatch&) { todo("Wal::append"); }
std::size_t Wal::replay(const std::function<void(const DirtyBatch&)>&) { todo("Wal::replay"); }
std::uint64_t Wal::bytes_used() const { todo("Wal::bytes_used"); }
bool Wal::accepting_writes() const { todo("Wal::accepting_writes"); }

WriteBehindFlusher::WriteBehindFlusher(SqlConnection& db, DirtyTracker& t, Wal& w) : db_(db), tracker_(t), wal_(w) {}
FlushResult WriteBehindFlusher::flush_once() { todo("WriteBehindFlusher::flush_once"); }
std::size_t WriteBehindFlusher::drain_wal() { todo("WriteBehindFlusher::drain_wal"); }

MigrationRunner::MigrationRunner(SqlConnection& db, std::vector<Migration> m) : db_(db), migrations_(std::move(m)) {}
std::uint32_t MigrationRunner::current_version() { todo("MigrationRunner::current_version"); }
std::vector<std::uint32_t> MigrationRunner::apply_pending() { todo("MigrationRunner::apply_pending"); }

IdMap::IdMap(SqlConnection& db, std::map<std::string, IdRange> r) : db_(db), ranges_(std::move(r)) {}
std::uint32_t IdMap::assign(const std::string&, const std::string&, std::optional<std::uint32_t>) { todo("IdMap::assign"); }
std::optional<std::uint32_t> IdMap::client_id(const std::string&, const std::string&) const { todo("IdMap::client_id"); }
std::optional<std::string> IdMap::namespaced_id(const std::string&, std::uint32_t) const { todo("IdMap::namespaced_id"); }
void IdMap::retire(const std::string&, const std::string&) { todo("IdMap::retire"); }
bool IdMap::is_tombstoned(const std::string&, std::uint32_t) const { todo("IdMap::is_tombstoned"); }

} // namespace rathena::persist
