#include <map>

#include "legacy_npc/parser.hpp"

#include "mini_test.hpp"

using namespace legacy_npc;

ACCEPT("LNPC-PARSE-003", source_hash_changes_with_content) {
  CHECK(source_hash("mes \"a\";") == source_hash("mes \"a\";"));
  CHECK(source_hash("mes \"a\";") != source_hash("mes \"b\";"));
}

ACCEPT_LUAU("LNPC-PARSE-001", declaration_forms_parse) {
  auto f = parse_file(
      "prontera,150,150,4\tscript\tGuide\t111,{\n mes \"hi\"; close;\n}\n"
      "prontera,1,1,0\twarp\tw1\t2,2,geffen,10,10\n", "t.txt");
  CHECK(f.npcs.size() == 2);
  CHECK(f.npcs[0].kind == DeclKind::Script && f.npcs[1].kind == DeclKind::Warp);
  CHECK(f.npcs[0].name == "Guide" && f.npcs[0].map == "prontera" && f.npcs[0].x == 150 && f.npcs[0].y == 150 && f.npcs[0].dir == 4);
  CHECK(f.npcs[1].name == "w1" && f.npcs[1].map == "prontera" && f.npcs[1].x == 1);
  CHECK(f.diagnostics.empty());
}

// every declaration form of the legacy dialect (npc_parsesrcfile)
ACCEPT_LUAU("LNPC-PARSE-001", all_declaration_forms_are_recognised) {
  auto f = parse_file(
      "-\tscript\tFloating\t-1,{\n end;\n}\n"
      "prontera,150,150,4\tscript\tGuide#a\t111,{\n mes \"hi\"; close;\n}\n"
      "prontera,152,150,4\tduplicate(Guide#a)\tGuide2\t111\n"
      "function\tscript\tF_Twice\t{\n return getarg(0) * 2;\n}\n"
      "prontera,10,10,0\twarp\twarp1\t2,2,geffen,10,10\n"
      "prontera,20,20,4\tshop\tShop1\t111,501:50,502:100\n"
      "prontera,30,30\tmonster\tPoring\t1002,5,5000,0,Mob::On\n"
      "prontera\tmapflag\tnoteleport\n", "t.txt");
  std::map<DeclKind, int> n;
  for (auto& x : f.npcs) ++n[x.kind];
  CHECK(n[DeclKind::Script] == 2);
  CHECK(n[DeclKind::Duplicate] == 1);
  CHECK(n[DeclKind::Function] == 1);
  CHECK(n[DeclKind::Warp] == 1);
  CHECK(n[DeclKind::Shop] == 1);
  CHECK(n[DeclKind::Monster] == 1);
  CHECK(n[DeclKind::Mapflag] == 1);
  for (auto& x : f.npcs)
    if (x.kind == DeclKind::Duplicate) CHECK(x.duplicate_of == "Guide#a" && x.name == "Guide2");
  CHECK(f.diagnostics.empty());
}

ACCEPT_LUAU("LNPC-PARSE-002", script_grammar_lowered_to_flat_list) {
  auto f = parse_file("-\tscript\tn\t-1,{\n if (a) { mes \"x\"; } else { mes \"y\"; }\n}\n", "t.txt");
  CHECK(!f.npcs.empty() && !f.npcs[0].program.code.empty());
  // control flow is pc assignment: a conditional jump, an unconditional jump over the else branch, two commands, the final end
  int jz = 0, jmp = 0, mes = 0, end = 0;
  for (auto& i : f.npcs[0].program.code) {
    jz += i.op == Op::JumpIfFalse;
    jmp += i.op == Op::Jump;
    mes += i.op == Op::Command && i.name == "mes";
    end += i.op == Op::End;
    if (i.op == Op::JumpIfFalse || i.op == Op::Jump) CHECK(i.target >= 0 && i.target <= static_cast<int>(f.npcs[0].program.code.size()));
  }
  CHECK(jz == 1 && jmp == 1 && mes == 2 && end == 1);
  CHECK(f.npcs[0].program.used_commands.size() == 1 && f.npcs[0].program.used_commands[0] == "mes");
  // labels become pcs
  auto g = parse_file("-\tscript\tn\t-1,{\n goto L_B;\nL_A: mes \"a\";\nL_B: mes \"b\"; end;\n}\n", "t.txt");
  CHECK(g.npcs.at(0).program.labels.count("l_a") == 1 && g.npcs.at(0).program.labels.count("l_b") == 1);
  CHECK(g.npcs[0].program.labels["l_b"] > g.npcs[0].program.labels["l_a"]);
}

ACCEPT_LUAU("LNPC-PARSE-006", syntax_errors_become_diagnostics_not_exceptions) {
  auto f = parse_file("-\tscript\tBad\t-1,{\n mes \"x\"\n}\n-\tscript\tGood\t-1,{\n end;\n}\n", "t.txt");
  CHECK(f.npcs.size() == 2);
  CHECK(!f.diagnostics.empty());
  CHECK(f.npcs[1].program.code.size() == 2);  // the second NPC still lowered (its `end;` and the closing end)
  auto g = parse_file("garbage without tabs\n", "t.txt");
  CHECK(!g.diagnostics.empty());
}

ACCEPT("LNPC-PARSE-004", ast_cache_roundtrip_and_hash_invalidation) {
  ParsedFile f; f.source_hash = 7;
  auto blob = serialize_ast(f);
  CHECK(deserialize_ast(blob, 7).source_hash == 7);
  CHECK(deserialize_ast(blob, 8).npcs.empty());
}

ACCEPT_LUAU("LNPC-PARSE-004", cache_roundtrip_keeps_a_parsed_file) {
  std::string src = "prontera,150,150,4\tscript\tGuide\t111,{\n mes \"hi\"; if (a) goto L_X; close;\nL_X: end;\n}\nprontera,1,1,0\twarp\tw1\t2,2,geffen,10,10\n";
  auto f = parse_file(src, "t.txt");
  auto blob = serialize_ast(f);
  auto g = deserialize_ast(blob, source_hash(src));
  CHECK(g.npcs.size() == f.npcs.size());
  CHECK(g.source_hash == f.source_hash && g.file_name == "t.txt");
  CHECK(g.npcs[0].program.code.size() == f.npcs[0].program.code.size());
  CHECK(g.npcs[0].program.body == f.npcs[0].program.body);
  CHECK(g.npcs[0].program.labels == f.npcs[0].program.labels);
  CHECK(g.npcs[0].program.used_commands == f.npcs[0].program.used_commands);
  CHECK(deserialize_ast(blob, source_hash(src) + 1).npcs.empty());
  CHECK(deserialize_ast(blob.substr(0, blob.size() / 2), source_hash(src)).npcs.empty());  // truncated cache = miss, not a crash
  CHECK(deserialize_ast("garbage", 1).npcs.empty());
}
