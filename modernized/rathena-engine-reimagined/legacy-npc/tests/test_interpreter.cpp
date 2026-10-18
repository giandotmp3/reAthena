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
RunOutcome run_src(const std::string& body, Host& h, RunLimits limits = {}) {
  auto f = parse_file("-\tscript\tt\t-1,{\n" + body + "\n}\n", "t.txt");
  CHECK(!f.npcs.empty());
  auto reg = CommandRegistry::with_defaults();
  Interpreter in(f.npcs.at(0).program, h, reg, {}, limits);
  return in.run();
}
}  // namespace

ACCEPT_LUAU("LNPC-FLOW-001", goto_jumps_to_label) {
  Host h;
  // `close` ends the script after delivering the text: the dialogue is still waiting for the client's acknowledgement (Yielded)
  CHECK(run_src("goto L_B; mes \"skipped\"; L_B: mes \"ok\"; close;", h) != RunOutcome::Error);
  CHECK(h.log.size() == 2);  // mes + close
  CHECK(h.log[0] == "mes" && h.log[1] == "close");
}

ACCEPT_LUAU("LNPC-FLOW-002", callsub_uses_heap_frames_no_recursion_limit) {
  Host h;
  // 5000 nested callsubs. Every callsub is also a "goto" for the legacy runaway guard (check_gotocount, default 2048, which
  // LNPC-SAFE-002 pins), so the guard is raised for this test: what it shows is that depth is not limited by a host stack.
  RunLimits l;
  l.max_gotos = 20000;
  CHECK(run_src("callsub L_R,5000; end; L_R: if (getarg(0)>0) callsub L_R,getarg(0)-1; return;", h, l) == RunOutcome::Finished);
  // with the legacy default budget the same script is stopped by the guard (typed), not by a crash
  Host h2;
  CHECK(run_src("callsub L_R,5000; end; L_R: if (getarg(0)>0) callsub L_R,getarg(0)-1; return;", h2) == RunOutcome::BudgetExceeded);
}

ACCEPT_LUAU("LNPC-FLOW-003", switch_fall_through) {
  Host h;
  run_src("switch(1){ case 1: mes \"a\"; case 2: mes \"b\"; break; case 3: mes \"c\"; }", h);
  CHECK(h.log.size() == 2);
}
