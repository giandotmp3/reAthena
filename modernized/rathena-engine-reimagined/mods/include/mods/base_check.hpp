#pragma once
// CI check: rathena.base is mechanism only (architecture 6.3). The set of
// ruleset-specific names is supplied by the caller (derived from the ruleset
// packages at CI time), so this unit itself contains no game vocabulary.
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace mods {

struct BaseViolation {
  std::filesystem::path file;
  unsigned line;
  std::string name;
};

struct BaseCheckOptions {
  // Legacy constants are upper case (SM_BASH) while definition ids are lower
  // case; derived lists are matched case-insensitively.
  bool case_insensitive = false;
};

// Scans every regular file under `base_dir` for whole-token occurrences of
// any name in `forbidden`. Empty result == pass. A token is a maximal run of
// [A-Za-z0-9_]; a name containing other characters (e.g. "ruleset.renewal",
// "ruleset.renewal:skill/sm_bash") must match exactly and be bounded by
// non-token characters.
std::vector<BaseViolation> check_base_purity(const std::filesystem::path& base_dir,
                                             const std::set<std::string>& forbidden);
std::vector<BaseViolation> check_base_purity(const std::filesystem::path& base_dir,
                                             const std::set<std::string>& forbidden,
                                             const BaseCheckOptions& opt);

// ---- deriving the forbidden list --------------------------------------------
struct DeriveInput {
  // Ruleset package roots (contain mod.toml). Their mod ids, the definition
  // ids they declare (data/ and scripts/ *.luau and *.json, both literal
  // "mod:kind/name" strings and `local P = "mod:kind/"` ... `P .. "name"`
  // prefix concatenation) and the slugs of those ids are forbidden.
  std::vector<std::filesystem::path> ruleset_dirs;
  // rathena-migrate output directories (out/prerenewal, out/renewal): every
  // `["mod:kind/slug"] = {` key. Generated and git-ignored, so optional.
  std::vector<std::filesystem::path> migrate_out_dirs;
  // Slugs shorter than this are not derived on their own (their full ids still are).
  std::size_t min_slug_len = 4;
  // Names that are mechanism vocabulary in spite of matching a slug.
  std::set<std::string> allow;
};

struct DerivedNames {
  std::set<std::string> names;                // lower case
  std::map<std::string, std::string> origin;  // name -> first source
  std::size_t from_rulesets = 0, from_migrator = 0;  // distinct ids found
  std::set<std::string> suppressed_by_allow;  // slugs dropped because of `allow`
};
DerivedNames derive_forbidden_names(const DeriveInput& in);

// Loads forbidden names from a one-per-line file ('#' comments allowed).
std::set<std::string> load_forbidden_names(const std::filesystem::path& file);

}  // namespace mods
