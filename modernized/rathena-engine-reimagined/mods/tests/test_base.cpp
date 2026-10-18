// rathena.base purity CI check. Tags MOD-BASE-*.
#include <sys/wait.h>

#include <filesystem>
#include <fstream>
#include <iostream>

#include "harness.hpp"
#include "mods/base_check.hpp"
#include "mods/manifest.hpp"

namespace fs = std::filesystem;
using namespace mods;

static fs::path tmpdir(const char* name) {
  fs::path p = fs::temp_directory_path() / (std::string("mods_test_") + name);
  fs::remove_all(p);
  fs::create_directories(p);
  return p;
}
static void write(const fs::path& p, const std::string& s) { std::ofstream(p) << s; }

TEST("MOD-BASE-001", reference_to_ruleset_name_is_reported_with_location) {
  auto d = tmpdir("viol");
  write(d / "a.luau", "local x = 1\nlocal y = \"sm_bash\"\n");
  auto v = check_base_purity(d, {"sm_bash"});
  CHECK(v.size() == 1);
  CHECK(v[0].line == 2 && v[0].name == "sm_bash");
}

TEST("MOD-BASE-002", whole_token_matching_only) {
  auto d = tmpdir("token");
  write(d / "a.luau", "local sm_bash_helper = 1\n");
  CHECK(check_base_purity(d, {"sm_bash"}).empty());
}

TEST("MOD-BASE-003", scans_nested_files_of_any_extension) {
  auto d = tmpdir("nested");
  fs::create_directories(d / "x" / "y");
  write(d / "x" / "y" / "mod.toml", "provides = [\"ruleset.renewal\"]\n");
  CHECK(check_base_purity(d, {"ruleset.renewal"}).size() == 1);
}

TEST("MOD-BASE-004", forbidden_list_loader_skips_comments_and_blanks) {
  auto d = tmpdir("list");
  write(d / "l.txt", "# c\n\nfoo\n  bar  # trailing\n");
  auto s = load_forbidden_names(d / "l.txt");
  CHECK(s.size() == 2 && s.count("foo") && s.count("bar"));
}

TEST("MOD-BASE-005", shipped_base_package_has_no_ruleset_specific_names) {
  fs::path root = MODS_SOURCE_DIR;
  auto names = load_forbidden_names(root / "config" / "forbidden_base_names.example.txt");
  CHECK(!names.empty());
  CHECK(check_base_purity(root / "base" / "rathena.base", names).empty());
}

TEST("MOD-BASE-006", base_manifest_parses_and_requests_no_permissions) {
  fs::path root = MODS_SOURCE_DIR;
  std::ifstream in(root / "base" / "rathena.base" / "mod.toml");
  std::string text((std::istreambuf_iterator<char>(in)), {});
  Manifest m = [&] { return parse_manifest(text); }();
  CHECK(m.id == "rathena.base");
  CHECK(m.permissions.empty());
}

// ---- added with the implementation ---------------------------------------------
#include <cstdlib>

static fs::path engine_root_dir() { return fs::path(MODS_ENGINE_ROOT); }

static DeriveInput real_input(bool with_migrator) {
  DeriveInput in;
  in.ruleset_dirs = {engine_root_dir() / "ruleset-prerenewal", engine_root_dir() / "ruleset-renewal"};
  if (with_migrator) in.migrate_out_dirs = {engine_root_dir() / "tooling/migrate/out/prerenewal", engine_root_dir() / "tooling/migrate/out/renewal"};
  in.allow = load_forbidden_names(fs::path(MODS_SOURCE_DIR) / "config" / "base_allowed_words.txt");
  return in;
}

TEST("MOD-BASE-007", forbidden_names_are_derived_from_the_ruleset_packages) {
  auto d = derive_forbidden_names(real_input(false));
  std::cout << "      ruleset-package ids found: " << d.from_rulesets << ", forbidden names: " << d.names.size() << "\n";
  CHECK(d.from_rulesets >= 12);
  for (const char* n : {"ruleset.prerenewal", "ruleset.renewal", "sm_bash", "al_heal", "red_potion", "poring_card",
                        "ruleset.prerenewal:skill/sm_bash", "ruleset.prerenewal:item/red_potion", "quagmire"})
    if (!d.names.count(n)) throw CheckFailure(std::string("not derived: ") + n);
  CHECK(d.origin.at("sm_bash").find("ruleset-prerenewal") != std::string::npos);
}

TEST("MOD-BASE-008", shipped_base_passes_the_derived_check_including_migrator_names) {
  fs::path out = engine_root_dir() / "tooling/migrate/out/prerenewal";
  bool have_migrator = fs::is_directory(out);
  auto d = derive_forbidden_names(real_input(true));
  if (have_migrator) CHECK(d.from_migrator > 5000);  // 6,170 items + 1,004 mobs + 1,228 skills + ... in pre-renewal alone
  BaseCheckOptions opt;
  opt.case_insensitive = true;
  auto v = check_base_purity(fs::path(MODS_SOURCE_DIR) / "base" / "rathena.base", d.names, opt);
  std::string report;
  for (auto& x : v) report += x.file.string() + ":" + std::to_string(x.line) + " '" + x.name + "' (" + d.origin[x.name] + ")\n";
  if (!v.empty()) throw CheckFailure("rathena.base references ruleset-specific names:\n" + report);
  CHECK(d.names.size() > 100);
  // the allow-list may not rot: every entry must suppress a derived name
  if (have_migrator)
    for (const auto& a : real_input(true).allow)
      if (!d.suppressed_by_allow.count(a)) throw CheckFailure("stale entry in base_allowed_words.txt: " + a);
  if (have_migrator) std::cout << "      migrator ids: " << d.from_migrator << ", forbidden names: " << d.names.size() << ", allowed slugs used: " << d.suppressed_by_allow.size() << "\n";
}

TEST("MOD-BASE-009", planted_references_are_caught_in_every_spelling) {
  auto d = derive_forbidden_names(real_input(true));
  auto dir = tmpdir("planted");
  fs::copy(fs::path(MODS_SOURCE_DIR) / "base" / "rathena.base", dir, fs::copy_options::recursive);
  write(dir / "scripts" / "evil.luau",
        "local a = 'SM_BASH'\n"                                     // legacy constant spelling
        "local b = 'ruleset.prerenewal:skill/sm_bash'\n"            // full id
        "local c = require('ruleset.renewal')\n"                    // package name
        "local ok = sm_bash_helper\n"                               // not a whole token: allowed
        "-- red_potion in a comment still counts\n");
  BaseCheckOptions opt;
  opt.case_insensitive = true;
  auto v = check_base_purity(dir, d.names, opt);
  std::set<unsigned> lines;
  for (auto& x : v) if (x.file.filename() == "evil.luau") lines.insert(x.line);
  CHECK(lines == (std::set<unsigned>{1, 2, 3, 5}));
}

#ifdef MODS_TOOL_PATH
static int run_tool(const std::string& args) {
  int rc = std::system((std::string("\"") + MODS_TOOL_PATH + "\" " + args + " >/dev/null 2>&1").c_str());
  return WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;
}
#endif

TEST("MOD-BASE-010", ci_tool_exit_codes) {
#ifdef MODS_TOOL_PATH
  std::string common = " --ruleset \"" + (engine_root_dir() / "ruleset-prerenewal").string() + "\" --ruleset \"" +
                       (engine_root_dir() / "ruleset-renewal").string() + "\"";
  std::string base = (fs::path(MODS_SOURCE_DIR) / "base" / "rathena.base").string();
  CHECK(run_tool("check-base --base \"" + base + "\"" + common) == 0);
  auto dir = tmpdir("tool_bad");
  fs::copy(base, dir, fs::copy_options::recursive);
  write(dir / "scripts" / "x.luau", "local s = 'sm_provoke'\n");
  CHECK(run_tool("check-base --base \"" + dir.string() + "\"" + common) == 1);
  // an empty or wrongly-pointed derivation must not pass silently
  CHECK(run_tool("check-base --base \"" + base + "\" --ruleset /nonexistent") == 2);
  CHECK(run_tool("check-base --base /nonexistent" + common) == 2);
  CHECK(run_tool("check-base") == 2);
#else
  SKIP_TEST("tool path not configured");
#endif
}

TEST("MOD-BASE-011", base_manifest_and_files_hold_mechanism_only_shape) {
  // the manifest requests nothing, provides only the "base" capability and has no [ruleset] table
  fs::path root = MODS_SOURCE_DIR;
  Manifest m = load_manifest_file(root / "base" / "rathena.base" / "mod.toml");
  CHECK(m.provides == std::vector<std::string>{"base"} && !m.ruleset.has_value() && m.depends.empty());
  // and it passes the explicit-list check the original scaffold shipped
  auto names = load_forbidden_names(root / "config" / "forbidden_base_names.example.txt");
  CHECK(check_base_purity(root / "base" / "rathena.base", names).empty());
}
