#include "mods/semver.hpp"

#include "mods/common.hpp"

namespace mods {

std::optional<SemVer> parse_semver(const std::string& s) {
  SemVer v;
  std::uint32_t* parts[3] = {&v.major, &v.minor, &v.patch};
  std::size_t i = 0;
  for (int p = 0; p < 3; ++p) {
    if (i >= s.size()) return std::nullopt;
    std::uint64_t n = 0;
    std::size_t start = i;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
      n = n * 10 + static_cast<std::uint64_t>(s[i] - '0');
      if (n > 0xFFFFFFFFull) return std::nullopt;
      ++i;
    }
    if (i == start) return std::nullopt;
    if (s[start] == '0' && i - start > 1) return std::nullopt;  // no leading zeros
    *parts[p] = static_cast<std::uint32_t>(n);
    if (p < 2) {
      if (i >= s.size() || s[i] != '.') return std::nullopt;
      ++i;
    }
  }
  if (i != s.size()) return std::nullopt;
  return v;
}

bool satisfies(const SemVer&, const std::string&) {
  throw NotImplemented("satisfies (version range matching)");
}

}  // namespace mods
