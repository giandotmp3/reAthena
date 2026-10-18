#include "legacy_npc/report.hpp"

#include "mini_test.hpp"

using namespace legacy_npc;

PENDING("LNPC-DEGRADE-002", unsupported_command_disables_only_that_npc, "analyzer not implemented") {
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
