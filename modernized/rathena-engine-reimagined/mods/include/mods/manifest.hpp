#pragma once
// mod.toml manifest (architecture 6.1) and ruleset package metadata
// (BR-RULESET-118).
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "mods/common.hpp"
#include "mods/toml.hpp"
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
  // "pipeline.protected_stages": replace, cancel or move a pipeline stage its defining ruleset marked protected, or
  // cancel a run that would skip one (script-host Permission::ProtectedStages). Inserting next to a protected stage
  // needs nothing.
  ProtectedStages,
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
  std::string db_set;                // "re" | "pre-re" (also accepted: "db/re", "db/pre-re")
  std::optional<bool> legacy_quirks;  // HITL #2 item 6
  std::optional<bool> strict_parity;
  bool renewal() const { return mode == "renewal"; }
};

// The sub-flag names of BR-RULESET-118 (six legacy flags plus EDP).
const std::vector<std::string>& ruleset_flag_names();

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
  // Script entry modules (relative paths under the package). Empty = the
  // package default (init.luau when present, otherwise every module).
  std::vector<std::string> entry;
  // `[entry] register = true`: the bundler calls register(engine) on every entry module that returns a table with a
  // `register` function, and the mod's sandbox gets the global `engine` table. Default false: a module's own
  // `register` (rathena.base's bonus.register(name, handler)) is then never called by the host.
  bool entry_register = false;
  std::string description;
  // Tables the mod owns (e.g. legacy-npc's [legacy]); preserved verbatim,
  // never interpreted by the loader.
  std::map<std::string, toml::Value> config;
  // Non-fatal findings, e.g. a top-level key written after a [table] header.
  std::vector<std::string> warnings;
};

// Parses and validates mod.toml text. Throws LoadError with a code
//   manifest.syntax | manifest.missing_key | manifest.unknown_key |
//   manifest.bad_type | manifest.bad_id | manifest.bad_version |
//   manifest.bad_range | manifest.bad_permission | manifest.bad_value |
//   manifest.duplicate | manifest.inconsistent
// and a message of the form
//   mod 'example.mod' (path/mod.toml:12:5): key 'permissions[1]': <reason>
// `source` (optional) is the file name shown in messages.
Manifest parse_manifest(const std::string& toml_text, const std::string& source = {});

// Reads and parses a mod.toml file. Throws LoadError{"manifest.io"} if unreadable.
Manifest load_manifest_file(const std::filesystem::path& file);

// Keys a manifest may contain at top level (for documentation and tests).
const std::vector<std::string>& manifest_known_keys();

}  // namespace mods
