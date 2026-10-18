#pragma once
// rathena-migrate-saves: legacy char/inventory/skill/sc_data/registries to the
// new store. Highest data-loss risk in the project (architecture section 8).
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "tooling/common.hpp"

namespace tooling::saves {

// Legacy database access is read-only by construction. Only SELECT/EXPLAIN-like
// statements pass; everything else is rejected before reaching the driver.
class ReadOnlyLegacyDb {
 public:
  // dsn must be an env placeholder or a credential-free value.
  explicit ReadOnlyLegacyDb(std::string dsn);
  static bool is_read_only_statement(const std::string& sql);
  // Throws std::runtime_error on non-read-only SQL.
  void guard(const std::string& sql) const;
 private:
  std::string dsn_;
};

enum class Mode { DryRun, Apply };

struct TableReconciliation {
  std::string table;
  uint64_t legacy_rows = 0, migrated_rows = 0, parked_orphaned = 0;
  std::string legacy_checksum, migrated_checksum;
  bool ok() const { return legacy_rows == migrated_rows + parked_orphaned; }
};
struct MigrationReport {
  Mode mode = Mode::DryRun;
  std::vector<TableReconciliation> tables;
  bool all_ok() const;
};

// sc_data: val1..val4 -> named fields per status (per-status mapping table).
struct LegacyStatusRow { int64_t type; int64_t val1, val2, val3, val4; int64_t tick; };
struct MappedStatus {
  bool mapped;
  NamespacedId id;                       // valid when mapped
  std::map<std::string, int64_t> fields; // named fields
  bool orphaned;                         // unmappable: parked, never dropped
  LegacyStatusRow original;              // retained verbatim when orphaned
};
MappedStatus map_status(const LegacyStatusRow& row);  // stub

// Registry sigils to namespaced KV under mod "legacy".
struct KvTarget { std::string scope; std::string mod; std::string key; };  // scope: character|account|world
KvTarget map_registry_key(const std::string& legacy_key, const std::string& table);  // stub

// Same-length checksum over rows (order-independent).
std::string table_checksum(const std::vector<std::string>& canonical_rows);

MigrationReport run_migration(Mode mode);  // stub
void rollback(const MigrationReport& report);  // stub: removes new-store rows only

}  // namespace tooling::saves
