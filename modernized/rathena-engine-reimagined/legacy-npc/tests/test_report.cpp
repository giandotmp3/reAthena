#include "legacy_npc/parser.hpp"
#include "legacy_npc/report.hpp"

#include "mini_test.hpp"

using namespace legacy_npc;

// hand-built Programs: the report works on what a Program lists, whether it came from parse_file or from a test
ACCEPT("LNPC-DEGRADE-002", unsupported_command_disables_only_that_npc) {
  ParsedFile f; f.file_name = "a.txt";
  Npc ok; ok.name = "ok";
  Instr i; i.op = Op::Command; i.name = "mes"; ok.program.code.push_back(i);
  Npc bad; bad.name = "bad";
  Instr j; j.op = Op::Command; j.name = "auction_open_unsupported"; bad.program.code.push_back(j);
  f.npcs = {ok, bad};
  auto r = analyze_file(f, CommandRegistry::with_defaults());
  CHECK(r.disabled_npcs.size() == 1 && r.disabled_npcs[0] == "bad");
  CHECK(r.commands.size() == 1 && r.commands[0].command == "auction_open_unsupported");
}

// the same through the real parser: nested calls count, gated commands do not disable, other NPCs keep running
ACCEPT_LUAU("LNPC-DEGRADE-002", report_over_a_parsed_file) {
  auto f = parse_file(
      "-\tscript\tGood\t-1,{\n mes \"x\"; if (countitem(501)) close;\n}\n"
      "-\tscript\tBad\t-1,{\n if (getmapusers(\"prt\") > 0) bg_warp 1, \"x\", 1, 1;\n}\n"
      "-\tscript\tGated\t-1,{\n atcommand \"@heal\";\n}\n", "mix.txt");
  auto r = analyze_file(f, CommandRegistry::with_defaults());
  CHECK(r.file == "mix.txt");
  CHECK(r.disabled_npcs.size() == 1 && r.disabled_npcs[0] == "Bad");
  CHECK(r.commands.size() == 1 && r.commands[0].command == "bg_warp" && r.commands[0].count == 1);
}
