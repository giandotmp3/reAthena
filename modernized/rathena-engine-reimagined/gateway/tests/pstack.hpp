#pragma once
// Persistence stack for the adapter tests: SQLite file -> GuardedConnection -> FaultInjectingConnection,
// RecordStore + KvStore for reads, WriteBehindService + bounded WAL for writes.
#include <unistd.h>

#include <filesystem>

#include "gateway/persist_adapter.hpp"
#include "rathena/persist/connection_wrappers.hpp"
#include "rathena/persist/sqlite_connection.hpp"

namespace gt {

inline std::filesystem::path scratch(const std::string& name) {
  struct Cleaner {   // the whole per-process test directory goes away at exit
    std::filesystem::path root = std::filesystem::temp_directory_path() / ("gateway_tests_" + std::to_string(::getpid()));
    ~Cleaner() { std::error_code ec; std::filesystem::remove_all(root, ec); }
  };
  static Cleaner cleaner;
  auto p = std::filesystem::temp_directory_path() / ("gateway_tests_" + std::to_string(::getpid())) / name;
  std::filesystem::remove_all(p);
  std::filesystem::create_directories(p);
  return p;
}

struct PStack {
  explicit PStack(const std::filesystem::path& dir, std::uint64_t wal_bytes = 1u << 20, bool fresh_wal = true)
      : sqlite((dir / "gateway.sqlite").string()), guarded(sqlite), db(guarded) {
    db.connect();
    if (fresh_wal) std::filesystem::remove_all(dir / "wal");
    std::filesystem::create_directories(dir / "wal");
    wal = std::make_unique<rathena::persist::Wal>(rathena::persist::WalConfig{dir / "wal", wal_bytes});
    rathena::persist::WriteBehindOptions o;
    o.retry_interval = std::chrono::milliseconds(5);
    wb = std::make_unique<rathena::persist::WriteBehindService>(db, *wal, o);
    quarantine = std::make_unique<rathena::persist::Quarantine>(db);
    records = std::make_unique<rathena::persist::RecordStore>(db, leases, *quarantine);
    kv = std::make_unique<rathena::persist::KvStore>(db, *quarantine);
    bb = std::make_unique<gateway::PersistBackbone>(db, *wb, *records, *kv);
  }
  ~PStack() { if (wb) wb->shutdown(); }
  bool wait_durable(std::chrono::milliseconds limit = std::chrono::milliseconds(5000)) {
    return wb->wait_durable(bb->last_epoch(), limit);
  }
  std::int64_t count(const char* sql) {
    const auto rows = guarded.query({sql, {}});
    if (rows.empty()) return -1;
    const auto& v = rows.front().begin()->second;
    if (const auto* i = std::get_if<std::int64_t>(&v)) return *i;
    return -1;
  }
  bool eventually(const std::function<bool()>& p, std::chrono::milliseconds limit = std::chrono::milliseconds(5000)) {
    const auto end = std::chrono::steady_clock::now() + limit;
    while (std::chrono::steady_clock::now() < end) {
      if (p()) return true;
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return p();
  }
  rathena::persist::SqliteConnection sqlite;
  rathena::persist::GuardedConnection guarded;
  rathena::persist::FaultInjectingConnection db;
  rathena::persist::OnlineLeases leases;
  std::unique_ptr<rathena::persist::Wal> wal;
  std::unique_ptr<rathena::persist::WriteBehindService> wb;
  std::unique_ptr<rathena::persist::Quarantine> quarantine;
  std::unique_ptr<rathena::persist::RecordStore> records;
  std::unique_ptr<rathena::persist::KvStore> kv;
  std::unique_ptr<gateway::PersistBackbone> bb;
};

}  // namespace gt
