#include "legacy_npc/commands.hpp"

namespace legacy_npc {

std::string_view permission_name(Permission p) {
  switch (p) {
    case Permission::AtCommand: return "atcommand";
    case Permission::QuerySql: return "legacy.query_sql";
    case Permission::SetUnitData: return "legacy.setunitdata";
    case Permission::UnboundedLoop: return "unbounded_loop";
  }
  return "";
}

bool is_yielding_command(std::string_view n) {
  return n == "next" || n == "close2" || n == "menu" || n == "select" || n == "prompt" ||
         n == "input" || n == "sleep" || n == "sleep2" || n == "progressbar" ||
         n == "progressbar_npc";
}

CommandRegistry CommandRegistry::with_defaults() {
  CommandRegistry r;
  for (auto n : {"next", "close2", "menu", "select", "prompt", "input", "sleep", "sleep2",
                 "progressbar", "progressbar_npc"})
    r.add({n, CommandClass::Yields, std::nullopt, 1});
  // close ends the script; mes/callshop/waitingroom do not yield.
  r.add({"close", CommandClass::WritesWorld, std::nullopt, 1});
  r.add({"mes", CommandClass::WritesWorld, std::nullopt, 1});
  r.add({"callshop", CommandClass::WritesWorld, std::nullopt, 1});
  r.add({"waitingroom", CommandClass::WritesWorld, std::nullopt, 1});
  r.add({"getarraysize", CommandClass::Pure, std::nullopt, 1});
  r.add({"donpcevent", CommandClass::WritesWorld, std::nullopt, 1});
  r.add({"freeloop", CommandClass::WritesWorld, Permission::UnboundedLoop, 1});
  r.add({"atcommand", CommandClass::WritesWorld, Permission::AtCommand, 0});
  r.add({"query_sql", CommandClass::ReadsWorld, Permission::QuerySql, 0});
  r.add({"setunitdata", CommandClass::WritesWorld, Permission::SetUnitData, 0});
  return r;
}

void CommandRegistry::add(CommandInfo info) {
  auto name = info.name;
  cmds_[name] = std::move(info);
}

const CommandInfo* CommandRegistry::find(std::string_view name) const {
  auto it = cmds_.find(name);
  return it == cmds_.end() ? nullptr : &it->second;
}

Resolution resolve(const CommandRegistry& reg, std::string_view cmd, const PermissionSet& granted) {
  const auto* c = reg.find(cmd);
  if (!c) return Resolution::Unsupported;
  if (c->required && granted.count(*c->required) == 0) return Resolution::PermissionDenied;
  return Resolution::Supported;
}

}  // namespace legacy_npc
