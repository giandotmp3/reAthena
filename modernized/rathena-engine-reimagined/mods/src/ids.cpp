#include "mods/ids.hpp"

#include <cctype>

namespace mods {
namespace {
bool lower_alnum(char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); }
bool all_of(const std::string& s, bool (*first)(char), bool (*rest)(char)) {
  if (s.empty() || !first(s[0])) return false;
  for (char c : s) if (!rest(c)) return false;
  return true;
}
bool mod_rest(char c) { return lower_alnum(c) || c == '.' || c == '_' || c == '-'; }
bool kind_first(char c) { return c >= 'a' && c <= 'z'; }
bool kind_rest(char c) { return lower_alnum(c) || c == '_'; }
bool name_rest(char c) { return lower_alnum(c) || c == '_' || c == '.' || c == '/' || c == '-'; }
}  // namespace

bool is_valid_mod_id(const std::string& s) { return all_of(s, lower_alnum, mod_rest); }

std::optional<NamespacedId> parse_namespaced_id(const std::string& s) {
  auto colon = s.find(':');
  if (colon == std::string::npos) return std::nullopt;
  auto slash = s.find('/', colon + 1);
  if (slash == std::string::npos) return std::nullopt;
  NamespacedId id{s.substr(0, colon), s.substr(colon + 1, slash - colon - 1),
                  s.substr(slash + 1)};
  if (!is_valid_mod_id(id.mod)) return std::nullopt;
  if (!all_of(id.kind, kind_first, kind_rest)) return std::nullopt;
  if (!all_of(id.name, lower_alnum, name_rest)) return std::nullopt;
  if (id.name.find("..") != std::string::npos) return std::nullopt;
  return id;
}

}  // namespace mods
