// Gateway entry point (skeleton): parses --role and exits. Wiring of IO pool,
// Argon2id pool and the persistence-backed stores lands with M6.
#include <cstring>
#include <iostream>

#include "gateway/role.hpp"

int main(int argc, char** argv) {
  gateway::Role role = gateway::Role::All;
  for (int i = 1; i < argc; ++i) {
    if (std::strncmp(argv[i], "--role=", 7) == 0) {
      auto r = gateway::parse_role(argv[i] + 7);
      if (!r) { std::cerr << "invalid --role (all|gateway|world)\n"; return 2; }
      role = *r;
    }
  }
  std::cout << "gateway skeleton: gateway=" << gateway::role_runs_gateway(role)
            << " world=" << gateway::role_runs_world(role) << "\n";
  return 0;
}
