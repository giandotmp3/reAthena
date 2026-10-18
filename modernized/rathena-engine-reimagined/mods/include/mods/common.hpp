#pragma once
#include <stdexcept>
#include <string>

namespace mods {

// Thrown by API stubs that are scaffolded but not yet implemented.
// Acceptance tests tagged PENDING expect a failure until the feature lands.
class NotImplemented : public std::logic_error {
public:
  explicit NotImplemented(const std::string& what)
      : std::logic_error("not implemented: " + what) {}
};

// Load-time refusal. `code` is a stable machine-readable token; `what()` is
// the precise operator-facing message (names the mods/permissions involved).
class LoadError : public std::runtime_error {
public:
  LoadError(std::string code, const std::string& msg)
      : std::runtime_error(msg), code_(std::move(code)) {}
  const std::string& code() const noexcept { return code_; }
private:
  std::string code_;
};

}  // namespace mods
