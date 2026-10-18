#include "legacy_npc/db_script.hpp"

#include "mini_test.hpp"

using namespace legacy_npc;

ACCEPT("BR-RULESET-112", recompile_depth_limit_is_ten) { CHECK(kMaxRecompileDepth == 10); }

ACCEPT("BR-RULESET-098", bonus_arity_maps_to_delta_args) {
  auto r = compile_db_script("bonus bStr,3; bonus2 bAddRace,RC_Brute,10;");
  CHECK(r.kind == CompiledDbScript::Kind::BonusTable);
  CHECK(r.deltas.size() == 2 && r.deltas[0].args.size() == 1 && r.deltas[1].args.size() == 2);
  CHECK(r.deltas[0].bonus == "bStr" && r.deltas[1].bonus == "bAddRace");
  CHECK(std::get<std::int64_t>(r.deltas[0].args[0]) == 3);
  CHECK(std::get<std::string>(r.deltas[1].args[0]) == "RC_Brute" && std::get<std::int64_t>(r.deltas[1].args[1]) == 10);
  auto r5 = compile_db_script("bonus5 bAutoSpellOnSkill,\"AL_HEAL\",\"AL_BLESSING\",1,100,0;");
  CHECK(r5.kind == CompiledDbScript::Kind::BonusTable && r5.deltas.at(0).args.size() == 5);
}

ACCEPT("BR-RULESET-112", compiled_deltas_are_cacheable_values_not_memory_writes) {
  auto a = compile_db_script("bonus bAgi,2;");
  auto b = compile_db_script("bonus bAgi,2;");
  CHECK(a.deltas.size() == b.deltas.size() && a.deltas[0].bonus == b.deltas[0].bonus);
  CHECK(a.deltas[0].args == b.deltas[0].args);  // plain values: equal compilations are interchangeable
}

ACCEPT("BR-RULESET-112", control_flow_falls_back_to_interpreter) {
  auto r = compile_db_script("if (BaseLevel > 50) bonus bStr,5;");
  CHECK(r.kind == CompiledDbScript::Kind::NeedsInterpreter);
  CHECK(compile_db_script("bonus bStr,BaseLevel/10;").kind == CompiledDbScript::Kind::NeedsInterpreter);  // expression argument
  CHECK(compile_db_script("set .@a,1; bonus bStr,.@a;").kind == CompiledDbScript::Kind::NeedsInterpreter);
  CHECK(compile_db_script("bonus bStr,1; itemheal 5,0;").kind == CompiledDbScript::Kind::NeedsInterpreter);
  CHECK(compile_db_script("bonus bStr,1;").kind == CompiledDbScript::Kind::BonusTable);
}
