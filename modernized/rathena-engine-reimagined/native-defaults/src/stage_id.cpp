#include "native_defaults/stage_id.hpp"

#include <cctype>
#include <charconv>

namespace nd {

std::string StageId::str() const { return name + "@" + std::to_string(version); }

std::optional<StageId> StageId::parse(std::string_view text) {
  const auto at = text.rfind('@');
  if (at == std::string_view::npos || at == 0 || at + 1 >= text.size()) return std::nullopt;
  const auto name = text.substr(0, at);
  const auto ver = text.substr(at + 1);
  if (name.rfind("native.", 0) != 0) return std::nullopt;
  bool seg_start = true;
  for (char c : name) {
    if (c == '.') {
      if (seg_start) return std::nullopt;
      seg_start = true;
      continue;
    }
    if (!(std::islower(static_cast<unsigned char>(c)) || std::isdigit(static_cast<unsigned char>(c)) || c == '_'))
      return std::nullopt;
    seg_start = false;
  }
  if (seg_start) return std::nullopt;
  int v = 0;
  const auto [p, ec] = std::from_chars(ver.data(), ver.data() + ver.size(), v);
  if (ec != std::errc{} || p != ver.data() + ver.size() || v < 1) return std::nullopt;
  if (ver.size() > 1 && ver[0] == '0') return std::nullopt;
  return StageId{std::string(name), v};
}

}  // namespace nd
