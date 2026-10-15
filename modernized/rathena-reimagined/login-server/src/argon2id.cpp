#include "rathena/login/argon2id.hpp"

#include <argon2.h>  // P-H-C reference implementation (SYSTEM include, built by CMake)

#include <algorithm>
#include <charconv>
#include <cerrno>
#include <cstdio>
#include <limits>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <bcrypt.h>
#elif defined(__linux__) && defined(__has_include)
#if __has_include(<sys/random.h>)
#include <sys/random.h>
#define RATHENA_HAVE_GETRANDOM 1
#endif
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
#include <stdlib.h>  // arc4random_buf
#define RATHENA_HAVE_ARC4RANDOM 1
#endif

namespace rathena::login {
namespace {

constexpr std::string_view kPrefix = "$argon2id$v=19$m=";
constexpr std::size_t kMinSalt = 8, kMaxSalt = 64;  // ARGON2_MIN_SALT_LENGTH .. sane cap
constexpr std::size_t kMinHash = 16, kMaxHash = 64;  // crypto_pwhash_BYTES_MIN .. sane cap
constexpr std::string_view kB64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int b64_value(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

/// Unpadded standard base64; rejects '=', stray characters and non-zero trailing bits so
/// that every byte string has exactly one accepted spelling.
std::optional<std::vector<std::uint8_t>> b64_decode(std::string_view s) {
  if (s.size() % 4 == 1) return std::nullopt;
  std::vector<std::uint8_t> out;
  out.reserve(s.size() * 3 / 4);
  std::uint32_t acc = 0;
  int bits = 0;
  for (char c : s) {
    const int v = b64_value(c);
    if (v < 0) return std::nullopt;
    acc = (acc << 6) | static_cast<std::uint32_t>(v);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push_back(static_cast<std::uint8_t>((acc >> bits) & 0xFF));
    }
  }
  if ((acc & ((1u << bits) - 1u)) != 0) return std::nullopt;
  return out;
}

std::string b64_encode(std::span<const std::uint8_t> in) {
  std::string out;
  out.reserve((in.size() * 4 + 2) / 3);
  std::uint32_t acc = 0;
  int bits = 0;
  for (std::uint8_t b : in) {
    acc = (acc << 8) | b;
    bits += 8;
    while (bits >= 6) {
      bits -= 6;
      out.push_back(kB64[(acc >> bits) & 0x3F]);
    }
  }
  if (bits > 0) out.push_back(kB64[(acc << (6 - bits)) & 0x3F]);
  return out;
}

/// Reads "<key>=<decimal>" with no sign and no leading zero, advancing `s`.
std::optional<std::uint32_t> take_decimal(std::string_view& s, std::string_view key) {
  if (s.substr(0, key.size()) != key) return std::nullopt;
  s.remove_prefix(key.size());
  if (s.empty() || s.front() == '0') return std::nullopt;
  std::uint32_t v = 0;
  const auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
  if (ec != std::errc{}) return std::nullopt;
  s.remove_prefix(static_cast<std::size_t>(p - s.data()));
  return v;
}

bool params_in_bounds(const Argon2Params& p) {
  return p.iterations >= 1 && p.iterations <= kArgon2MaxIterations && p.parallelism >= 1 &&
         p.parallelism <= kArgon2MaxParallelism && p.memory_kib >= 8u * p.parallelism &&
         p.memory_kib <= kArgon2MaxMemoryKib;
}

[[noreturn]] void fail(const char* what, int code) {
  throw Argon2Error(std::string(what) + ": " + argon2_error_message(code));
}

}  // namespace

void validate_argon2_params(const Argon2Params& params) {
  if (!params_in_bounds(params)) {
    throw Argon2Error("Argon2id parameters out of range (m=" + std::to_string(params.memory_kib) +
                      ", t=" + std::to_string(params.iterations) +
                      ", p=" + std::to_string(params.parallelism) + ")");
  }
}

std::optional<Argon2idPhc> parse_argon2id_phc(std::string_view s) {
  if (s.substr(0, kPrefix.size()) != kPrefix) return std::nullopt;
  s.remove_prefix(kPrefix.size() - 2);  // leave "m=" for take_decimal
  Argon2idPhc phc;
  const auto m = take_decimal(s, "m=");
  if (!m) return std::nullopt;
  const auto t = take_decimal(s, ",t=");
  if (!t) return std::nullopt;
  const auto p = take_decimal(s, ",p=");
  if (!p) return std::nullopt;
  phc.params = {*m, *t, *p};
  if (!params_in_bounds(phc.params)) return std::nullopt;

  if (s.empty() || s.front() != '$') return std::nullopt;
  s.remove_prefix(1);
  const auto sep = s.find('$');
  if (sep == std::string_view::npos) return std::nullopt;
  auto salt = b64_decode(s.substr(0, sep));
  auto hash = b64_decode(s.substr(sep + 1));  // a further '$' is not base64 -> rejected
  if (!salt || !hash) return std::nullopt;
  if (salt->size() < kMinSalt || salt->size() > kMaxSalt) return std::nullopt;
  if (hash->size() < kMinHash || hash->size() > kMaxHash) return std::nullopt;
  phc.salt = std::move(*salt);
  phc.hash = std::move(*hash);
  return phc;
}

std::string format_argon2id_phc(const Argon2idPhc& phc) {
  return std::string(kPrefix) + std::to_string(phc.params.memory_kib) +
         ",t=" + std::to_string(phc.params.iterations) +
         ",p=" + std::to_string(phc.params.parallelism) + "$" + b64_encode(phc.salt) + "$" +
         b64_encode(phc.hash);
}

void argon2id_raw(const Argon2Params& params, std::span<const std::uint8_t> password,
                  std::span<const std::uint8_t> salt, std::span<const std::uint8_t> secret,
                  std::span<const std::uint8_t> associated_data, std::span<std::uint8_t> out) {
  validate_argon2_params(params);
  constexpr auto kU32 = std::numeric_limits<std::uint32_t>::max();
  if (password.size() > kU32 || salt.size() > kU32 || secret.size() > kU32 ||
      associated_data.size() > kU32 || out.size() > kU32) {
    throw Argon2Error("Argon2id input too long");
  }
  // The reference API takes non-const pointers but only reads pwd/salt/secret/ad; the
  // CLEAR_* flags (which would write) stay off.
  argon2_context ctx{};
  ctx.out = out.data();
  ctx.outlen = static_cast<std::uint32_t>(out.size());
  ctx.pwd = const_cast<std::uint8_t*>(password.data());
  ctx.pwdlen = static_cast<std::uint32_t>(password.size());
  ctx.salt = const_cast<std::uint8_t*>(salt.data());
  ctx.saltlen = static_cast<std::uint32_t>(salt.size());
  ctx.secret = const_cast<std::uint8_t*>(secret.data());
  ctx.secretlen = static_cast<std::uint32_t>(secret.size());
  ctx.ad = const_cast<std::uint8_t*>(associated_data.data());
  ctx.adlen = static_cast<std::uint32_t>(associated_data.size());
  ctx.t_cost = params.iterations;
  ctx.m_cost = params.memory_kib;
  ctx.lanes = params.parallelism;
  ctx.threads = params.parallelism;  // built with ARGON2_NO_THREADS: lanes run sequentially
  ctx.version = ARGON2_VERSION_13;
  ctx.flags = ARGON2_DEFAULT_FLAGS;
  const int rc = argon2id_ctx(&ctx);
  if (rc != ARGON2_OK) fail("argon2id", rc);
}

std::string argon2id_encode(const Argon2Params& params, std::string_view password,
                            std::span<const std::uint8_t> salt) {
  validate_argon2_params(params);
  if (salt.size() < kMinSalt || salt.size() > kMaxSalt) throw Argon2Error("Argon2id salt length out of range");
  Argon2idPhc phc;
  phc.params = params;
  phc.salt.assign(salt.begin(), salt.end());
  phc.hash.resize(kArgon2HashBytes);
  argon2id_raw(params,
               {reinterpret_cast<const std::uint8_t*>(password.data()), password.size()},
               salt, {}, {}, phc.hash);
  return format_argon2id_phc(phc);
}

std::optional<bool> argon2id_check(std::string_view encoded, std::string_view password) {
  // Strict parse first: it bounds m/t/p before the reference decoder allocates anything.
  if (!parse_argon2id_phc(encoded)) return std::nullopt;
  const std::string z(encoded);  // the reference API wants a NUL-terminated string
  const int rc = argon2id_verify(z.c_str(), password.data(), password.size());
  if (rc == ARGON2_OK) return true;
  if (rc == ARGON2_VERIFY_MISMATCH) return false;
  if (rc == ARGON2_MEMORY_ALLOCATION_ERROR) fail("argon2id_verify", rc);
  return std::nullopt;
}

void os_random_bytes(std::span<std::uint8_t> out) {
#if defined(_WIN32)
  std::size_t done = 0;
  while (done < out.size()) {
    const auto chunk = static_cast<ULONG>(std::min<std::size_t>(out.size() - done, 1u << 20));
    if (!BCRYPT_SUCCESS(BCryptGenRandom(nullptr, out.data() + done, chunk, BCRYPT_USE_SYSTEM_PREFERRED_RNG))) {
      throw Argon2Error("BCryptGenRandom failed");
    }
    done += chunk;
  }
#elif defined(RATHENA_HAVE_GETRANDOM)
  std::size_t done = 0;
  while (done < out.size()) {
    const ssize_t n = getrandom(out.data() + done, out.size() - done, 0);
    if (n < 0) {
      if (errno == EINTR) continue;
      throw Argon2Error("getrandom failed");
    }
    done += static_cast<std::size_t>(n);
  }
#elif defined(RATHENA_HAVE_ARC4RANDOM)
  arc4random_buf(out.data(), out.size());
#else
  std::FILE* f = std::fopen("/dev/urandom", "rb");
  if (f == nullptr) throw Argon2Error("cannot open /dev/urandom");
  const std::size_t n = std::fread(out.data(), 1, out.size(), f);
  std::fclose(f);
  if (n != out.size()) throw Argon2Error("short read from /dev/urandom");
#endif
}

}  // namespace rathena::login
