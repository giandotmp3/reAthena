#pragma once
// Namespaced definition ids: "<mod>:<kind>/<name>", e.g.
// "ruleset.renewal:skill/sm_bash". Save data stores this string, never the
// client-visible number (architecture 6.2).
#include <optional>
#include <string>

namespace mods {

struct NamespacedId {
  std::string mod;   // [a-z0-9][a-z0-9._-]*
  std::string kind;  // [a-z][a-z0-9_]*
  std::string name;  // [a-z0-9][a-z0-9_./-]*

  std::string str() const { return mod + ":" + kind + "/" + name; }
  friend bool operator==(const NamespacedId&, const NamespacedId&) = default;
  friend auto operator<=>(const NamespacedId&, const NamespacedId&) = default;
};

bool is_valid_mod_id(const std::string& s);
// Returns nullopt on any malformed input (no exceptions for data errors).
std::optional<NamespacedId> parse_namespaced_id(const std::string& s);

}  // namespace mods
