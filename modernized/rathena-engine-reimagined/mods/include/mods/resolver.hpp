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
};

// Returns manifests in deterministic load order: dependencies first, then
// load_after/load_before, ties broken by id. Throws LoadError with code
//   resolve.missing_dependency | resolve.version_mismatch |
//   resolve.conflict | resolve.cycle | resolve.exclusive_provider |
//   resolve.engine_api
// and a message naming every mod involved (a cycle lists the full path).
std::vector<Manifest> resolve_load_order(const std::vector<Manifest>& mods,
                                         const ResolveOptions& opts = {});

}  // namespace mods
