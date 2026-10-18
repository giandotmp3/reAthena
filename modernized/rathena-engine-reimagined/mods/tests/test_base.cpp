// rathena.base purity CI check. Tags MOD-BASE-*.
#include <filesystem>
#include <fstream>

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

PENDING_TEST("MOD-BASE-006", base_manifest_parses_and_requests_no_permissions) {
  fs::path root = MODS_SOURCE_DIR;
  std::ifstream in(root / "base" / "rathena.base" / "mod.toml");
  std::string text((std::istreambuf_iterator<char>(in)), {});
  Manifest m = [&] { return parse_manifest(text); }();
  CHECK(m.id == "rathena.base");
  CHECK(m.permissions.empty());
}
