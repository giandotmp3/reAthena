#pragma once
// Luau scenario runner. A scenario is a .luau file run in its own sandboxed script-host state with a
// `sim` global (sim/scenario_prelude.luau). Directives are comment lines at the top of the file:
//
//   --@rule BR-RULESET-020 BR-RULESET-115     rule ids the scenario covers (coverage report)
//   -- rule: BR-RULESET-020                    older spelling, same meaning
//   --@mods rathena.base,legacy.npc            mods to load (default: rathena.base, ruleset.prerenewal, legacy.npc)
//   --@mods-dir path                           extra directory searched for mod.toml (relative to the file)
//   --@seed 42
//   --@map 200x200                             map size in cells
//   --@golden subset|full                      stage the ruleset's golden data into its bundle
//   --@test-support                            stage the ruleset's tests/support (fake ctx, scripted RNG)
//   --@grant mod=perm1,perm2                   mods.lock grants
//   --@memory-storage                          use script-host's in-memory backend instead of SQLite
//   --@npc npc/custom/healer.txt               legacy NPC file (relative to the repository root), loaded first
//   --@strict-parity off                       let the pipeline draw from the main stream without error
#include <string>
#include <vector>

#include "tooling/sim.hpp"
#include "tooling/sim_world.hpp"

namespace tooling::sim {

struct ScenarioSpec {
  std::string path;
  std::string source;
  std::vector<std::string> rule_tags;
  SimConfig cfg;
  std::vector<std::string> npc_files;
};

// Repository root (the directory holding npc/ and src/), found above the engine tree.
std::filesystem::path repo_root();

ScenarioSpec parse_scenario(const std::string& path, SimConfig base = {});
// Runs one scenario in a fresh Sim. `seed` overrides the directive when set.
ScenarioResult run_scenario_spec(ScenarioSpec spec);
// *.luau files of a directory (not recursive), sorted.
std::vector<std::string> list_scenarios(const std::string& dir);

}  // namespace tooling::sim
