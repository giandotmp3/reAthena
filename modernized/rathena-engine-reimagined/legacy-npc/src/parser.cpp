#include "legacy_npc/parser.hpp"

namespace legacy_npc {

std::uint64_t source_hash(std::string_view s) {
  std::uint64_t h = 1469598103934665603ULL;
  for (unsigned char c : s) {
    h ^= c;
    h *= 1099511628211ULL;
  }
  return h;
}

ParsedFile parse_file(std::string_view, std::string_view) { throw NotImplemented("parse_file"); }
std::string serialize_ast(const ParsedFile&) { throw NotImplemented("serialize_ast"); }
ParsedFile deserialize_ast(std::string_view, std::uint64_t) {
  throw NotImplemented("deserialize_ast");
}

}  // namespace legacy_npc
