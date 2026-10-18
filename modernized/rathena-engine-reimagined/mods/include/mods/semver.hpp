#pragma once
#include <compare>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace mods {

struct SemVer {
  std::uint32_t major = 0, minor = 0, patch = 0;
  friend bool operator==(const SemVer&, const SemVer&) = default;
  friend auto operator<=>(const SemVer&, const SemVer&) = default;
};

// "1.2.3" -> SemVer; nullopt when malformed.
std::optional<SemVer> parse_semver(const std::string& s);

// Version ranges, shared by `depends` and `engine_api`.
//
// Grammar: a range is zero or more terms separated by whitespace and/or
// commas; every term must hold (AND). An empty range or "*" matches anything.
//   term   := [op] partial        op := ^ ~ >= <= > < =
//   partial:= N | N.N | N.N.N     (a component may be x, X or * instead of a
//                                  number, but only after the specified ones)
// Semantics (npm-style, no pre-releases):
//   ^1.2.3  >=1.2.3 <2.0.0      ^0.1 >=0.1.0 <0.2.0      ^0.0.3 exactly 0.0.3
//   ^0      >=0.0.0 <1.0.0      ~1.2.3 >=1.2.3 <1.3.0    ~1 >=1.0.0 <2.0.0
//   1.2     >=1.2.0 <1.3.0      1.2.3 exactly            >1.2 >=1.3.0
//   <=1     <2.0.0              ">= 1.0"  (space after the operator is fine)
struct Comparator {
  enum class Op { Ge, Gt, Le, Lt, Eq } op = Op::Ge;
  SemVer v;
};
struct VersionRange {
  std::vector<Comparator> all;  // conjunction; empty = any version
  bool matches(const SemVer& v) const;
};
// nullopt on malformed input; `why` (optional) receives the reason.
std::optional<VersionRange> parse_range(const std::string& text, std::string* why = nullptr);

// True when `v` satisfies `range`. A malformed range never matches (secure
// default); manifest validation rejects malformed ranges before this matters.
bool satisfies(const SemVer& v, const std::string& range);

std::string to_string(const SemVer& v);

}  // namespace mods
