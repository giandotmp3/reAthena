#pragma once
#include <compare>
#include <cstdint>
#include <optional>
#include <string>

namespace mods {

struct SemVer {
  std::uint32_t major = 0, minor = 0, patch = 0;
  friend bool operator==(const SemVer&, const SemVer&) = default;
  friend auto operator<=>(const SemVer&, const SemVer&) = default;
};

// "1.2.3" -> SemVer; nullopt when malformed.
std::optional<SemVer> parse_semver(const std::string& s);

// Range such as "^1", "^1.2", ">=1.0 <2", "1.2.3". `satisfies` is the single
// entry point used for both `depends` and `engine_api`.
bool satisfies(const SemVer& v, const std::string& range);

}  // namespace mods
