#include "mods/lock.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>

#include "mods/common.hpp"
#include "mods/ids.hpp"
#include "mods/sha256.hpp"

namespace mods {
namespace {

using toml::Value;

[[noreturn]] void bad(const char* code, const std::string& source, const Value& at, const std::string& key,
                      const std::string& why) {
  std::string w = source.empty() ? std::string("mods.lock") : source;
  if (at.line) w += ":" + std::to_string(at.line) + ":" + std::to_string(at.col);
  throw LoadError(code, "mods.lock (" + w + "): key '" + key + "': " + why);
}

bool valid_hash(const std::string& s) {
  if (s.size() != 71 || s.compare(0, 7, "sha256:") != 0) return false;
  for (std::size_t i = 7; i < s.size(); ++i)
    if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f'))) return false;
  return true;
}

bool ct_equal(const std::string& a, const std::string& b) {
  unsigned char d = a.size() != b.size();
  for (std::size_t i = 0; i < a.size() && i < b.size(); ++i)
    d |= static_cast<unsigned char>(a[i] ^ b[i]);
  return d == 0;
}

}  // namespace

ModsLock parse_mods_lock(const std::string& text, const std::string& source) {
  Value root;
  try {
    root = toml::parse(text, source.empty() ? std::string("mods.lock") : source);
  } catch (const toml::ParseError& e) {
    throw LoadError("lock.syntax", std::string("mods.lock ") + e.what());
  }
  ModsLock lock;
  for (const auto& kv : root.t) {
    const Value& v = kv.second;
    if (kv.first == "require_signatures") {
      if (!v.is_bool()) bad("lock.bad_type", source, v, kv.first, std::string("expected boolean, found ") + v.type_name());
      lock.require_signatures = v.b;
    } else if (kv.first == "trusted_key_hash") {
      if (!v.is_string()) bad("lock.bad_type", source, v, kv.first, std::string("expected string, found ") + v.type_name());
      if (!valid_hash(v.s)) bad("lock.bad_value", source, v, kv.first, "must look like sha256:<64 lowercase hex digits>");
      lock.trusted_key_hash = v.s;
    } else if (kv.first == "lock_version") {
      if (!v.is_int() || v.i != 1) bad("lock.bad_value", source, v, kv.first, "only lock_version = 1 is supported");
    } else if (kv.first == "mod") {
      if (!v.is_table()) bad("lock.bad_type", source, v, "mod", std::string("expected table, found ") + v.type_name());
      for (const auto& mk : v.t) {
        const std::string key = "mod.\"" + mk.first + "\"";
        if (!is_valid_mod_id(mk.first)) bad("lock.bad_value", source, mk.second, key, "not a valid mod id");
        if (!mk.second.is_table()) bad("lock.bad_type", source, mk.second, key, std::string("expected table, found ") + mk.second.type_name());
        LockEntry e;
        for (const auto& ek : mk.second.t) {
          const Value& ev = ek.second;
          const std::string ekey = key + "." + ek.first;
          if (ek.first == "grants") {
            if (!ev.is_array()) bad("lock.bad_type", source, ev, ekey, std::string("expected array, found ") + ev.type_name());
            for (std::size_t g = 0; g < ev.a.size(); ++g) {
              const Value& gv = ev.a[g];
              const std::string gkey = ekey + "[" + std::to_string(g) + "]";
              if (!gv.is_string()) bad("lock.bad_type", source, gv, gkey, std::string("expected string, found ") + gv.type_name());
              auto p = parse_permission(gv.s);
              if (!p) bad("lock.unknown_grant", source, gv, gkey,
                          "unknown permission \"" + gv.s + "\" (grants must be in the manifest vocabulary; "
                          "there is no general sql.read)");
              if (!e.granted.insert(*p).second) bad("lock.bad_value", source, gv, gkey, "grant listed twice");
            }
          } else if (ek.first == "signature") {
            if (!ev.is_string()) bad("lock.bad_type", source, ev, ekey, std::string("expected string, found ") + ev.type_name());
            if (ev.s.empty() || ev.s.size() > 4096) bad("lock.bad_value", source, ev, ekey, "must be 1 to 4096 characters");
            e.signature = ev.s;
          } else if (ek.first == "digest") {
            if (!ev.is_string()) bad("lock.bad_type", source, ev, ekey, std::string("expected string, found ") + ev.type_name());
            if (!valid_hash(ev.s)) bad("lock.bad_value", source, ev, ekey, "must look like sha256:<64 lowercase hex digits>");
            e.digest = ev.s;
          } else {
            bad("lock.unknown_key", source, ev, ekey, "unknown key (known: grants, signature, digest)");
          }
        }
        lock.entries[mk.first] = std::move(e);
      }
    } else {
      bad("lock.unknown_key", source, v, kv.first,
          "unknown key (known: lock_version, require_signatures, trusted_key_hash, mod)");
    }
  }
  return lock;
}

ModsLock load_mods_lock_file(const std::filesystem::path& file) {
  std::ifstream in(file, std::ios::binary);
  if (!in) throw LoadError("lock.io", "cannot read " + file.string());
  std::ostringstream o;
  o << in.rdbuf();
  return parse_mods_lock(o.str(), file.string());
}

std::string write_mods_lock(const ModsLock& lock) {
  Value root = Value::make_table();
  root.set("lock_version", Value::make_int(1));
  root.set("require_signatures", Value::make_bool(lock.require_signatures));
  if (lock.trusted_key_hash) root.set("trusted_key_hash", Value::make_string(*lock.trusted_key_hash));
  Value mods = Value::make_table();
  for (const auto& [id, e] : lock.entries) {
    Value t = Value::make_table();
    Value g = Value::make_array();
    for (const auto& p : e.granted) g.a.push_back(Value::make_string(p.str()));
    t.set("grants", std::move(g));
    if (e.digest) t.set("digest", Value::make_string(*e.digest));
    if (e.signature) t.set("signature", Value::make_string(*e.signature));
    mods.set(id, std::move(t));
  }
  if (!mods.t.empty()) root.set("mod", std::move(mods));
  return toml::write(root);
}

std::string key_hash(const std::string& key_bytes) { return "sha256:" + sha256_hex(key_bytes); }

std::string keyed_hash_signature(const std::string& key_bytes, const std::string& mod_id,
                                 const std::string& digest) {
  std::string msg = std::string("rathena-mod-sig-v1") + '\0' + key_bytes + '\0' + mod_id + '\0' + digest;
  return "sha256:" + sha256_hex(msg);
}

SignatureVerifier make_keyed_hash_verifier(std::string key_bytes, std::string trusted_key_hash) {
  return [key = std::move(key_bytes), trusted = std::move(trusted_key_hash)](
             const std::string& mod, const std::string& digest, const std::string& sig) {
    if (key.empty() || !ct_equal(key_hash(key), trusted)) return false;  // wrong key in force
    return ct_equal(keyed_hash_signature(key, mod, digest), sig);
  };
}

SignatureVerifier verifier_from_environment(const ModsLock& lock, const char* env_name) {
  const char* k = std::getenv(env_name);
  if (!k || !*k || !lock.trusted_key_hash)
    return [](const std::string&, const std::string&, const std::string&) { return false; };
  return make_keyed_hash_verifier(k, *lock.trusted_key_hash);
}

std::set<Permission> effective_grants(const Manifest& m, const ModsLock& lock) {
  std::set<Permission> out;
  auto it = lock.entries.find(m.id);
  if (it == lock.entries.end()) return out;
  for (const auto& p : m.permissions)
    if (it->second.granted.count(p)) out.insert(p);
  return out;
}

void authorize(const Manifest& m, const ModsLock& lock, const SignatureVerifier& verify,
               const std::string& manifest_digest) {
  auto it = lock.entries.find(m.id);
  const LockEntry* e = it == lock.entries.end() ? nullptr : &it->second;

  if (e && e->signature) {
    if (!verify || !verify(m.id, manifest_digest, *e->signature))
      throw LoadError("signature.invalid", "mod '" + m.id + "': signature in mods.lock does not verify against digest " +
                                               manifest_digest + (lock.trusted_key_hash ? " and trusted key " + *lock.trusted_key_hash : std::string()));
  } else if (lock.require_signatures) {
    throw LoadError("signature.missing", "mod '" + m.id + "': mods.lock requires signatures (require_signatures = true) but has " +
                                             (e ? "no signature for this mod" : "no entry for this mod"));
  }
  if (e && e->digest && *e->digest != manifest_digest)
    throw LoadError("digest.mismatch", "mod '" + m.id + "': package digest " + manifest_digest +
                                           " differs from the digest pinned in mods.lock (" + *e->digest + ")");

  std::string missing;
  for (const auto& p : m.permissions)
    if (!e || !e->granted.count(p)) missing += (missing.empty() ? "" : ", ") + p.str();
  if (!missing.empty())
    throw LoadError("permission.ungranted", "mod '" + m.id + "' requests permission(s) not granted in mods.lock: " + missing +
                                                " (add them to grants under [mod.\"" + m.id + "\"] if the operator accepts)");
}

}  // namespace mods
