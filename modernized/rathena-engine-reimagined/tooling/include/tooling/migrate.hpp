#pragma once
// rathena-migrate: legacy YAML db -> Luau data modules in the two ruleset
// packages. Reproduces legacy merge semantics (section 4.4 of the spec).
#include <cstdint>
#include <map>
#include <string>
#include <variant>
#include <vector>

#include "tooling/common.hpp"

namespace tooling::migrate {

struct Value;
using Map = std::map<std::string, Value>;
using List = std::vector<Value>;
struct Value {
  std::variant<std::monostate, bool, int64_t, std::string, List, Map> v;
  Value() = default;
  Value(bool b) : v(b) {}
  Value(int64_t i) : v(i) {}
  Value(int i) : v(static_cast<int64_t>(i)) {}
  Value(const char* s) : v(std::string(s)) {}
  Value(std::string s) : v(std::move(s)) {}
  Value(List l) : v(std::move(l)) {}
  Value(Map m) : v(std::move(m)) {}
  bool operator==(const Value& o) const { return v == o.v; }
  const Map* map() const { return std::get_if<Map>(&v); }
  const List* list() const { return std::get_if<List>(&v); }
};

// Per-field "last file wins". Maps merge recursively. A list whose elements
// are maps carrying "Index" is patched by Index (matching entries merge,
// unmatched entries append). Any other list or scalar is replaced.
Value merge_fields(const Value& base, const Value& overlay);

// Merge an ordered chain of DB files (db/<set>/x.yml, then db/import/x.yml...)
// keyed by the record's identity field (e.g. "Id" or "Aegis"). Later wins.
Map merge_records(const std::vector<List>& files_in_order, const std::string& key_field);

// Mode imports: boolean flag map where an import may set or clear single flags.
Value merge_mode(const Value& base, const Value& overlay);  // stub

// Parse the YAML subset used by the legacy db (Header/Body/Footer.Imports).
std::vector<List> load_db_chain(const std::string& db_dir, const std::string& name, DbSet set);  // stub

// Emit a Luau data module for one definition kind.
std::string emit_luau_module(const std::string& mod_id, const std::string& kind, const Map& records);  // stub

// Section of the diff report: which records are identical across the two rulesets.
struct DiffReport {
  std::vector<std::string> shared_identical;
  std::vector<std::string> only_prerenewal;
  std::vector<std::string> only_renewal;
  std::vector<std::string> differing;
};
DiffReport diff_rulesets(const Map& prerenewal, const Map& renewal);  // stub

// Embedded rAthena scripts (Script, EquipScript...) are routed to the script
// front end (legacy-npc), never executed or emitted raw into Luau source.
struct EmbeddedScriptRoute { std::string field; bool compiled_to_bonus; std::string fallback_interpreter_ref; };
EmbeddedScriptRoute route_embedded_script(const std::string& field, const std::string& script);  // stub

}  // namespace tooling::migrate
