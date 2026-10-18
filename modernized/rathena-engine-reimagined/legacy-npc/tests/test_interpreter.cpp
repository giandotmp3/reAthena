#include "legacy_npc/interpreter.hpp"
#include "legacy_npc/parser.hpp"

#include "mini_test.hpp"

using namespace legacy_npc;

namespace {
struct Host : HostApi {
  InMemoryVariableStore s;
  std::vector<std::string> log;
  VariableStore& vars() override { return s; }
  void emit(std::string_view c, const std::vector<Value>&) override { log.emplace_back(c); }
};
RunOutcome run_src(const std::string& body, Host& h) {
  auto f = parse_file("-\tscript\tt\t-1,{\n" + body + "\n}\n", "t.txt");
  auto reg = CommandRegistry::with_defaults();
  Interpreter in(f.npcs.at(0).program, h, reg, {});
  return in.run();
}
}  // namespace

PENDING("LNPC-FLOW-001", goto_jumps_to_label, "interpreter not implemented") {
  Host h;
  CHECK(run_src("goto L_B; mes \"skipped\"; L_B: mes \"ok\"; close;", h) != RunOutcome::Error);
  CHECK(h.log.size() == 2);  // mes + close
}

PENDING("LNPC-FLOW-002", callsub_uses_heap_frames_no_recursion_limit, "interpreter not implemented") {
  Host h;
  CHECK(run_src("callsub L_R,5000; end; L_R: if (getarg(0)>0) callsub L_R,getarg(0)-1; return;", h) ==
        RunOutcome::Finished);
}

PENDING("LNPC-FLOW-003", switch_fall_through, "interpreter not implemented") {
  Host h;
  run_src("switch(1){ case 1: mes \"a\"; case 2: mes \"b\"; break; case 3: mes \"c\"; }", h);
  CHECK(h.log.size() == 2);
}
