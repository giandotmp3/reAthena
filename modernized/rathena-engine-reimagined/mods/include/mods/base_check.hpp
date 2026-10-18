#pragma once
// CI check: rathena.base is mechanism only (architecture 6.3). The set of
// ruleset-specific names is supplied by the caller (derived from the ruleset
// packages at CI time), so this unit itself contains no game vocabulary.
#include <filesystem>
#include <set>
#include <string>
#include <vector>

namespace mods {

struct BaseViolation {
  std::filesystem::path file;
  unsigned line;
  std::string name;
};

// Scans every regular file under `base_dir` for whole-token occurrences of
// any name in `forbidden`. Empty result == pass.
std::vector<BaseViolation> check_base_purity(const std::filesystem::path& base_dir,
                                             const std::set<std::string>& forbidden);

// Loads forbidden names from a one-per-line file ('#' comments allowed).
std::set<std::string> load_forbidden_names(const std::filesystem::path& file);

}  // namespace mods
