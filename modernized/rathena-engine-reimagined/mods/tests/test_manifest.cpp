// Manifest acceptance tests. BR-RULESET-118 covers the ruleset package
// manifest; MOD-MANIFEST-* are the mods-service rules from architecture 6.1.
#include "harness.hpp"
#include "mods/manifest.hpp"
#include "mods/semver.hpp"

using namespace mods;

static const char* kValid = R"(
id = "example.mod"
version = "1.2.3"
engine_api = "^1"
depends = { "rathena.base" = "^1" }
conflicts = ["other.mod"]
provides = ["feature"]
load_after = ["rathena.base"]
load_before = []
permissions = ["storage.account", "storage.read:other.mod"]
)";

TEST("MOD-MANIFEST-001", permission_vocabulary_is_closed) {
  CHECK(parse_permission("storage.account").has_value());
  auto r = parse_permission("storage.read:other.mod");
  CHECK(r && r->arg == "other.mod");
  CHECK(!parse_permission("storage.read").has_value());   // needs a target mod
  CHECK(!parse_permission("sql.read").has_value());       // no general SQL permission
  CHECK(!parse_permission("root").has_value());
  CHECK(!parse_permission("").has_value());
}

TEST("MOD-MANIFEST-002", semver_parse_rejects_malformed) {
  CHECK(parse_semver("1.2.3").has_value());
  CHECK(!parse_semver("1.2").has_value());
  CHECK(!parse_semver("01.2.3").has_value());
  CHECK(!parse_semver("1.2.3.4").has_value());
  CHECK(!parse_semver("a.b.c").has_value());
}

TEST("MOD-MANIFEST-003", valid_manifest_parses_all_fields) {
  Manifest m = parse_manifest(kValid);
  CHECK(m.id == "example.mod");
  CHECK((m.version == SemVer{1, 2, 3}));
  CHECK(m.depends.at("rathena.base") == "^1");
  CHECK(m.permissions.size() == 2);
  CHECK(m.provides.size() == 1);
}

TEST("MOD-MANIFEST-004", missing_id_or_version_is_load_error) {
  try { parse_manifest("version = \"1.0.0\"\nengine_api = \"^1\"\n"); }
  catch (const NotImplemented&) { CHECK(false); }
  catch (const LoadError& e) { CHECK(std::string(e.what()).find("id") != std::string::npos); return; }
  CHECK(false);  // must throw LoadError
}

TEST("MOD-MANIFEST-005", unknown_permission_in_manifest_refused) {
  try {
    parse_manifest("id = \"a.b\"\nversion = \"1.0.0\"\nengine_api = \"^1\"\n"
                   "permissions = [\"sql.read\"]\n");
  } catch (const NotImplemented&) { CHECK(false); }
  catch (const LoadError& e) {
    CHECK(std::string(e.what()).find("sql.read") != std::string::npos);
    return;
  }
  CHECK(false);
}

TEST("MOD-MANIFEST-006", semver_range_matching) {
  CHECK(satisfies(SemVer{1, 4, 0}, "^1"));
  CHECK(!satisfies(SemVer{2, 0, 0}, "^1"));
  CHECK(satisfies(SemVer{1, 2, 3}, ">=1.0.0 <2.0.0"));
  CHECK(!satisfies(SemVer{0, 9, 0}, "^1.0"));
}

TEST("BR-RULESET-118", ruleset_manifest_carries_subflags_not_boolean) {
  Manifest m = parse_manifest(R"(
id = "ruleset.example"
version = "1.0.0"
engine_api = "^1"
provides = ["ruleset"]
[ruleset]
mode = "prerenewal"
db_set = "pre-re"
[ruleset.flags]
cast = false
drop = false
exp = false
lvdmg = false
aspd = false
stat = false
edp = false
)");
  CHECK(m.ruleset.has_value());
  CHECK(m.ruleset->flags.size() == 7);
  CHECK(m.ruleset->db_set == "pre-re");
}

// ---- added with the implementation ---------------------------------------------
#include <filesystem>

static std::string engine_root() { return MODS_ENGINE_ROOT; }
static Manifest real(const char* rel) { return load_manifest_file(std::filesystem::path(engine_root()) / rel / "mod.toml"); }

static std::string refusal(const std::string& text, const std::string& code) {
  try { parse_manifest(text, "t/mod.toml"); }
  catch (const LoadError& e) {
    if (e.code() != code) throw CheckFailure("expected code " + code + " got " + e.code() + ": " + e.what());
    return e.what();
  }
  throw CheckFailure("expected LoadError " + code + " for:\n" + text);
}
static const char* kHead = "id = \"my.mod\"\nversion = \"1.0.0\"\nengine_api = \"^1\"\n";

TEST("MOD-MANIFEST-007", real_manifests_in_the_repo_are_accepted) {
  Manifest npc = real("legacy-npc");
  CHECK(npc.id == "legacy.npc" && npc.version == (SemVer{0, 1, 0}));
  CHECK(npc.provides == std::vector<std::string>{"legacy-npc"});
  CHECK(npc.permissions.empty());
  CHECK(npc.config.count("legacy") == 1);
  CHECK(npc.config.at("legacy").find("max_commands")->i == 65535);
  CHECK(npc.config.at("legacy").find("optional_permissions")->a.size() == 5);

  Manifest pre = real("ruleset-prerenewal");
  CHECK(pre.id == "ruleset.prerenewal" && pre.engine_api == "^1");
  CHECK(pre.permissions.empty() && pre.warnings.empty());  // permissions used to sit under [depends] and was lifted with a warning; fixed in the manifest
  CHECK(pre.depends.size() == 1 && pre.depends.at("rathena.base") == "^0.1");
  CHECK(pre.conflicts == std::vector<std::string>{"ruleset.renewal"});
  CHECK(pre.load_after == std::vector<std::string>{"rathena.base"});
  CHECK(pre.ruleset.has_value() && pre.ruleset->mode == "pre-renewal" && !pre.ruleset->renewal());
  CHECK(pre.ruleset->db_set == "db/pre-re" && pre.ruleset->flags.size() == 7);
  for (auto& [name, on] : pre.ruleset->flags) { (void)name; CHECK(!on); }
  CHECK(pre.ruleset->strict_parity == true && pre.ruleset->legacy_quirks == true);

  Manifest re = real("ruleset-renewal");
  CHECK(re.id == "ruleset.renewal" && re.provides == std::vector<std::string>{"ruleset"});
  CHECK(!re.ruleset.has_value());  // renewal keeps its sub-flags in package.toml, not mod.toml

  Manifest base = real("mods/base/rathena.base");
  CHECK(base.id == "rathena.base" && base.version == (SemVer{0, 1, 0}) && base.permissions.empty());
}

TEST("MOD-MANIFEST-008", errors_name_the_mod_the_key_and_the_position) {
  auto m = refusal(std::string(kHead) + "permissions = [\"storage.account\", \"sql.read\"]\n", "manifest.bad_permission");
  CHECK(m.find("mod 'my.mod'") != std::string::npos && m.find("key 'permissions[1]'") != std::string::npos);
  CHECK(m.find("t/mod.toml:4:") != std::string::npos);
  CHECK(m.find("no general SQL permission") != std::string::npos);
  m = refusal(std::string(kHead) + "[depends]\n\"x\" = \"^^1\"\n", "manifest.bad_range");
  CHECK(m.find("key 'depends.\"x\"'") != std::string::npos);
  m = refusal("id = \"Bad_ID\"\nversion = \"1.0.0\"\nengine_api = \"^1\"\n", "manifest.bad_id");
  CHECK(m.find("key 'id'") != std::string::npos && m.find("Bad_ID") != std::string::npos);
  m = refusal("id = \"a\"\nversion = \"1.0\"\nengine_api = \"^1\"\n", "manifest.bad_version");
  CHECK(m.find("mod 'a'") != std::string::npos && m.find("key 'version'") != std::string::npos);
  refusal("id = \"a\"\nversion = \"1.0.0-beta\"\nengine_api = \"^1\"\n", "manifest.bad_version");
  m = refusal("id = \"a\"\nversion = \"1.0.0\"\n", "manifest.missing_key");
  CHECK(m.find("'engine_api'") != std::string::npos);
  m = refusal(std::string(kHead) + "permisions = []\n", "manifest.unknown_key");
  CHECK(m.find("permisions") != std::string::npos && m.find("known keys") != std::string::npos);
  m = refusal("id = 5\nversion = \"1.0.0\"\nengine_api = \"^1\"\n", "manifest.bad_type");
  CHECK(m.find("expected string, found integer") != std::string::npos);
  refusal(std::string(kHead) + "permissions = \"storage.account\"\n", "manifest.bad_type");
  refusal(std::string(kHead) + "permissions = [\"storage.account\", \"storage.account\"]\n", "manifest.duplicate");
  m = refusal(std::string(kHead) + "permissions = [\"storage.read\"]\n", "manifest.bad_permission");
  CHECK(m.find("storage.read:<mod>") != std::string::npos);
  refusal(std::string(kHead) + "permissions = [\"storage.read:Bad Mod\"]\n", "manifest.bad_permission");
  refusal(std::string(kHead) + "permissions = [\"root\"]\n", "manifest.bad_permission");
  refusal(std::string(kHead) + "[depends]\n\"my.mod\" = \"^1\"\n", "manifest.inconsistent");
  refusal(std::string(kHead) + "depends = { other = \"^1\" }\nconflicts = [\"other\"]\n", "manifest.inconsistent");
  refusal(std::string(kHead) + "load_after = [\"x\"]\nload_before = [\"x\"]\n", "manifest.inconsistent");
  refusal(std::string(kHead) + "load_after = [\"my.mod\"]\n", "manifest.inconsistent");
  refusal(std::string(kHead) + "provides = [\"a\", \"a\"]\n", "manifest.duplicate");
  refusal(std::string(kHead) + "entry = [\"../x.luau\"]\n", "manifest.bad_value");
  refusal(std::string(kHead) + "entry = [\"scripts/x.txt\"]\n", "manifest.bad_value");
  m = refusal(std::string(kHead) + "version = \"2.0.0\"\n", "manifest.syntax");
  CHECK(m.find("duplicate key 'version'") != std::string::npos && m.find("t/mod.toml:4:1") != std::string::npos);
  m = refusal("id = \"my.mod\"\nversion = \"1.0.0\"\nengine_api = \"\"\n", "manifest.bad_range");
  CHECK(m.find("must not be empty") != std::string::npos);
}

TEST("MOD-MANIFEST-009", ruleset_table_is_validated) {
  const std::string base = std::string(kHead) + "provides = [\"ruleset\"]\n";
  Manifest ok = parse_manifest(base + "[ruleset]\nmode = \"renewal\"\ndb_set = \"re\"\nsubflags = { cast = true, edp = true }\nlegacy_quirks = false\n");
  CHECK(ok.ruleset->renewal() && ok.ruleset->flags.size() == 2 && ok.ruleset->legacy_quirks == false);
  auto m = refusal(base + "[ruleset]\nmode = \"hybrid\"\n", "manifest.bad_value");
  CHECK(m.find("key 'ruleset.mode'") != std::string::npos);
  m = refusal(base + "[ruleset]\nmode = \"renewal\"\ndb_set = \"pre-re\"\n", "manifest.inconsistent");
  CHECK(m.find("ruleset.db_set") != std::string::npos);
  m = refusal(base + "[ruleset]\nmode = \"renewal\"\nflags = { castt = true }\n", "manifest.unknown_key");
  CHECK(m.find("ruleset.flags.castt") != std::string::npos);
  refusal(base + "[ruleset]\nmode = \"renewal\"\nflags = { cast = 1 }\n", "manifest.bad_type");
  refusal(base + "[ruleset]\nmode = \"renewal\"\nflags = {}\nsubflags = {}\n", "manifest.duplicate");
  refusal(base + "[ruleset]\nflags = {}\n", "manifest.missing_key");
  refusal(base + "[ruleset]\nmode = \"renewal\"\nbogus = 1\n", "manifest.unknown_key");
  m = refusal(std::string(kHead) + "[ruleset]\nmode = \"renewal\"\n", "manifest.inconsistent");
  CHECK(m.find("provides") != std::string::npos);
}

TEST("MOD-MANIFEST-010", version_ranges_table) {
  struct C { const char* range; const char* v; bool ok; };
  const C cases[] = {
      {"^1.2.3", "1.2.3", true}, {"^1.2.3", "1.9.9", true}, {"^1.2.3", "2.0.0", false}, {"^1.2.3", "1.2.2", false},
      {"^0.1", "0.1.9", true}, {"^0.1", "0.2.0", false}, {"^0.0.3", "0.0.3", true}, {"^0.0.3", "0.0.4", false},
      {"^0", "0.9.9", true}, {"^0", "1.0.0", false}, {"^0.0", "0.0.9", true}, {"^0.0", "0.1.0", false},
      {"~1.2.3", "1.2.9", true}, {"~1.2.3", "1.3.0", false}, {"~1", "1.9.0", true}, {"~1.2", "1.2.5", true}, {"~1.2", "1.3.0", false},
      {">=0.1, <1", "0.5.0", true}, {">=0.1, <1", "1.0.0", false}, {">=0.1, <1", "0.0.9", false},
      {">=1.0 <2", "1.5.0", true}, {">= 1.0 < 2", "1.5.0", true}, {">=1.0.0,<2.0.0", "2.0.0", false},
      {">1.2", "1.2.9", false}, {">1.2", "1.3.0", true}, {">1.2.3", "1.2.4", true}, {">1.2.3", "1.2.3", false}, {">1", "1.9.9", false}, {">1", "2.0.0", true},
      {"<=1", "1.9.9", true}, {"<=1", "2.0.0", false}, {"<=1.2.3", "1.2.3", true}, {"<=1.2.3", "1.2.4", false}, {"<1.2", "1.1.9", true}, {"<1.2", "1.2.0", false},
      {"1.2.3", "1.2.3", true}, {"1.2.3", "1.2.4", false}, {"=1.2.3", "1.2.3", true}, {"1.2", "1.2.7", true}, {"1.2", "1.3.0", false}, {"1", "1.9.9", true}, {"1", "2.0.0", false},
      {"1.x", "1.4.0", true}, {"1.*", "2.0.0", false}, {"*", "9.9.9", true}, {"", "3.0.0", true}, {"x", "0.0.1", true},
  };
  for (const auto& c : cases)
    if (satisfies(*parse_semver(c.v), c.range) != c.ok)
      throw CheckFailure(std::string("range \"") + c.range + "\" vs " + c.v + " expected " + (c.ok ? "match" : "no match"));
  for (const char* bad : {"^", "^^1", "1.2.3.4", "a", ">=", "1..2", "01", ">=1.x.3", "^1.2.3-beta", "~", "1.2.", "99999999999", "^ ", "<>1"}) {
    std::string why;
    if (parse_range(bad, &why).has_value() || why.empty()) throw CheckFailure(std::string("range should be malformed: ") + bad);
    CHECK(!satisfies(SemVer{1, 0, 0}, bad));  // malformed never matches
  }
}

TEST("MOD-MANIFEST-011", io_error_and_unknown_tables_are_kept) {
  try { load_manifest_file("/nonexistent/dir/mod.toml"); CHECK(false); }
  catch (const LoadError& e) { CHECK(e.code() == "manifest.io"); }
  Manifest m = parse_manifest(std::string(kHead) + "description = \"d\"\nentry = [\"scripts/main.luau\"]\n[mine]\nx = 1\n[mine.sub]\ny = \"z\"\n");
  CHECK(m.description == "d" && m.entry.size() == 1 && m.config.count("mine") == 1);
  CHECK(m.config.at("mine").find("sub")->find("y")->s == "z");
}
