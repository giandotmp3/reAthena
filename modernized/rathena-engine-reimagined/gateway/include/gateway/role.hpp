#pragma once
// Process roles (architecture section 3): one binary, `--role=all|gateway|world`.
//   all      gateway and world in one process (the milestone-1 deployment, the one that is tested)
//   gateway  only login / char-select / IO pool; the WorldBus is the loopback transport to a world
//            that lives elsewhere (until that peer is connected, available() is false and logins are
//            refused with "server closed", the documented fail-closed behaviour)
//   world    only the simulation side; no gateway thread is started
#include <optional>
#include <string>
#include <vector>

namespace gateway {

enum class Role { All, Gateway, World };

// Parses the value of --role=. Unknown values yield nullopt (caller exits non-zero).
std::optional<Role> parse_role(const std::string& value);
bool role_runs_gateway(Role r);
bool role_runs_world(Role r);
const char* role_name(Role r);

struct CliOptions {
  Role role = Role::All;
  bool role_from_cli = false;
  std::string config_path;
  bool check_only = false;       // --check: parse and validate, print the plan, start nothing
  int run_for_ms = 0;            // --run-for=MS: start the gateway runtime, run, shut down (smoke test)
  std::string error;             // non-empty = invalid command line / config (exit code 2)
};

/// Parses argv. `--role=X`, `--config=PATH`, `--check`, `--run-for=MS`; later duplicates win; anything
/// else is an error (no silent ignoring of typos).
CliOptions parse_cli(const std::vector<std::string>& args);

/// A `role = "gateway"` line under [process] of the config file; the command line wins over it.
std::optional<Role> role_from_config_text(const std::string& text, std::string* error);

struct ProcessPlan {
  bool gateway_threads = false;
  bool world_side = false;
  bool needs_world_peer = false;  // gateway role: the loopback transport must be attached to a world
};
ProcessPlan plan_for(Role r);

}  // namespace gateway
