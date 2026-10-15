// Minimal SHA-256 (FIPS 180-4). Used so the known-default credential denylist can be
// stored as digests: no legacy credential literal appears anywhere in source (S1).
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace rathena::core {

std::array<std::uint8_t, 32> sha256(std::string_view data);
std::string sha256_hex(std::string_view data);

} // namespace rathena::core
