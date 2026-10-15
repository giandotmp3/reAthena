#pragma once
// RFC 1321 MD5. Used ONLY for wire compatibility: the BR-LOGIN-003 challenge
// shim and the BR-LOGIN-013 client hash check. Never used for password storage.

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace rathena::login {

using Md5Digest = std::array<std::uint8_t, 16>;

Md5Digest md5(std::span<const std::uint8_t> data);
Md5Digest md5(std::string_view data);
std::string to_hex(const Md5Digest& d);

}  // namespace rathena::login
