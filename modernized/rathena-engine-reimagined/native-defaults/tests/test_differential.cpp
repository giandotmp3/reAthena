#include "fixtures.hpp"
#include "mini_test.hpp"
#include "native_defaults/not_implemented.hpp"
#include "native_defaults/registry.hpp"

using namespace nd;

// Differential harness (BR-RULESET-119, architecture section 8 conformance (b)): each native stage must
// match its pure-Luau reference bit for bit on every golden vector, including RNG draw counts.
// The Luau references live in luau/reference/. A Luau VM is not linked into this scaffold, so the
// reference side is injected as a ReferenceFn; these stay pending until the script-host adapter exists.

static void differential(const char* id_text, std::unique_ptr<IStage> native) {
  auto id = *StageId::parse(id_text);
  Registry reg;
  reg.register_native(std::move(native));
  throw NotImplementedError(std::string("Luau reference adapter for ") + id.str());
}

PENDING("BR-RULESET-119", "differential: weapon_basic native == Luau reference over golden vectors") {
  differential("native.damage.weapon_basic@1", make_weapon_basic_v1());
}
PENDING("BR-RULESET-119", "differential: element_table native == Luau reference over golden vectors") {
  differential("native.damage.element_table@1", make_element_table_v1());
}
PENDING("BR-RULESET-119", "differential: defense_soft native == Luau reference over golden vectors") {
  differential("native.damage.defense_soft@1", make_defense_soft_v1());
}
PENDING("BR-RULESET-119", "golden-trace (RATHENA_GOLDEN_TRACE) replay keyed by BR-RULESET id, both rulesets") {
  throw NotImplementedError("golden trace loader");
}
PENDING("BR-RULESET-115", "strict_parity: stage RNG draw counts equal legacy for hit/crit/defense") {
  throw NotImplementedError("draw-count vectors");
}
