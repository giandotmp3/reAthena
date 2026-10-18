#pragma once
#include <cstdint>
#include <string>
#include <string_view>

#include "legacy_npc/ast.hpp"

namespace legacy_npc {

// FNV-1a 64 over the raw bytes; cache invalidation key.
std::uint64_t source_hash(std::string_view source);

// Declaration forms and script grammar -> ParsedFile. Stub.
ParsedFile parse_file(std::string_view source, std::string_view file_name);

// Binary cache. Stubs. load returns nullopt-equivalent (empty npcs + hash 0) on hash mismatch.
std::string serialize_ast(const ParsedFile& f);
ParsedFile deserialize_ast(std::string_view blob, std::uint64_t expected_hash);

}  // namespace legacy_npc
