#include "legacy_npc/commands.hpp"
#include "legacy_npc/interpreter.hpp"

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

PENDING("LNPC-SAFE-003", command_budget_aborts_script_not_tick, "needs interpreter") {
  auto reg = CommandRegistry::with_defaults();
  NullHost h;
  Program p;  // infinite jump loop once the lowering exists
  Interpreter in(p, h, reg, {}, RunLimits{100, 10});
  CHECK(in.run() == RunOutcome::BudgetExceeded);
}

PENDING("LNPC-SAFE-004", freeloop_without_permission_is_denied_at_runtime, "needs interpreter") {
  auto reg = CommandRegistry::with_defaults();
  NullHost h;
  Program p;
  Instr i; i.op = Op::Command; i.name = "freeloop";
  p.code.push_back(i);
  Interpreter in(p, h, reg, {});
  CHECK(in.run() == RunOutcome::PermissionDenied);
}

PENDING("LNPC-SAFE-005", atcommand_query_sql_setunitdata_denied_at_runtime, "needs interpreter") {
  auto reg = CommandRegistry::with_defaults();
  NullHost h;
  for (auto c : {"atcommand", "query_sql", "setunitdata"}) {
    Program p;
    Instr i; i.op = Op::Command; i.name = c;
    p.code.push_back(i);
    Interpreter in(p, h, reg, {});
    CHECK(in.run() == RunOutcome::PermissionDenied);
  }
}

PENDING("LNPC-YIELD-002", blocking_commands_yield_and_disconnect_cancels, "needs interpreter") {
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
