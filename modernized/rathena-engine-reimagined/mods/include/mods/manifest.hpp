#pragma once
// mod.toml manifest (architecture 6.1) and ruleset package metadata
// (BR-RULESET-118).
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "mods/common.hpp"
#include "mods/semver.hpp"

namespace mods {

// Closed vocabulary. Unknown permission strings are a manifest error
// (secure default), never silently accepted.
// NOTE: "sql.read" is intentionally absent: the architecture says no general
// SQL permission exists; any SQL access is scoped to the mod's own namespace.
enum class PermissionKind {
  StorageAccount,  // "storage.account"
  StorageRead,     // "storage.read:<mod>"  (cross-mod read, needs arg)
  HttpRoute,       // "http.route"
  UnboundedLoop,   // "unbounded_loop"      (freeloop equivalent)
  AtCommand,       // "atcommand"
  NetRaw,          // "net.raw"
};

struct Permission {
  PermissionKind kind{};
  std::string arg;  // only StorageRead
  std::string str() const;
  friend bool operator==(const Permission&, const Permission&) = default;
  friend auto operator<=>(const Permission&, const Permission&) = default;
};

// nullopt for unknown names or a missing/extra argument.
std::optional<Permission> parse_permission(const std::string& s);

// Ruleset packages carry sub-flags, not a boolean (BR-RULESET-118).
struct RulesetMeta {
  std::string mode;                  // "renewal" | "prerenewal"
  std::map<std::string, bool> flags; // cast, drop, exp, lvdmg, aspd, stat, edp
  std::string db_set;                // "re" | "pre-re"
};

struct Manifest {
  std::string id;
  SemVer version;
  std::string engine_api;                      // range
  std::map<std::string, std::string> depends;  // mod id -> range
  std::vector<std::string> conflicts;
  std::vector<std::string> provides;           // e.g. "ruleset"
  std::vector<std::string> load_after;
  std::vector<std::string> load_before;
  std::vector<Permission> permissions;         // requested, NOT granted
  std::optional<RulesetMeta> ruleset;
};

// Parses and validates mod.toml text. Throws LoadError{"manifest.*"} with a
// message naming the offending field.
Manifest parse_manifest(const std::string& toml_text);

}  // namespace mods
