#pragma once
// Shared types for the rathena-engine tooling suite.
#include <cstdint>
#include <stdexcept>
#include <string>

namespace tooling {

// Thrown by every stub. Pending acceptance tests expect it (expected-failure).
struct NotImplemented : std::logic_error {
  explicit NotImplemented(const std::string& what)
      : std::logic_error("not implemented: " + what) {}
};

// Which legacy ruleset database set a tool operates on (BR-RULESET-118).
enum class DbSet { PreRenewal, Renewal };

// Namespaced definition id, e.g. "ruleset.renewal:skill/sm_bash".
struct NamespacedId {
  std::string mod;   // "ruleset.renewal"
  std::string kind;  // "skill"
  std::string name;  // "sm_bash"
  std::string str() const { return mod + ":" + kind + "/" + name; }
};

}  // namespace tooling
