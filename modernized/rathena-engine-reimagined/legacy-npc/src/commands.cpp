#include "legacy_npc/commands.hpp"

#include <array>
#include <set>

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

namespace {

// Every command implemented by the Luau tables (luau/commands.luau, commands_ext.luau, commands_rt.luau).
constexpr std::array kSupported = {
#define X(name) std::string_view(#name),
#include "supported_commands.inc"
#undef X
};

bool starts(std::string_view s, std::string_view p) { return s.substr(0, p.size()) == p; }

// "pure": reads and writes script variables only. "reads world": asks the world. Everything else writes the world.
CommandClass classify(std::string_view n) {
  static const std::set<std::string_view> pure = {
      "abs", "atoi", "axtoi", "charat", "charisalpha", "charislower", "charisupper", "compare", "countstr", "delchar", "distance",
      "explode", "getarg", "getargcount", "getarraysize", "getelementofarray", "getstrlen", "implode", "inarray", "countinarray",
      "insertchar", "max", "min", "pow", "rand", "replacestr", "setchar", "sprintf", "sqrt", "sscanf", "strcmp", "strlen", "strpos",
      "strtol", "strtolower", "strtoupper", "substr", "set", "setarray", "cleararray", "copyarray", "deletearray", "getd", "setd",
      "getvariableofnpc", "clear"};
  if (is_yielding_command(n)) return CommandClass::Yields;
  if (pure.count(n)) return CommandClass::Pure;
  if (starts(n, "get") || starts(n, "is") || starts(n, "check") || starts(n, "count") || n == "strcharinfo" || n == "strnpcinfo" ||
      n == "mobcount" || n == "readparam" || n == "jobname" || n == "playerattached" || n == "rid2name" || n == "query_sql")
    return CommandClass::ReadsWorld;
  return CommandClass::WritesWorld;
}

}  // namespace

CommandRegistry CommandRegistry::with_defaults() {
  CommandRegistry r;
  for (auto n : kSupported) r.add({std::string(n), classify(n), std::nullopt, 0});
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
