#pragma once
// Tool configuration. Secrets come from the environment only; config files
// carry ${ENV_VAR} placeholders, never literals.
#include <map>
#include <string>

namespace tooling {

using Env = std::map<std::string, std::string>;

// Expand ${NAME} using env. Unknown names throw std::runtime_error.
std::string expand_placeholders(const std::string& text, const Env& env);

// True if a connection string embeds a literal password (user:pass@host or
// password=...). Placeholders like ${DATABASE_URL} are fine.
bool has_literal_credential(const std::string& value);

// Throws std::runtime_error if value is a connection string with a literal credential.
void require_no_literal_credential(const std::string& key, const std::string& value);

}  // namespace tooling
