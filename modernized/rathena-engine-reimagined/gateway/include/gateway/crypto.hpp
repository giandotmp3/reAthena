#pragma once
// Self-contained primitives used by the gateway (no third-party crypto library is needed or linked):
// MD5 (legacy wire shim only), SHA-256, HMAC-SHA256, PBKDF2-HMAC-SHA256 (RFC 8018 / RFC 7914 vectors),
// base64 without padding (PHC string format), hex, constant-time comparison.
#include <cstdint>
#include <string>
#include <string_view>

namespace gateway::crypto {

std::string md5(std::string_view data);      // 16 raw bytes
std::string sha256(std::string_view data);   // 32 raw bytes
std::string hmac_sha256(std::string_view key, std::string_view data);
/// PBKDF2-HMAC-SHA256. `iterations` must be >= 1; `dk_len` bytes are returned.
std::string pbkdf2_sha256(std::string_view password, std::string_view salt, std::uint32_t iterations,
                          std::size_t dk_len);

std::string to_hex(std::string_view raw);
/// Lowercase-hex of MD5; what the legacy login server compares (MD5_String).
std::string md5_hex(std::string_view data);

std::string base64_nopad_encode(std::string_view raw);
/// Returns false on any character outside the alphabet or an impossible length.
bool base64_nopad_decode(std::string_view text, std::string& out);

/// Compares in time independent of where the first difference is (length is not secret here).
bool constant_time_equal(std::string_view a, std::string_view b) noexcept;

/// Fills `n` bytes from the operating system's CSPRNG (std::random_device).
std::string random_bytes(std::size_t n);

}  // namespace gateway::crypto
