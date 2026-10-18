#include "netproto/obfuscation.hpp"

#include <cctype>
#include <cstdlib>

namespace netproto {

std::optional<ObfuscationKeys> parse_keys(std::string_view text) {
  ObfuscationKeys out;
  std::size_t n = 0, i = 0;
  while (i < text.size()) {
    while (i < text.size() && (text[i] == ',' || std::isspace(static_cast<unsigned char>(text[i])))) ++i;
    if (i >= text.size()) break;
    if (n == 3) return std::nullopt;
    unsigned base = 10;
    if (i + 1 < text.size() && text[i] == '0' && (text[i + 1] == 'x' || text[i + 1] == 'X')) { base = 16; i += 2; }
    std::uint64_t v = 0;
    std::size_t digits = 0;
    for (; i < text.size() && text[i] != ',' && !std::isspace(static_cast<unsigned char>(text[i])); ++i, ++digits) {
      const char c = text[i];
      unsigned d;
      if (c >= '0' && c <= '9') d = static_cast<unsigned>(c - '0');
      else if (c >= 'a' && c <= 'f') d = static_cast<unsigned>(c - 'a') + 10;
      else if (c >= 'A' && c <= 'F') d = static_cast<unsigned>(c - 'A') + 10;
      else return std::nullopt;
      if (d >= base) return std::nullopt;
      v = v * base + d;
      if (v > 0xFFFFFFFFull) return std::nullopt;
    }
    if (digits == 0) return std::nullopt;
    out.k[n++] = static_cast<std::uint32_t>(v);
  }
  if (n != 3) return std::nullopt;
  return out;
}

std::optional<ObfuscationKeys> keys_from_env(const std::string& env_name) {
  if (env_name.empty()) return std::nullopt;
  const char* v = std::getenv(env_name.c_str());
  if (!v) return std::nullopt;
  return parse_keys(v);
}

}  // namespace netproto
