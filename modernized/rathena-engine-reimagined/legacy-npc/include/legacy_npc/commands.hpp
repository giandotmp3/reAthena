#pragma once
#include <cstddef>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>

namespace legacy_npc {

enum class CommandClass { Pure, ReadsWorld, WritesWorld, Yields };

enum class Permission { AtCommand, QuerySql, SetUnitData, UnboundedLoop };
using PermissionSet = std::set<Permission>;
std::string_view permission_name(Permission p);

struct CommandInfo {
  std::string name;
  CommandClass cls = CommandClass::Pure;
  std::optional<Permission> required;  // gated commands
  int usage_rank = 0;                  // 0 = on-demand tier; 1..388 = used in npc/
};

class CommandRegistry {
 public:
  // Seeded with yielding commands, gated commands and a starter set.
  // The full 388-command first tier is filled in by the build-out milestone (M4).
  static CommandRegistry with_defaults();
  void add(CommandInfo info);
  const CommandInfo* find(std::string_view name) const;
  std::size_t size() const { return cmds_.size(); }

 private:
  std::map<std::string, CommandInfo, std::less<>> cmds_;
};

enum class Resolution { Supported, Unsupported, PermissionDenied };
// Unknown commands resolve to Unsupported (typed, per-NPC); never throw.
Resolution resolve(const CommandRegistry& reg, std::string_view cmd, const PermissionSet& granted);

// next close2 menu select prompt input sleep sleep2 progressbar progressbar_npc
bool is_yielding_command(std::string_view name);

}  // namespace legacy_npc
