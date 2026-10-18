#pragma once
#include <optional>
#include <string>

namespace gateway {

enum class Role { All, Gateway, World };

// Parses the value of --role=. Unknown values yield nullopt (caller exits non-zero).
std::optional<Role> parse_role(const std::string& value);
bool role_runs_gateway(Role r);
bool role_runs_world(Role r);

}  // namespace gateway
