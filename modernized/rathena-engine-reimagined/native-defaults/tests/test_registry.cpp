#include "fixtures.hpp"
#include "mini_test.hpp"
#include "native_defaults/not_implemented.hpp"
#include "native_defaults/registry.hpp"

using namespace nd;

TEST("ARCH-1.1", "stage id parses name@version") {
  auto id = StageId::parse("native.damage.defense_soft@1");
  CHECK(id.has_value());
  CHECK_EQ(id->name, std::string("native.damage.defense_soft"));
  CHECK_EQ(id->version, 1);
  CHECK_EQ(id->str(), std::string("native.damage.defense_soft@1"));
}

TEST("ARCH-1.1", "malformed or non-native stage ids are rejected") {
  for (const char* bad : {"", "native.x", "native.x@", "native.x@0", "native.x@01", "native.x@-1", "mod.x@1",
                          "native..x@1", "native.X@1", "native.x@1a", "@1"})
    CHECK(!StageId::parse(bad).has_value());
}

static Registry with_stage(bool ref) {
  Registry r;
  r.register_native(make_defense_soft_v1());
  if (ref) r.register_reference(*StageId::parse("native.damage.defense_soft@1"), [](DamageCtx&, const RulesetTables&, IRng&) {});
  return r;
}

TEST("ARCH-1.1", "unhooked stage resolves to native") {
  auto r = with_stage(true);
  auto res = r.resolve(*StageId::parse("native.damage.defense_soft@1"), false);
  CHECK(res.path == ResolvedPath::Native);
  CHECK(res.native != nullptr);
}

TEST("ARCH-1.1", "hooked stage resolves to the Luau reference") {
  auto r = with_stage(true);
  auto res = r.resolve(*StageId::parse("native.damage.defense_soft@1"), true);
  CHECK(res.path == ResolvedPath::LuauReference);
}

TEST("ARCH-1.1", "native stage without a Luau reference is refused") {
  auto r = with_stage(false);
  CHECK_THROWS(r.resolve(*StageId::parse("native.damage.defense_soft@1"), false));
}

TEST("ARCH-1.1", "unknown stage or version is a registry error, never a silent fallback") {
  auto r = with_stage(true);
  CHECK_THROWS(r.resolve(*StageId::parse("native.damage.defense_soft@2"), false));
  CHECK_THROWS(r.resolve(*StageId::parse("native.damage.nope@1"), false));
}

TEST("ARCH-1.1", "duplicate native registration is refused") {
  auto r = with_stage(true);
  CHECK_THROWS(r.register_native(make_defense_soft_v1()));
}

TEST("BR-RULESET-118", "tables are indexed by ruleset-supplied data (element, size)") {
  auto pre = fx::tables(false);
  auto re = fx::tables(true);
  CHECK_EQ(pre.element.at(1, 2, 1), 150);      // water vs fire, both modes (BR-MAP-017)
  CHECK_EQ(re.element.at(0, 3, 1), 90);        // neutral vs ghost renewal
  CHECK_EQ(pre.element.at(0, 3, 1), 25);       // neutral vs ghost pre-renewal
  CHECK_EQ(re.size.at(0, 2), 75);              // BR-MAP-018
  CHECK_THROWS(re.element.at(9, 0, 1));
}

PENDING("BR-RULESET-118", "opt-in binds stages and params from a ruleset manifest; unknown params rejected") {
  // pipeline.use_native(...) manifest parsing is not part of the scaffold yet.
  throw NotImplementedError("manifest opt-in");
}
