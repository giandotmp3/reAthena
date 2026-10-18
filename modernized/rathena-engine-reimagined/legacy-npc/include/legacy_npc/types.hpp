#pragma once
#include <cstdint>
#include <stdexcept>
#include <string>
#include <variant>

namespace legacy_npc {

// Thrown by unimplemented stubs. Pending acceptance tests expect it.
struct NotImplemented : std::logic_error {
  using std::logic_error::logic_error;
};

inline constexpr const char* kModId = "legacy";
using Value = std::variant<std::int64_t, std::string>;

}  // namespace legacy_npc
