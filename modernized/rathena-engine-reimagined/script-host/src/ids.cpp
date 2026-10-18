#include "script_host/ids.hpp"

namespace script_host {

static bool ok_part(std::string_view s, bool allow_upper = false) {
  if (s.empty()) return false;
  for (char c : s)
    if (!((c >= 'a' && c <= 'z') || (allow_upper && c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.')) return false;
  return true;
}

std::optional<NamespacedId> NamespacedId::parse(std::string_view s) {
  auto colon = s.find(':');
  if (colon == std::string_view::npos) return std::nullopt;
  auto slash = s.find('/', colon + 1);
  if (slash == std::string_view::npos) return std::nullopt;
  auto mod = s.substr(0, colon);
  auto kind = s.substr(colon + 1, slash - colon - 1);
  auto name = s.substr(slash + 1);
  if (!ok_part(mod) || !ok_part(kind) || !ok_part(name)) return std::nullopt;
  return NamespacedId{std::string(mod), std::string(kind), std::string(name)};
}

std::optional<EventName> EventName::parse(std::string_view s) {
  auto colon = s.find(':');
  if (colon == std::string_view::npos) return std::nullopt;
  auto ns = s.substr(0, colon);
  auto ev = s.substr(colon + 1);
  if (!ok_part(ns) || !ok_part(ev, true)) return std::nullopt;
  return EventName{std::string(ns), std::string(ev)};
}

}  // namespace script_host
