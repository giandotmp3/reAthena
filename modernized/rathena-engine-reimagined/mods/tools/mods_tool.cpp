// mods_tool: command line front end of the mods unit (CI and operators).
//
//   mods_tool check-base --base DIR [--ruleset DIR]... [--migrate-out DIR]...
//                        [--names FILE]... [--allow FILE] [--min-slug N]
//   mods_tool manifest FILE...
//   mods_tool resolve (--mod DIR | --mods-root DIR)... [--lock FILE]
//                     [--engine-api X.Y.Z] [--require CAP]
//   mods_tool digest DIR
//   mods_tool sign --key-env VAR --mod ID --digest sha256:HEX
//
// Exit codes: 0 ok, 1 check failed (violations / refused), 2 usage or I/O error.
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "mods/base_check.hpp"
#include "mods/lock.hpp"
#include "mods/manifest.hpp"
#include "mods/resolver.hpp"
#include "mods/sha256.hpp"

namespace fs = std::filesystem;
using namespace mods;

namespace {

int usage() {
  std::cerr << "usage: mods_tool check-base --base DIR [--ruleset DIR]... [--migrate-out DIR]... [--names FILE]... [--allow FILE] [--min-slug N]\n"
               "       mods_tool manifest FILE...\n"
               "       mods_tool resolve (--mod DIR | --mods-root DIR)... [--lock FILE] [--engine-api X.Y.Z] [--require CAP]\n"
               "       mods_tool digest DIR\n"
               "       mods_tool sign --key-env VAR --mod ID --digest sha256:HEX\n";
  return 2;
}

int check_base(const std::vector<std::string>& a) {
  fs::path base;
  DeriveInput in;
  std::set<std::string> explicit_names;
  for (std::size_t i = 0; i < a.size(); ++i) {
    auto next = [&]() -> const std::string& {
      if (i + 1 >= a.size()) throw std::runtime_error("missing value after " + a[i]);
      return a[++i];
    };
    if (a[i] == "--base") base = next();
    else if (a[i] == "--ruleset") in.ruleset_dirs.push_back(next());
    else if (a[i] == "--migrate-out") in.migrate_out_dirs.push_back(next());
    else if (a[i] == "--names") { auto s = load_forbidden_names(next()); explicit_names.insert(s.begin(), s.end()); }
    else if (a[i] == "--allow") { auto s = load_forbidden_names(next()); for (auto& n : s) { std::string l = n; for (auto& c : l) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); in.allow.insert(l); } }
    else if (a[i] == "--min-slug") in.min_slug_len = static_cast<std::size_t>(std::stoul(next()));
    else throw std::runtime_error("unknown option " + a[i]);
  }
  if (base.empty()) throw std::runtime_error("--base is required");
  if (!fs::is_directory(base)) throw std::runtime_error("base directory not found: " + base.string());
  DerivedNames d = derive_forbidden_names(in);
  std::set<std::string> names = d.names;
  for (const auto& n : explicit_names) names.insert(n);
  if (!in.ruleset_dirs.empty() && d.from_rulesets == 0) {
    std::cerr << "refusing to pass: no definition ids were found in the ruleset packages (is --ruleset right?)\n";
    return 2;
  }
  if (names.empty()) { std::cerr << "refusing to pass with an empty forbidden-name list\n"; return 2; }
  BaseCheckOptions opt;
  opt.case_insensitive = true;
  auto v = check_base_purity(base, names, opt);
  std::cout << "forbidden names: " << names.size() << " (" << d.from_rulesets << " ruleset-package ids, " << d.from_migrator
            << " migrator ids, " << explicit_names.size() << " explicit, " << d.suppressed_by_allow.size() << " slugs allowed)\n";
  for (const auto& x : v) {
    auto it = d.origin.find(x.name);
    std::cout << x.file.string() << ":" << x.line << ": references ruleset-specific name '" << x.name << "'"
              << (it != d.origin.end() ? " (defined in " + it->second + ")" : std::string()) << "\n";
  }
  std::cout << (v.empty() ? "rathena.base is mechanism-only: PASS\n" : "rathena.base FAILS: " + std::to_string(v.size()) + " reference(s)\n");
  return v.empty() ? 0 : 1;
}

int manifest(const std::vector<std::string>& a) {
  if (a.empty()) return usage();
  int rc = 0;
  for (const auto& f : a) {
    try {
      Manifest m = load_manifest_file(f);
      std::cout << f << ": ok  " << m.id << " " << to_string(m.version) << "  engine_api \"" << m.engine_api << "\"  depends " << m.depends.size()
                << "  permissions " << m.permissions.size() << (m.ruleset ? "  ruleset " + m.ruleset->mode : std::string()) << "\n";
    } catch (const LoadError& e) {
      std::cout << f << ": REFUSED [" << e.code() << "] " << e.what() << "\n";
      rc = 1;
    }
  }
  return rc;
}

int resolve(const std::vector<std::string>& a) {
  std::vector<Manifest> ms;
  ModsLock lock;
  ResolveOptions ro;
  for (std::size_t i = 0; i < a.size(); ++i) {
    auto next = [&]() -> const std::string& {
      if (i + 1 >= a.size()) throw std::runtime_error("missing value after " + a[i]);
      return a[++i];
    };
    if (a[i] == "--mod") ms.push_back(load_manifest_file(fs::path(next()) / "mod.toml"));
    else if (a[i] == "--mods-root") {
      std::vector<fs::path> dirs;
      for (const auto& e : fs::directory_iterator(next())) if (e.is_directory() && fs::exists(e.path() / "mod.toml")) dirs.push_back(e.path());
      std::sort(dirs.begin(), dirs.end());
      for (const auto& d : dirs) ms.push_back(load_manifest_file(d / "mod.toml"));
    } else if (a[i] == "--lock") lock = load_mods_lock_file(next());
    else if (a[i] == "--engine-api") {
      auto v = parse_semver(next());
      if (!v) throw std::runtime_error("--engine-api needs X.Y.Z");
      ro.engine_api_version = *v;
    } else if (a[i] == "--require") ro.required_provides.push_back(next());
    else throw std::runtime_error("unknown option " + a[i]);
  }
  try {
    auto order = resolve_load_order(ms, ro);
    for (const auto& m : order) {
      std::string ungranted;
      auto eff = effective_grants(m, lock);
      for (const auto& p : m.permissions) if (!eff.count(p)) ungranted += (ungranted.empty() ? "" : ",") + p.str();
      std::cout << m.id << " " << to_string(m.version) << (ungranted.empty() ? "" : "  UNGRANTED: " + ungranted) << "\n";
    }
  } catch (const LoadError& e) {
    std::cout << "REFUSED [" << e.code() << "] " << e.what() << "\n";
    return 1;
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) return usage();
  std::string cmd = argv[1];
  std::vector<std::string> args(argv + 2, argv + argc);
  try {
    if (cmd == "check-base") return check_base(args);
    if (cmd == "manifest") return manifest(args);
    if (cmd == "resolve") return resolve(args);
    if (cmd == "digest") {
      if (args.size() != 1) return usage();
      std::cout << package_digest(args[0]) << "\n";
      return 0;
    }
    if (cmd == "sign") {
      std::string env, mod, digest;
      for (std::size_t i = 0; i + 1 < args.size(); i += 2) {
        if (args[i] == "--key-env") env = args[i + 1];
        else if (args[i] == "--mod") mod = args[i + 1];
        else if (args[i] == "--digest") digest = args[i + 1];
        else return usage();
      }
      const char* k = env.empty() ? nullptr : std::getenv(env.c_str());
      if (!k || !*k || mod.empty() || digest.empty()) { std::cerr << "sign needs --key-env VAR (set in the environment), --mod and --digest\n"; return 2; }
      std::cout << keyed_hash_signature(k, mod, digest) << "\n";
      std::cout << "trusted_key_hash = \"" << key_hash(k) << "\"\n";
      return 0;
    }
  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << "\n";
    return 2;
  }
  return usage();
}
