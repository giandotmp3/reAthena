#pragma once
#include <string>
#include <vector>

#include "legacy_npc/ast.hpp"
#include "legacy_npc/commands.hpp"

namespace legacy_npc {

struct UnsupportedCommand {
  std::string command;
  std::size_t count = 0;
};

struct FileReport {
  std::string file;
  std::vector<UnsupportedCommand> commands;
  std::vector<std::string> disabled_npcs;  // only NPCs that use them
};

struct CorpusReport {
  std::vector<FileReport> files;
};

// Stub. Disables only the NPCs that use an unsupported command.
FileReport analyze_file(const ParsedFile& f, const CommandRegistry& reg);

}  // namespace legacy_npc
