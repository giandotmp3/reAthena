#pragma once
// mods.lock: operator-granted permissions, optional content pins and optional
// signatures (architecture 5 threat model, 6.1). Nothing is granted by default.
//
//   require_signatures = true                 # operator policy (default false)
//   trusted_key_hash = "sha256:<64 hex>"      # hash of the signing key (optional)
//   [mod."example.mod"]
//   grants = ["storage.account"]              # permissions the operator allows
//   digest = "sha256:<64 hex>"                # optional: pin the package content
//   signature = "..."                         # optional, checked by the verifier
//
// What is and is not cryptographic. SHA-256 and the package digest are real
// (mods/sha256.hpp). The built-in `make_keyed_hash_verifier` is a keyed
// SHA-256 MAC over (mod id, package digest) with a shared secret, plus a
// check that the secret hashes to `trusted_key_hash`. It detects tampering
// by anyone who lacks the key and pins which key is in force, but whoever
// holds the key can also sign: it is NOT a public-key signature. The
// `SignatureVerifier` seam exists so an Ed25519 verifier can replace it
// without touching the loader. Keys come from the environment, never from
// config files.
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
  std::optional<std::string> digest;     // pinned package digest "sha256:<hex>"
};

struct ModsLock {
  bool require_signatures = false;  // operator policy
  std::optional<std::string> trusted_key_hash;  // "sha256:<hex>" of the signing key
  std::map<std::string, LockEntry> entries;  // by mod id
};

// Throws LoadError{"lock.syntax"|"lock.unknown_key"|"lock.bad_type"|
// "lock.bad_value"|"lock.unknown_grant"} naming the key and position.
ModsLock parse_mods_lock(const std::string& toml_text, const std::string& source = {});
ModsLock load_mods_lock_file(const std::filesystem::path& file);
// Canonical text; parse_mods_lock(write_mods_lock(l)) reproduces `l`.
std::string write_mods_lock(const ModsLock& lock);

// Verifier is injected. Arguments: mod id, manifest/package digest, signature.
using SignatureVerifier = std::function<bool(const std::string&, const std::string&, const std::string&)>;

// "sha256:" + sha256_hex(key_bytes): the value for `trusted_key_hash`.
std::string key_hash(const std::string& key_bytes);
// The signature the keyed-hash verifier accepts for (mod, digest).
std::string keyed_hash_signature(const std::string& key_bytes, const std::string& mod_id,
                                 const std::string& digest);
// Accepts only when sha256(key_bytes) == trusted_key_hash and the signature
// equals keyed_hash_signature(...) (constant-time compare).
SignatureVerifier make_keyed_hash_verifier(std::string key_bytes, std::string trusted_key_hash);
// Reads the key from the environment variable (default RATHENA_MODS_SIGNING_KEY).
// Without the variable (or without lock.trusted_key_hash) the verifier rejects everything.
SignatureVerifier verifier_from_environment(const ModsLock& lock,
                                            const char* env_name = "RATHENA_MODS_SIGNING_KEY");

// Throws LoadError:
//   signature.missing | signature.invalid  when policy requires signatures
//                         (a present signature is always verified)
//   digest.mismatch       when the lock pins a digest that differs
//   permission.ungranted  (names mod and every missing permission) when the
//                         manifest requests anything the lock does not grant
void authorize(const Manifest& m, const ModsLock& lock,
               const SignatureVerifier& verify,
               const std::string& manifest_digest);

// Grants for permissions a mod did not request are ignored (least privilege).
std::set<Permission> effective_grants(const Manifest& m, const ModsLock& lock);

}  // namespace mods
