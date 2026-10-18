#include "mods/base_check.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <regex>
#include <sstream>

#include "mods/ids.hpp"
#include "mods/manifest.hpp"

namespace mods {
namespace {
namespace fs = std::filesystem;

bool is_word(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}
std::string lower(std::string s) {
  for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}
std::string slurp(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  std::ostringstream o;
  o << in.rdbuf();
  return o.str();
}
}  // namespace

std::vector<BaseViolation> check_base_purity(const fs::path& base_dir, const std::set<std::string>& forbidden) {
  return check_base_purity(base_dir, forbidden, BaseCheckOptions{});
}

std::vector<BaseViolation> check_base_purity(const fs::path& base_dir, const std::set<std::string>& forbidden,
                                             const BaseCheckOptions& opt) {
  std::vector<BaseViolation> out;
  std::vector<fs::path> files;
  for (const auto& e : fs::recursive_directory_iterator(base_dir))
    if (e.is_regular_file()) files.push_back(e.path());
  std::sort(files.begin(), files.end());
  std::vector<std::string> names;
  for (const auto& n : forbidden) if (!n.empty()) names.push_back(opt.case_insensitive ? lower(n) : n);
  for (const auto& f : files) {
    std::ifstream in(f);
    std::string line;
    for (unsigned n = 1; std::getline(in, line); ++n) {
      const std::string hay = opt.case_insensitive ? lower(line) : line;
      for (const auto& name : names) {
        for (auto pos = hay.find(name); pos != std::string::npos; pos = hay.find(name, pos + 1)) {
          bool left = pos == 0 || !is_word(hay[pos - 1]) || !is_word(name.front());
          std::size_t end = pos + name.size();
          bool right = end >= hay.size() || !is_word(hay[end]) || !is_word(name.back());
          if (left && right) out.push_back({f, n, name});
        }
      }
    }
  }
  return out;
}

std::set<std::string> load_forbidden_names(const fs::path& file) {
  std::set<std::string> out;
  std::ifstream in(file);
  std::string line;
  while (std::getline(in, line)) {
    auto h = line.find('#');
    if (h != std::string::npos) line.erase(h);
    while (!line.empty() && (line.back() == ' ' || line.back() == '\r' || line.back() == '\t'))
      line.pop_back();
    std::size_t s = 0;
    while (s < line.size() && (line[s] == ' ' || line[s] == '\t')) ++s;
    if (s < line.size()) out.insert(line.substr(s));
  }
  return out;
}

// ---- derivation -----------------------------------------------------------------
namespace {

void add_name(DerivedNames& d, const DeriveInput& in, const std::string& name, const std::string& origin, bool is_slug) {
  std::string n = lower(name);
  if (n.empty()) return;
  if (is_slug) {
    if (n.size() < in.min_slug_len) return;
    if (in.allow.count(n)) { d.suppressed_by_allow.insert(n); return; }
  }
  if (d.names.insert(n).second) d.origin[n] = origin;
}

void add_id(DerivedNames& d, const DeriveInput& in, const std::string& id, const std::string& origin,
            std::set<std::string>& seen) {
  auto p = parse_namespaced_id(id);
  if (!p) return;
  if (!seen.insert(id).second) return;
  add_name(d, in, id, origin, false);
  add_name(d, in, p->mod, origin, false);
  // the last path segment of the name is the slug (names may nest with '/')
  add_name(d, in, p->name, origin, true);
}

// Ids in a ruleset package file: literal "mod:kind/name" strings, and
// `local P = "mod:kind/"` ... `P .. "name"`.
void scan_package_file(const fs::path& f, DerivedNames& d, const DeriveInput& in, std::set<std::string>& seen) {
  const std::string text = slurp(f);
  const std::string origin = f.string();
  static const std::regex lit("\"([a-z0-9][a-z0-9._-]*:[a-z][a-z0-9_]*/[a-z0-9][a-z0-9_./-]*)\"");
  for (std::sregex_iterator it(text.begin(), text.end(), lit), end; it != end; ++it)
    add_id(d, in, (*it)[1], origin, seen);
  static const std::regex pre("local\\s+([A-Za-z_][A-Za-z0-9_]*)\\s*=\\s*\"([a-z0-9][a-z0-9._-]*:[a-z][a-z0-9_]*/)\"");
  for (std::sregex_iterator it(text.begin(), text.end(), pre), end; it != end; ++it) {
    const std::string var = (*it)[1], prefix = (*it)[2];
    std::regex use("\\b" + var + "\\s*\\.\\.\\s*\"([a-z0-9][a-z0-9_./-]*)\"");
    for (std::sregex_iterator u(text.begin(), text.end(), use), uend; u != uend; ++u)
      add_id(d, in, prefix + (*u)[1].str(), origin, seen);
  }
}

}  // namespace

DerivedNames derive_forbidden_names(const DeriveInput& in) {
  DerivedNames d;
  std::set<std::string> seen_rs, seen_mig;
  for (const auto& dir : in.ruleset_dirs) {
    if (!fs::exists(dir / "mod.toml")) continue;
    Manifest m = load_manifest_file(dir / "mod.toml");
    add_name(d, in, m.id, (dir / "mod.toml").string(), false);
    for (const char* sub : {"data", "scripts"}) {
      if (!fs::is_directory(dir / sub)) continue;
      std::vector<fs::path> files;
      for (const auto& e : fs::recursive_directory_iterator(dir / sub))
        if (e.is_regular_file() && (e.path().extension() == ".luau" || e.path().extension() == ".json")) files.push_back(e.path());
      std::sort(files.begin(), files.end());
      for (const auto& f : files) scan_package_file(f, d, in, seen_rs);
    }
  }
  d.from_rulesets = seen_rs.size();
  for (const auto& dir : in.migrate_out_dirs) {
    if (!fs::is_directory(dir)) continue;
    std::vector<fs::path> files;
    for (const auto& e : fs::recursive_directory_iterator(dir))
      if (e.is_regular_file() && e.path().extension() == ".luau") files.push_back(e.path());
    std::sort(files.begin(), files.end());
    for (const auto& f : files) {
      std::ifstream is(f);
      std::string line;
      while (std::getline(is, line)) {
        // generated layout: <TAB>["mod:kind/slug"] = {
        if (line.size() < 6 || line[0] != '\t' || line[1] != '[' || line[2] != '"') continue;
        auto close = line.find("\"]", 3);
        if (close == std::string::npos) continue;
        add_id(d, in, line.substr(3, close - 3), f.string(), seen_mig);
      }
    }
  }
  d.from_migrator = seen_mig.size();
  return d;
}

}  // namespace mods
