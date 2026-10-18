#pragma once
// SHA-256 (FIPS 180-4), implemented here so the unit needs no crypto library.
// This is a real, standard hash function; what is built on top of it in
// lock.hpp is documented there (it is NOT a public-key signature).
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace mods {

class Sha256 {
public:
  Sha256();
  void update(const void* data, std::size_t n);
  void update(std::string_view s) { update(s.data(), s.size()); }
  std::array<std::uint8_t, 32> finish();  // the object must not be reused afterwards
private:
  void block(const std::uint8_t* p);
  std::uint32_t h_[8];
  std::uint8_t buf_[64];
  std::size_t buf_len_ = 0;
  std::uint64_t total_ = 0;
};

std::string to_hex(const std::uint8_t* p, std::size_t n);
// Lowercase hex of SHA-256(data), 64 characters.
std::string sha256_hex(std::string_view data);
// "sha256:<hex>" of a file's bytes; throws std::runtime_error if unreadable.
std::string sha256_file(const std::filesystem::path& file);

// Digest of a package directory: SHA-256 over the sorted list of
// "<relative path>\0<sha256 of file>\n" for every regular file (generic
// separators). Returned as "sha256:<hex>". Order- and platform-independent.
std::string package_digest(const std::filesystem::path& dir);

}  // namespace mods
