#pragma once
// Command line of rathena-sim:
//   run <scenario.luau | dir>... [--seed N] [--json]
//   coverage [--json | --json-out FILE] [--suites] [--no-run] [--no-static] [--test-output FILE]...
//            [--tests-bin PATH]... [--rules FILE]... [--scenarios DIR]... [--require SPEC] [--require-level passing|covered]
//   conformance --ruleset prerenewal|renewal [--json] [--no-suite] [--no-golden] [--subset N]
//   load [--players N] [--seconds S] [--seed N]
//   mods
#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

namespace tooling::sim {

int cli_main(int argc, char** argv, std::ostream& out, std::ostream& err);

struct LoadReport {
  int players = 0;
  double virtual_s = 0, wall_s = 0, setup_wall_s = 0;
  double wall_per_virtual_s = 0;  // seconds of wall clock per second of virtual time
  uint64_t steps = 0, trace_events = 0, trace_hash = 0, walks = 0, chats = 0;
  uint64_t frames_received = 0, bytes_received = 0;
  uint64_t main_rng_draws = 0, native_rng_draws = 0;
  std::string error;
};
// N players random-walking and chatting for `virtual_ms` of virtual time. Deterministic per seed.
LoadReport run_load(int players, int64_t virtual_ms, uint64_t seed, const std::vector<std::string>& mods = {});

}  // namespace tooling::sim
