#pragma once
// Dependency and ordering resolution (architecture 5 "Ordering", 6.1, 6.3).
#include <string>
#include <vector>

#include "mods/manifest.hpp"

namespace mods {

struct ResolveOptions {
  SemVer engine_api_version{1, 0, 0};
  // Capabilities with at most one provider (e.g. "ruleset").
  std::vector<std::string> exclusive_provides{"ruleset"};
  // Capabilities that exactly one enabled mod must provide. Empty by default
  // (a headless test set needs no ruleset); the server config sets {"ruleset"}.
  std::vector<std::string> required_provides{};
};

// True when the two mods can never be enabled together (one names the other,
// or a capability the other provides, in `conflicts`).
bool mutually_exclusive(const Manifest& a, const Manifest& b);

// Returns manifests in deterministic load order: dependencies first, then
// load_after/load_before, ties broken by id. Throws LoadError with code
//   resolve.duplicate_id | resolve.missing_dependency | resolve.version_mismatch |
//   resolve.conflict | resolve.cycle | resolve.exclusive_provider |
//   resolve.missing_provider | resolve.engine_api
// and a message naming every mod involved (a cycle lists the full path with
// the reason for each edge). load_after/load_before naming an absent mod are
// ignored (optional ordering). `depends` names mod ids only; `conflicts` may
// name a mod id or a capability from `provides`.
std::vector<Manifest> resolve_load_order(const std::vector<Manifest>& mods,
                                         const ResolveOptions& opts = {});

// Version-range solving over several available versions per mod id: starting
// from `roots`, picks for every needed mod the highest version that satisfies
// all collected `depends` ranges, `engine_api`, conflicts and exclusivity,
// backtracking when a choice dead-ends. Returns the chosen manifests (one per
// id, unordered; pass them to resolve_load_order). Throws the same codes as
// resolve_load_order, naming the requiring mod and the range that failed.
std::vector<Manifest> select_versions(const std::vector<Manifest>& available,
                                      const std::vector<std::string>& roots,
                                      const ResolveOptions& opts = {});

}  // namespace mods
