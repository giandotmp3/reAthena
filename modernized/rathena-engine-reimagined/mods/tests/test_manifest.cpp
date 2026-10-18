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

PENDING_TEST("MOD-MANIFEST-003", valid_manifest_parses_all_fields) {
  Manifest m = parse_manifest(kValid);
  CHECK(m.id == "example.mod");
  CHECK((m.version == SemVer{1, 2, 3}));
  CHECK(m.depends.at("rathena.base") == "^1");
  CHECK(m.permissions.size() == 2);
  CHECK(m.provides.size() == 1);
}

PENDING_TEST("MOD-MANIFEST-004", missing_id_or_version_is_load_error) {
  try { parse_manifest("version = \"1.0.0\"\nengine_api = \"^1\"\n"); }
  catch (const NotImplemented&) { CHECK(false); }
  catch (const LoadError& e) { CHECK(std::string(e.what()).find("id") != std::string::npos); return; }
  CHECK(false);  // must throw LoadError
}

PENDING_TEST("MOD-MANIFEST-005", unknown_permission_in_manifest_refused) {
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

PENDING_TEST("MOD-MANIFEST-006", semver_range_matching) {
  CHECK(satisfies(SemVer{1, 4, 0}, "^1"));
  CHECK(!satisfies(SemVer{2, 0, 0}, "^1"));
  CHECK(satisfies(SemVer{1, 2, 3}, ">=1.0.0 <2.0.0"));
  CHECK(!satisfies(SemVer{0, 9, 0}, "^1.0"));
}

PENDING_TEST("BR-RULESET-118", ruleset_manifest_carries_subflags_not_boolean) {
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
