#pragma once
#include <compare>
#include <optional>
#include <string>
#include <string_view>

namespace nd {

// "native.damage.defense_soft@1" -> {name="native.damage.defense_soft", version=1}
struct StageId {
  std::string name;
  int version = 0;

  std::string str() const;
  auto operator<=>(const StageId&) const = default;

  // Valid: lowercase dotted segments, must start with "native.", "@<positive int>".
  static std::optional<StageId> parse(std::string_view text);
};

}  // namespace nd
