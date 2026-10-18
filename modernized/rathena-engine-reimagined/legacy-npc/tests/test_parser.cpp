#include "legacy_npc/parser.hpp"

#include "mini_test.hpp"

using namespace legacy_npc;

ACCEPT("LNPC-PARSE-003", source_hash_changes_with_content) {
  CHECK(source_hash("mes \"a\";") == source_hash("mes \"a\";"));
  CHECK(source_hash("mes \"a\";") != source_hash("mes \"b\";"));
}

PENDING("LNPC-PARSE-001", declaration_forms_parse, "parser not implemented") {
  auto f = parse_file(
      "prontera,150,150,4\tscript\tGuide\t111,{\n mes \"hi\"; close;\n}\n"
      "prontera,1,1,0\twarp\tw1\t2,2,geffen,10,10\n", "t.txt");
  CHECK(f.npcs.size() == 2);
  CHECK(f.npcs[0].kind == DeclKind::Script && f.npcs[1].kind == DeclKind::Warp);
}

PENDING("LNPC-PARSE-002", script_grammar_lowered_to_flat_list, "parser not implemented") {
  auto f = parse_file("-\tscript\tn\t-1,{\n if (a) { mes \"x\"; } else { mes \"y\"; }\n}\n", "t.txt");
  CHECK(!f.npcs.empty() && !f.npcs[0].program.code.empty());
}

PENDING("LNPC-PARSE-004", ast_cache_roundtrip_and_hash_invalidation, "serializer not implemented") {
  ParsedFile f; f.source_hash = 7;
  auto blob = serialize_ast(f);
  CHECK(deserialize_ast(blob, 7).source_hash == 7);
  CHECK(deserialize_ast(blob, 8).npcs.empty());
}
