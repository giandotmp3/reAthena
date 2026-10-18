#include "legacy_npc/commands.hpp"
#include "legacy_npc/interpreter.hpp"
#include "legacy_npc/package.hpp"
#include "legacy_npc/parser.hpp"

#include "mini_test.hpp"

using namespace legacy_npc;

ACCEPT("LNPC-YIELD-001", yielding_command_set) {
  for (auto n : {"next", "close2", "menu", "select", "prompt", "input", "sleep", "sleep2",
                 "progressbar", "progressbar_npc"})
    CHECK(is_yielding_command(n));
  for (auto n : {"close", "mes", "callshop", "waitingroom"}) CHECK(!is_yielding_command(n));
  auto reg = CommandRegistry::with_defaults();
  CHECK(reg.find("menu")->cls == CommandClass::Yields);
  CHECK(reg.find("mes")->cls != CommandClass::Yields);
}

ACCEPT("LNPC-SAFE-001", gated_commands_denied_by_default) {
  auto reg = CommandRegistry::with_defaults();
  PermissionSet none;
  for (auto c : {"atcommand", "query_sql", "setunitdata", "freeloop"})
    CHECK(resolve(reg, c, none) == Resolution::PermissionDenied);
  PermissionSet g{Permission::AtCommand};
  CHECK(resolve(reg, "atcommand", g) == Resolution::Supported);
  CHECK(resolve(reg, "query_sql", g) == Resolution::PermissionDenied);
  CHECK(resolve(reg, "freeloop", {Permission::UnboundedLoop}) == Resolution::Supported);
}

ACCEPT("LNPC-DEGRADE-001", unknown_command_is_typed_unsupported_not_exception) {
  auto reg = CommandRegistry::with_defaults();
  CHECK(resolve(reg, "bg_create_team_not_supported", {}) == Resolution::Unsupported);
  CHECK(resolve(reg, "mes", {}) == Resolution::Supported);
}

ACCEPT("LNPC-SAFE-002", default_budget_matches_legacy) {
  RunLimits l;
  CHECK(l.max_commands == 65535 && l.max_gotos == 2048);
}

namespace {
struct NullHost : HostApi {
  InMemoryVariableStore s;
  VariableStore& vars() override { return s; }
  void emit(std::string_view, const std::vector<Value>&) override {}
};
}  // namespace

ACCEPT_LUAU("LNPC-SAFE-003", command_budget_aborts_script_not_tick) {
  auto reg = CommandRegistry::with_defaults();
  NullHost h;
  Program p;  // an infinite jump loop: L: goto L;
  p.labels["L"] = 0;
  Instr g; g.op = Op::Goto; g.name = "L";
  p.code.push_back(g);
  Interpreter in(p, h, reg, {}, RunLimits{100, 10});
  CHECK(in.run() == RunOutcome::BudgetExceeded);
  CHECK(in.last_error_type() == "BUDGET_EXCEEDED");
  // the tick (the host) is unharmed: the same sandbox runs the next script
  Program ok;
  Instr e; e.op = Op::Command; e.name = "clear";
  ok.code.push_back(e);
  Interpreter in2(ok, h, reg, {});
  CHECK(in2.run() == RunOutcome::Finished);
}

ACCEPT_LUAU("LNPC-SAFE-004", freeloop_without_permission_is_denied_at_runtime) {
  auto reg = CommandRegistry::with_defaults();
  NullHost h;
  Program p;
  Instr i; i.op = Op::Command; i.name = "freeloop";
  p.code.push_back(i);
  Interpreter in(p, h, reg, {});
  CHECK(in.run() == RunOutcome::PermissionDenied);
  CHECK(in.last_error_command() == "freeloop");
  Interpreter granted(p, h, reg, {Permission::UnboundedLoop});
  CHECK(granted.run() == RunOutcome::Finished);
}

ACCEPT_LUAU("LNPC-SAFE-005", atcommand_query_sql_setunitdata_denied_at_runtime) {
  auto reg = CommandRegistry::with_defaults();
  NullHost h;
  for (auto c : {"atcommand", "query_sql", "setunitdata"}) {
    Program p;
    Instr i; i.op = Op::Command; i.name = c;
    p.code.push_back(i);
    Interpreter in(p, h, reg, {});
    CHECK(in.run() == RunOutcome::PermissionDenied);
    CHECK(in.last_error_command() == c);
  }
}

ACCEPT_LUAU("LNPC-YIELD-002", blocking_commands_yield_and_disconnect_cancels) {
  auto reg = CommandRegistry::with_defaults();
  NullHost h;
  Program p;
  Instr i; i.op = Op::Command; i.name = "next";
  p.code.push_back(i);
  Interpreter in(p, h, reg, {});
  CHECK(in.run() == RunOutcome::Yielded);
  in.cancel();
  CHECK(in.resume({}) == RunOutcome::Cancelled);
}

// every blocking command leaves run() as Yielded and resume() carries the client's answer back into the script
ACCEPT_LUAU("LNPC-YIELD-003", every_blocking_command_yields_and_resumes) {
  auto reg = CommandRegistry::with_defaults();
  struct Rec : HostApi {
    InMemoryVariableStore s;
    std::vector<std::pair<std::string, std::vector<Value>>> calls;
    VariableStore& vars() override { return s; }
    void emit(std::string_view c, const std::vector<Value>& a) override { calls.emplace_back(std::string(c), a); }
  } h;
  auto f = parse_file("-\tscript\tt\t-1,{\n mes \"a\"; next; .@r = select(\"x:y:z\"); input .@n; sleep 100; progressbar \"0xff\", 2; mes \"r=\" + .@r + \" n=\" + .@n; close2; end;\n}\n", "t.txt");
  Interpreter in(f.npcs.at(0).program, h, reg, {});
  CHECK(in.run() == RunOutcome::Yielded);                  // next
  CHECK(in.resume({}) == RunOutcome::Yielded);             // select
  ResumeInput pick; pick.choice = 3;
  CHECK(in.resume(pick) == RunOutcome::Yielded);           // input
  ResumeInput typed; typed.input = std::int64_t{42};
  CHECK(in.resume(typed) == RunOutcome::Yielded);          // sleep
  CHECK(in.resume({}) == RunOutcome::Yielded);             // progressbar
  CHECK(in.resume({}) == RunOutcome::Yielded);             // close2
  CHECK(in.resume({}) == RunOutcome::Finished);
  bool saw = false;
  for (auto& [c, a] : h.calls)
    if (c == "mes" && !a.empty())
      if (auto* s = std::get_if<std::string>(&a[0])) saw = saw || *s == "r=3 n=42";
  CHECK(saw);
}

// the command tables of the Luau package and the C++ registry list the same commands
ACCEPT_LUAU("LNPC-DEGRADE-003", registry_matches_luau_command_tables) {
  auto reg = CommandRegistry::with_defaults();
  auto& host = shared_host();
  host.clear_log();
  host.run_ok("names", "for _, n in LEGACY.command_names() do if string.sub(n, -7) ~= '_unused' then print('@name\\t' .. n) end end");
  std::size_t n = 0;
  for (auto& r : host.records("@name")) {
    CHECK_MSG(reg.find(r[1]) != nullptr, "registry lacks " + r[1]);
    ++n;
  }
  CHECK(n > 200);
  CHECK(reg.size() >= n);
}

// an unimplemented command is a typed outcome naming the command; the NPC is refused before any of its commands run
ACCEPT_LUAU("LNPC-DEGRADE-006", interpreter_reports_unsupported_commands_as_a_typed_outcome) {
  auto reg = CommandRegistry::with_defaults();
  auto f = parse_file("-\tscript\tt\t-1,{\n mes \"before\"; bg_warp 1, \"x\", 1, 1; mes \"after\";\n}\n", "t.txt");
  struct Rec : HostApi {
    InMemoryVariableStore s;
    int calls = 0;
    VariableStore& vars() override { return s; }
    void emit(std::string_view, const std::vector<Value>&) override { ++calls; }
  } rec;
  Interpreter in(f.npcs.at(0).program, rec, reg, {});
  CHECK(in.run() == RunOutcome::Unsupported);
  CHECK(in.last_error_type() == "UNSUPPORTED" && in.last_error_command() == "bg_warp");
  CHECK(rec.calls == 0);  // disabled up front: not even `mes "before"` ran
}
