#include "legacy_npc/report.hpp"

#include <algorithm>
#include <map>
#include <set>

namespace legacy_npc {

// Per file: the NPCs that call at least one command the registry resolves as Unsupported are disabled; the others are untouched. The
// commands of an NPC are those its Program lists (`used_commands` from the Luau lowering, nested calls included) plus the top-level
// command instructions of a hand-built Program.
FileReport analyze_file(const ParsedFile& f, const CommandRegistry& reg) {
  FileReport r;
  r.file = f.file_name;
  std::map<std::string, std::size_t> counts;
  for (const auto& npc : f.npcs) {
    if (npc.kind != DeclKind::Script && npc.kind != DeclKind::Function && npc.kind != DeclKind::Duplicate) continue;
    std::set<std::string> used(npc.program.used_commands.begin(), npc.program.used_commands.end());
    for (const auto& in : npc.program.code)
      if (in.op == Op::Command && !in.name.empty()) used.insert(in.name);
    bool disabled = false;
    for (const auto& c : used)
      if (resolve(reg, c, {}) == Resolution::Unsupported) {
        ++counts[c];
        disabled = true;
      }
    if (disabled && npc.kind != DeclKind::Function) r.disabled_npcs.push_back(npc.name);
  }
  for (auto& [name, n] : counts) r.commands.push_back({name, n});
  return r;
}

}  // namespace legacy_npc
