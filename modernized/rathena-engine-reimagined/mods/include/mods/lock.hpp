#pragma once
// mods.lock: operator-granted permissions and optional signatures
// (architecture 5 threat model, 6.1). Nothing is granted by default.
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>

#include "mods/manifest.hpp"

namespace mods {

struct LockEntry {
  std::set<Permission> granted;
  std::optional<std::string> signature;  // opaque, operator-supplied
};

struct ModsLock {
  bool require_signatures = false;  // operator policy
  std::map<std::string, LockEntry> entries;  // by mod id
};

ModsLock parse_mods_lock(const std::string& toml_text);

// Verifier is injected (keys come from the environment, never from config
// defaults). Arguments: mod id, manifest digest, signature.
using SignatureVerifier = std::function<bool(const std::string&, const std::string&, const std::string&)>;

// Throws LoadError:
//   permission.ungranted  (names mod and permission) when the manifest
//                         requests anything the lock does not grant
//   signature.missing | signature.invalid when policy requires signatures
void authorize(const Manifest& m, const ModsLock& lock,
               const SignatureVerifier& verify,
               const std::string& manifest_digest);

// Grants for permissions a mod did not request are ignored (least privilege).
std::set<Permission> effective_grants(const Manifest& m, const ModsLock& lock);

}  // namespace mods
