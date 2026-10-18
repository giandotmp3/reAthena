#include "legacy_npc/db_script.hpp"

#include "mini_test.hpp"

using namespace legacy_npc;

ACCEPT("BR-RULESET-112", recompile_depth_limit_is_ten) { CHECK(kMaxRecompileDepth == 10); }

PENDING("BR-RULESET-098", bonus_arity_maps_to_delta_args, "compiler not implemented") {
  auto r = compile_db_script("bonus bStr,3; bonus2 bAddRace,RC_Brute,10;");
  CHECK(r.kind == CompiledDbScript::Kind::BonusTable);
  CHECK(r.deltas.size() == 2 && r.deltas[0].args.size() == 1 && r.deltas[1].args.size() == 2);
}

PENDING("BR-RULESET-112", compiled_deltas_are_cacheable_values_not_memory_writes, "compiler not implemented") {
  auto a = compile_db_script("bonus bAgi,2;");
  auto b = compile_db_script("bonus bAgi,2;");
  CHECK(a.deltas.size() == b.deltas.size() && a.deltas[0].bonus == b.deltas[0].bonus);
}

PENDING("BR-RULESET-112", control_flow_falls_back_to_interpreter, "compiler not implemented") {
  auto r = compile_db_script("if (BaseLevel > 50) bonus bStr,5;");
  CHECK(r.kind == CompiledDbScript::Kind::NeedsInterpreter);
}
