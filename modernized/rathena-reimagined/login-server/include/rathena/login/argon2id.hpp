#pragma once
// Argon2id primitives for password-at-rest (S6, HITL #1 drop decision 3, migration M6).
//
// Backed by the Argon2 PHC reference implementation (P-H-C/phc-winner-argon2, release
// 20190702), built without its own threads: concurrency comes from HashWorkerPool.
// Encoded hashes are standard PHC strings,
//   $argon2id$v=19$m=<KiB>,t=<passes>,p=<lanes>$<salt b64>$<hash b64>
// (unpadded standard base64), the format libsodium's crypto_pwhash_str{,_verify} reads and
// writes. With the defaults below (16-byte salt, 32-byte hash) the strings are
// byte-for-byte what crypto_pwhash_str would produce for the same salt and parameters.
//
// Everything here is thread-safe: no state is shared between calls.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace rathena::login {

/// Cost parameters. Defaults: OWASP Password Storage Cheat Sheet, Argon2id option 2
/// (19 MiB, 2 passes, 1 lane).
struct Argon2Params {
  std::uint32_t memory_kib = 19456;
  std::uint32_t iterations = 2;
  std::uint32_t parallelism = 1;
};

inline constexpr std::size_t kArgon2SaltBytes = 16;  // crypto_pwhash_SALTBYTES
inline constexpr std::size_t kArgon2HashBytes = 32;  // crypto_pwhash_STRBYTES layout
inline constexpr std::uint32_t kArgon2Version = 0x13;  // v=19

/// Upper bounds accepted from a stored hash (or from config). A tampered row must not be
/// able to make one login allocate gigabytes or spin for minutes on a pool worker.
inline constexpr std::uint32_t kArgon2MaxMemoryKib = 4u * 1024u * 1024u;  // 4 GiB
inline constexpr std::uint32_t kArgon2MaxIterations = 256;
inline constexpr std::uint32_t kArgon2MaxParallelism = 64;

/// Thrown for invalid parameters, a failed CSPRNG or an internal Argon2 error.
class Argon2Error : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

/// Throws Argon2Error unless 1 <= t <= kArgon2MaxIterations, 1 <= p <= kArgon2MaxParallelism
/// and 8*p <= m <= kArgon2MaxMemoryKib (the Argon2 minimum is 8 KiB per lane).
void validate_argon2_params(const Argon2Params& params);

/// A parsed `$argon2id$` PHC string.
struct Argon2idPhc {
  Argon2Params params;
  std::vector<std::uint8_t> salt;
  std::vector<std::uint8_t> hash;
};

/// Strict parser: `$argon2id$v=19$m=..,t=..,p=..$salt$hash`, decimal fields without
/// leading zeros, canonical unpadded base64, salt 8..64 bytes, hash 16..64 bytes, and
/// parameters within the bounds above. Anything else (argon2i/argon2d, a missing or other
/// version, trailing data) is nullopt.
std::optional<Argon2idPhc> parse_argon2id_phc(std::string_view encoded);

/// Inverse of parse_argon2id_phc.
std::string format_argon2id_phc(const Argon2idPhc& phc);

/// Raw Argon2id (version 19) with optional secret key K and associated data X, as in
/// RFC 9106 §3.1. Fills `out` (4..64 bytes here). Throws Argon2Error on invalid input.
void argon2id_raw(const Argon2Params& params, std::span<const std::uint8_t> password,
                  std::span<const std::uint8_t> salt, std::span<const std::uint8_t> secret,
                  std::span<const std::uint8_t> associated_data, std::span<std::uint8_t> out);

/// PHC-encoded Argon2id hash of `password` with a caller-chosen salt and a 32-byte tag.
/// Production code must use a CSPRNG salt (Argon2idHasher::hash does); a fixed salt is
/// only for known-answer tests.
std::string argon2id_encode(const Argon2Params& params, std::string_view password,
                            std::span<const std::uint8_t> salt);

/// Verifies `password` against a PHC string. The tag comparison is constant-time
/// (the reference implementation's argon2_compare). nullopt = malformed/unsupported.
std::optional<bool> argon2id_check(std::string_view encoded, std::string_view password);

/// Fills `out` from the operating system CSPRNG: getrandom(2) on Linux, arc4random_buf on
/// macOS/BSD, BCryptGenRandom on Windows, /dev/urandom elsewhere. Throws Argon2Error if
/// the OS facility fails; it never falls back to a weaker generator.
void os_random_bytes(std::span<std::uint8_t> out);

}  // namespace rathena::login
