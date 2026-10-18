#include "tooling/migrate_saves.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <sstream>

#include "tooling/config.hpp"

namespace tooling::saves {

ReadOnlyLegacyDb::ReadOnlyLegacyDb(std::string dsn) : dsn_(std::move(dsn)) {
  require_no_literal_credential("legacy_dsn", dsn_);
}

bool ReadOnlyLegacyDb::is_read_only_statement(const std::string& sql) {
  size_t i = 0;
  while (i < sql.size() && std::isspace(static_cast<unsigned char>(sql[i]))) ++i;
  std::string head;
  while (i < sql.size() && std::isalpha(static_cast<unsigned char>(sql[i])))
    head += static_cast<char>(std::toupper(static_cast<unsigned char>(sql[i++])));
  if (head != "SELECT" && head != "SHOW" && head != "DESCRIBE" && head != "EXPLAIN") return false;
  // Reject stacked statements and write clauses hiding behind a SELECT.
  std::string up;
  for (char c : sql) up += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  auto semi = up.find(';');
  if (semi != std::string::npos && up.find_first_not_of(" \t\r\n", semi + 1) != std::string::npos) return false;
  for (const char* bad : {"INTO OUTFILE", "INTO DUMPFILE", "FOR UPDATE", "LOCK IN SHARE"})
    if (up.find(bad) != std::string::npos) return false;
  return true;
}

void ReadOnlyLegacyDb::guard(const std::string& sql) const {
  if (!is_read_only_statement(sql))
    throw std::runtime_error("legacy database is read-only; rejected statement");
}

bool MigrationReport::all_ok() const {
  return std::all_of(tables.begin(), tables.end(), [](const auto& t) { return t.ok(); });
}

std::string table_checksum(const std::vector<std::string>& rows) {
  // Order-independent: sum and xor of FNV-1a row hashes.
  uint64_t sum = 0, x = 0;
  for (const auto& r : rows) {
    uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : r) { h ^= c; h *= 1099511628211ULL; }
    sum += h; x ^= h;
  }
  std::ostringstream o;
  o << std::hex << sum << ":" << x << ":" << rows.size();
  return o.str();
}

MappedStatus map_status(const LegacyStatusRow&) { throw NotImplemented("sc_data val1..val4 mapping"); }
KvTarget map_registry_key(const std::string&, const std::string&) { throw NotImplemented("registry sigil mapping"); }
MigrationReport run_migration(Mode) { throw NotImplemented("save migration run"); }
void rollback(const MigrationReport&) { throw NotImplemented("save migration rollback"); }

}  // namespace tooling::saves
