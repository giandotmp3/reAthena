#include "mods/manifest.hpp"

#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>

#include "mods/common.hpp"
#include "mods/ids.hpp"

namespace mods {

std::string Permission::str() const {
  switch (kind) {
    case PermissionKind::StorageAccount: return "storage.account";
    case PermissionKind::StorageRead: return "storage.read:" + arg;
    case PermissionKind::HttpRoute: return "http.route";
    case PermissionKind::UnboundedLoop: return "unbounded_loop";
    case PermissionKind::AtCommand: return "atcommand";
    case PermissionKind::NetRaw: return "net.raw";
    case PermissionKind::ProtectedStages: return "pipeline.protected_stages";
  }
  return {};
}

std::optional<Permission> parse_permission(const std::string& s) {
  static const std::pair<const char*, PermissionKind> plain[] = {
      {"storage.account", PermissionKind::StorageAccount},
      {"http.route", PermissionKind::HttpRoute},
      {"unbounded_loop", PermissionKind::UnboundedLoop},
      {"atcommand", PermissionKind::AtCommand},
      {"net.raw", PermissionKind::NetRaw},
      {"pipeline.protected_stages", PermissionKind::ProtectedStages},
  };
  for (auto& [name, kind] : plain)
    if (s == name) return Permission{kind, {}};
  const std::string prefix = "storage.read:";
  if (s.rfind(prefix, 0) == 0) {
    std::string other = s.substr(prefix.size());
    if (is_valid_mod_id(other)) return Permission{PermissionKind::StorageRead, other};
  }
  return std::nullopt;
}

const std::vector<std::string>& ruleset_flag_names() {
  static const std::vector<std::string> n{"cast", "drop", "exp", "lvdmg", "aspd", "stat", "edp"};
  return n;
}

const std::vector<std::string>& manifest_known_keys() {
  static const std::vector<std::string> k{
      "id", "version", "engine_api", "depends", "conflicts", "provides", "load_after",
      "load_before", "permissions", "entry", "description", "name", "license", "authors", "ruleset"};
  return k;
}

namespace {

using toml::Value;

class Validator {
public:
  Validator(const std::string& source) : source_(source) {}

  Manifest run(const Value& root) {
    // The id first, so every later message can name the mod.
    if (const Value* v = root.find("id"); v && v->is_string()) mod_ = v->s;

    for (const auto& kv : root.t) {
      const auto& known = manifest_known_keys();
      if (std::find(known.begin(), known.end(), kv.first) == known.end()) {
        if (kv.second.is_table()) continue;  // mod-owned table, kept in `config`
        fail("manifest.unknown_key", kv.second, kv.first,
             "unknown key (a mod's own settings belong in a [table]; known keys: " + join(known) + ")");
      }
    }

    Manifest m;
    // TOML scopes every key after a [table] header to that table, so
    // `[depends]` followed by `permissions = []` puts permissions INSIDE
    // depends. The shipped ruleset-prerenewal/mod.toml does exactly that.
    // Array-valued keys that are manifest keys are lifted to the top level
    // (never a valid dependency spec, whose values are strings) with a warning.
    Value lifted_depends;
    const Value* depends_v = root.find("depends");
    std::vector<std::pair<std::string, const Value*>> lifted;
    if (depends_v && depends_v->is_table()) {
      static const std::set<std::string> liftable{"conflicts", "provides", "load_after", "load_before", "permissions", "entry", "authors"};
      for (const auto& kv : depends_v->t)
        if (kv.second.is_array() && liftable.count(kv.first)) {
          if (root.find(kv.first))
            fail("manifest.duplicate", kv.second, "depends." + kv.first,
                 "key is also set at the top level; move it above the [depends] table header");
          lifted.emplace_back(kv.first, &kv.second);
          m.warnings.push_back("mod '" + mod_ + "' (" + where(kv.second) + "): key '" + kv.first + "' was written after the [depends] header, so TOML scopes it to [depends]; "
                               "treated as the top-level key '" + kv.first + "' (move it above [depends] to silence this)");
        }
    }
    auto top = [&](const char* name) -> const Value* {
      if (const Value* v = root.find(name)) return v;
      for (const auto& l : lifted) if (l.first == name) return l.second;
      return nullptr;
    };
    // id
    const Value* id = need(root, "id", Value::Type::String);
    if (!is_valid_mod_id(id->s))
      fail("manifest.bad_id", *id, "id",
           "\"" + id->s + "\" is not a valid mod id (lowercase letters, digits, '.', '_' and '-'; "
           "starts with a letter or digit; at most 64 characters; no empty '.' segment)");
    m.id = id->s;
    // version
    const Value* ver = need(root, "version", Value::Type::String);
    auto sv = parse_semver(ver->s);
    if (!sv)
      fail("manifest.bad_version", *ver, "version",
           "\"" + ver->s + "\" is not a semantic version MAJOR.MINOR.PATCH (pre-release and build suffixes are not supported)");
    m.version = *sv;
    // engine_api
    const Value* api = need(root, "engine_api", Value::Type::String);
    check_range(*api, "engine_api", api->s);
    if (api->s.find_first_not_of(" \t,") == std::string::npos)
      fail("manifest.bad_range", *api, "engine_api", "must not be empty");
    m.engine_api = api->s;

    if (const Value* d = opt(root, "depends", Value::Type::Table)) {
      for (const auto& kv : d->t) {
        if (std::any_of(lifted.begin(), lifted.end(), [&](const auto& l) { return l.second == &kv.second; })) continue;
        std::string key = "depends.\"" + kv.first + "\"";
        if (!is_valid_mod_id(kv.first))
          fail("manifest.bad_id", kv.second, key, "\"" + kv.first + "\" is not a valid mod id");
        if (kv.first == m.id) fail("manifest.inconsistent", kv.second, key, "a mod cannot depend on itself");
        if (!kv.second.is_string()) type_fail(kv.second, key, "string (a version range)");
        check_range(kv.second, key, kv.second.s);
        m.depends[kv.first] = kv.second.s;
      }
    }
    m.conflicts = id_list(top("conflicts"), "conflicts", m.id, true);
    m.provides = id_list(top("provides"), "provides", "", false);
    m.load_after = id_list(top("load_after"), "load_after", m.id, true);
    m.load_before = id_list(top("load_before"), "load_before", m.id, true);

    for (const auto& c : m.conflicts)
      if (m.depends.count(c))
        fail("manifest.inconsistent", *top("conflicts"), "conflicts",
             "\"" + c + "\" is both a dependency and a conflict");
    for (const auto& a : m.load_after)
      if (std::find(m.load_before.begin(), m.load_before.end(), a) != m.load_before.end())
        fail("manifest.inconsistent", *top("load_after"), "load_after",
             "\"" + a + "\" is in both load_after and load_before");

    // permissions
    if (const Value* p = top("permissions")) {
      if (!p->is_array()) type_fail(*p, "permissions", "array");
      std::set<std::string> seen;
      for (std::size_t k = 0; k < p->a.size(); ++k) {
        const Value& e = p->a[k];
        std::string key = "permissions[" + std::to_string(k) + "]";
        if (!e.is_string()) type_fail(e, key, "string");
        auto perm = parse_permission(e.s);
        if (!perm) fail("manifest.bad_permission", e, key, permission_reason(e.s));
        if (!seen.insert(perm->str()).second)
          fail("manifest.duplicate", e, key, "permission \"" + e.s + "\" is listed twice");
        m.permissions.push_back(*perm);
      }
    }

    // entry
    // `entry = [..]` (module list) or the table form `[entry] modules = [..]; register = true|false`
    if (const Value* en0 = top("entry")) {
      const Value* en = en0;
      std::string base = "entry";
      if (!en0->is_table() && !en0->is_array()) type_fail(*en0, "entry", "array or table");
      if (en0->is_table()) {
        for (const auto& kv : en0->t)
          if (kv.first != "modules" && kv.first != "register")
            fail("manifest.unknown_key", kv.second, "entry." + kv.first, "unknown key (known: modules, register)");
        if (const Value* r = en0->find("register")) {
          if (!r->is_bool()) type_fail(*r, "entry.register", "boolean");
          m.entry_register = r->b;
        }
        en = en0->find("modules");
        base = "entry.modules";
      }
      if (en && !en->is_array()) type_fail(*en, base, "array");
      std::set<std::string> seen;
      for (std::size_t k = 0; en && k < en->a.size(); ++k) {
        const Value& e = en->a[k];
        std::string key = base + "[" + std::to_string(k) + "]";
        if (!e.is_string()) type_fail(e, key, "string");
        const std::string& s = e.s;
        bool bad = s.empty() || s[0] == '/' || s.find('\\') != std::string::npos ||
                   s.find("..") != std::string::npos || s.find(':') != std::string::npos ||
                   s.size() < 6 || s.compare(s.size() - 5, 5, ".luau") != 0;
        if (bad)
          fail("manifest.bad_value", e, key,
               "\"" + s + "\" must be a relative path ending in .luau, without '..', ':' or backslashes");
        if (!seen.insert(s).second) fail("manifest.duplicate", e, key, "entry \"" + s + "\" is listed twice");
        m.entry.push_back(s);
      }
    }
    if (const Value* d = opt(root, "description", Value::Type::String)) m.description = d->s;
    opt(root, "name", Value::Type::String);
    opt(root, "license", Value::Type::String);
    if (const Value* a = top("authors"))
      if (!a->is_array()) type_fail(*a, "authors", "array");
    if (const Value* a = top("authors"))
      for (std::size_t k = 0; k < a->a.size(); ++k)
        if (!a->a[k].is_string()) type_fail(a->a[k], "authors[" + std::to_string(k) + "]", "string");

    // ruleset
    bool provides_ruleset = std::find(m.provides.begin(), m.provides.end(), "ruleset") != m.provides.end();
    if (const Value* r = opt(root, "ruleset", Value::Type::Table)) {
      if (!provides_ruleset)
        fail("manifest.inconsistent", *r, "ruleset", "a [ruleset] table requires provides = [\"ruleset\"]");
      m.ruleset = ruleset(*r);
    }

    for (const auto& kv : root.t) {
      const auto& known = manifest_known_keys();
      if (kv.second.is_table() && std::find(known.begin(), known.end(), kv.first) == known.end())
        m.config[kv.first] = kv.second;
    }
    return m;
  }

private:
  const std::string& source_;
  std::string mod_;

  static std::string join(const std::vector<std::string>& v) {
    std::string r;
    for (std::size_t k = 0; k < v.size(); ++k) r += (k ? ", " : "") + v[k];
    return r;
  }
  std::string where(const Value& at) const {
    std::string w = source_.empty() ? std::string("mod.toml") : source_;
    if (at.line) w += ":" + std::to_string(at.line) + ":" + std::to_string(at.col);
    return w;
  }
  [[noreturn]] void fail(const char* code, const Value& at, const std::string& key, const std::string& why) const {
    throw LoadError(code, "mod '" + (mod_.empty() ? std::string("<unnamed>") : mod_) + "' (" + where(at) +
                              "): key '" + key + "': " + why);
  }
  [[noreturn]] void type_fail(const Value& v, const std::string& key, const char* want) const {
    fail("manifest.bad_type", v, key, std::string("expected ") + want + ", found " + v.type_name());
  }
  const Value* need(const Value& root, const char* key, Value::Type t) const {
    const Value* v = root.find(key);
    if (!v)
      throw LoadError("manifest.missing_key", "mod '" + (mod_.empty() ? std::string("<unnamed>") : mod_) + "' (" +
                                                  (source_.empty() ? std::string("mod.toml") : source_) +
                                                  "): required key '" + key + "' is missing");
    if (v->type != t) type_fail(*v, key, type_word(t));
    return v;
  }
  const Value* opt(const Value& root, const char* key, Value::Type t) const {
    const Value* v = root.find(key);
    if (!v) return nullptr;
    if (v->type != t) type_fail(*v, key, type_word(t));
    return v;
  }
  static const char* type_word(Value::Type t) {
    switch (t) {
      case Value::Type::String: return "string";
      case Value::Type::Array: return "array";
      case Value::Type::Table: return "table";
      case Value::Type::Boolean: return "boolean";
      case Value::Type::Integer: return "integer";
      case Value::Type::Float: return "float";
    }
    return "value";
  }
  void check_range(const Value& at, const std::string& key, const std::string& range) const {
    std::string why;
    if (!parse_range(range, &why))
      fail("manifest.bad_range", at, key, "\"" + range + "\" is not a valid version range: " + why);
  }
  std::vector<std::string> id_list(const Value* a, const char* key, const std::string& self, bool mod_ids) const {
    std::vector<std::string> out;
    if (!a) return out;
    if (!a->is_array()) type_fail(*a, key, "array");
    std::set<std::string> seen;
    for (std::size_t k = 0; k < a->a.size(); ++k) {
      const Value& e = a->a[k];
      std::string kk = std::string(key) + "[" + std::to_string(k) + "]";
      if (!e.is_string()) type_fail(e, kk, "string");
      if (!is_valid_mod_id(e.s))
        fail("manifest.bad_id", e, kk, "\"" + e.s + "\" is not a valid " + (mod_ids ? "mod id" : "capability name"));
      if (mod_ids && e.s == self) fail("manifest.inconsistent", e, kk, "a mod cannot name itself");
      if (!seen.insert(e.s).second) fail("manifest.duplicate", e, kk, "\"" + e.s + "\" is listed twice");
      out.push_back(e.s);
    }
    return out;
  }
  static std::string permission_reason(const std::string& s) {
    std::string r = "unknown permission \"" + s + "\"; the vocabulary is storage.account, storage.read:<mod>, "
                    "http.route, unbounded_loop, atcommand, net.raw, pipeline.protected_stages";
    if (s == "sql.read" || s.rfind("sql.", 0) == 0)
      r = "unknown permission \"" + s + "\": there is no general SQL permission (architecture section 5); "
          "SQL access is scoped to the mod's own namespace. The vocabulary is storage.account, "
          "storage.read:<mod>, http.route, unbounded_loop, atcommand, net.raw, pipeline.protected_stages";
    else if (s == "storage.read")
      r = "\"storage.read\" needs a target mod, written storage.read:<mod>";
    else if (s.rfind("storage.read:", 0) == 0)
      r = "\"" + s + "\": the part after 'storage.read:' is not a valid mod id";
    return r;
  }

  RulesetMeta ruleset(const Value& r) const {
    RulesetMeta rm;
    static const std::set<std::string> keys{"mode", "db_set", "flags", "subflags", "legacy_quirks", "strict_parity"};
    for (const auto& kv : r.t)
      if (!keys.count(kv.first))
        fail("manifest.unknown_key", kv.second, "ruleset." + kv.first,
             "unknown key (known: mode, db_set, flags, subflags, legacy_quirks, strict_parity)");
    const Value* mode = r.find("mode");
    if (!mode) fail("manifest.missing_key", r, "ruleset.mode", "required key is missing");
    if (!mode->is_string()) type_fail(*mode, "ruleset.mode", "string");
    if (mode->s == "renewal") rm.mode = "renewal";
    else if (mode->s == "prerenewal" || mode->s == "pre-renewal") rm.mode = mode->s;
    else
      fail("manifest.bad_value", *mode, "ruleset.mode",
           "\"" + mode->s + "\" is not one of renewal, prerenewal, pre-renewal");
    if (const Value* db = r.find("db_set")) {
      if (!db->is_string()) type_fail(*db, "ruleset.db_set", "string");
      static const std::set<std::string> ok{"re", "pre-re", "db/re", "db/pre-re"};
      if (!ok.count(db->s))
        fail("manifest.bad_value", *db, "ruleset.db_set", "\"" + db->s + "\" is not one of re, pre-re, db/re, db/pre-re");
      bool re_set = db->s == "re" || db->s == "db/re";
      if (re_set != rm.renewal())
        fail("manifest.inconsistent", *db, "ruleset.db_set",
             "\"" + db->s + "\" does not match mode \"" + rm.mode + "\"");
      rm.db_set = db->s;
    }
    const Value* f1 = r.find("flags");
    const Value* f2 = r.find("subflags");
    if (f1 && f2) fail("manifest.duplicate", *f2, "ruleset.subflags", "ruleset.flags and ruleset.subflags are the same table; use one");
    const Value* fl = f1 ? f1 : f2;
    const char* fkey = f1 ? "ruleset.flags" : "ruleset.subflags";
    if (fl) {
      if (!fl->is_table()) type_fail(*fl, fkey, "table");
      const auto& names = ruleset_flag_names();
      for (const auto& kv : fl->t) {
        if (std::find(names.begin(), names.end(), kv.first) == names.end())
          fail("manifest.unknown_key", kv.second, std::string(fkey) + "." + kv.first,
               "unknown ruleset sub-flag (known: " + join(names) + ")");
        if (!kv.second.is_bool()) type_fail(kv.second, std::string(fkey) + "." + kv.first, "boolean");
        rm.flags[kv.first] = kv.second.b;
      }
    }
    if (const Value* q = r.find("legacy_quirks")) {
      if (!q->is_bool()) type_fail(*q, "ruleset.legacy_quirks", "boolean");
      rm.legacy_quirks = q->b;
    }
    if (const Value* q = r.find("strict_parity")) {
      if (!q->is_bool()) type_fail(*q, "ruleset.strict_parity", "boolean");
      rm.strict_parity = q->b;
    }
    return rm;
  }
};

}  // namespace

Manifest parse_manifest(const std::string& toml_text, const std::string& source) {
  toml::Value root;
  try {
    root = toml::parse(toml_text, source.empty() ? std::string("mod.toml") : source);
  } catch (const toml::ParseError& e) {
    throw LoadError("manifest.syntax", std::string("manifest ") + e.what());
  }
  return Validator(source).run(root);
}

Manifest load_manifest_file(const std::filesystem::path& file) {
  std::ifstream in(file, std::ios::binary);
  if (!in) throw LoadError("manifest.io", "cannot read manifest " + file.string());
  std::ostringstream o;
  o << in.rdbuf();
  return parse_manifest(o.str(), file.string());
}

}  // namespace mods
