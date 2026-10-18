#include "gateway/crypto.hpp"

#include <array>
#include <cstring>
#include <random>
#include <vector>

namespace gateway::crypto {

namespace {

inline std::uint32_t rotl(std::uint32_t x, int c) { return (x << c) | (x >> (32 - c)); }
inline std::uint32_t rotr(std::uint32_t x, int c) { return (x >> c) | (x << (32 - c)); }

// ---- MD5 (RFC 1321) ----------------------------------------------------------------------
constexpr std::array<std::uint32_t, 64> kMd5K = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
    0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
    0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
    0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
    0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
    0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
constexpr std::array<int, 64> kMd5S = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
                                       5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
                                       4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                                       6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};

void md5_block(std::uint32_t st[4], const unsigned char* p) {
  std::uint32_t m[16];
  for (int i = 0; i < 16; ++i)
    m[i] = std::uint32_t(p[i * 4]) | std::uint32_t(p[i * 4 + 1]) << 8 | std::uint32_t(p[i * 4 + 2]) << 16 |
           std::uint32_t(p[i * 4 + 3]) << 24;
  std::uint32_t a = st[0], b = st[1], c = st[2], d = st[3];
  for (int i = 0; i < 64; ++i) {
    std::uint32_t f;
    int g;
    if (i < 16) { f = (b & c) | (~b & d); g = i; }
    else if (i < 32) { f = (d & b) | (~d & c); g = (5 * i + 1) % 16; }
    else if (i < 48) { f = b ^ c ^ d; g = (3 * i + 5) % 16; }
    else { f = c ^ (b | ~d); g = (7 * i) % 16; }
    f = f + a + kMd5K[static_cast<std::size_t>(i)] + m[g];
    a = d; d = c; c = b;
    b = b + rotl(f, kMd5S[static_cast<std::size_t>(i)]);
  }
  st[0] += a; st[1] += b; st[2] += c; st[3] += d;
}

// ---- SHA-256 (FIPS 180-4) ----------------------------------------------------------------
constexpr std::array<std::uint32_t, 64> kShaK = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

void sha_block(std::uint32_t h[8], const unsigned char* p) {
  std::uint32_t w[64];
  for (int i = 0; i < 16; ++i)
    w[i] = std::uint32_t(p[i * 4]) << 24 | std::uint32_t(p[i * 4 + 1]) << 16 | std::uint32_t(p[i * 4 + 2]) << 8 |
           std::uint32_t(p[i * 4 + 3]);
  for (int i = 16; i < 64; ++i) {
    std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
    std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
  for (int i = 0; i < 64; ++i) {
    std::uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
    std::uint32_t ch = (e & f) ^ (~e & g);
    std::uint32_t t1 = hh + S1 + ch + kShaK[static_cast<std::size_t>(i)] + w[i];
    std::uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
    std::uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
    std::uint32_t t2 = S0 + mj;
    hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
  }
  h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}

}  // namespace

std::string md5(std::string_view data) {
  std::uint32_t st[4] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};
  std::string msg(data);
  const std::uint64_t bits = static_cast<std::uint64_t>(data.size()) * 8;
  msg.push_back(static_cast<char>(0x80));
  while (msg.size() % 64 != 56) msg.push_back('\0');
  for (int i = 0; i < 8; ++i) msg.push_back(static_cast<char>((bits >> (8 * i)) & 0xff));
  for (std::size_t off = 0; off < msg.size(); off += 64)
    md5_block(st, reinterpret_cast<const unsigned char*>(msg.data()) + off);
  std::string out(16, '\0');
  for (int i = 0; i < 4; ++i)
    for (int j = 0; j < 4; ++j) out[static_cast<std::size_t>(i * 4 + j)] = static_cast<char>((st[i] >> (8 * j)) & 0xff);
  return out;
}

std::string sha256(std::string_view data) {
  std::uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  std::string msg(data);
  const std::uint64_t bits = static_cast<std::uint64_t>(data.size()) * 8;
  msg.push_back(static_cast<char>(0x80));
  while (msg.size() % 64 != 56) msg.push_back('\0');
  for (int i = 7; i >= 0; --i) msg.push_back(static_cast<char>((bits >> (8 * i)) & 0xff));
  for (std::size_t off = 0; off < msg.size(); off += 64)
    sha_block(h, reinterpret_cast<const unsigned char*>(msg.data()) + off);
  std::string out(32, '\0');
  for (int i = 0; i < 8; ++i)
    for (int j = 0; j < 4; ++j) out[static_cast<std::size_t>(i * 4 + j)] = static_cast<char>((h[i] >> (24 - 8 * j)) & 0xff);
  return out;
}

std::string hmac_sha256(std::string_view key, std::string_view data) {
  std::string k(key);
  if (k.size() > 64) k = sha256(k);
  k.resize(64, '\0');
  std::string ipad(64, '\0'), opad(64, '\0');
  for (std::size_t i = 0; i < 64; ++i) {
    ipad[i] = static_cast<char>(k[i] ^ 0x36);
    opad[i] = static_cast<char>(k[i] ^ 0x5c);
  }
  return sha256(opad + sha256(ipad + std::string(data)));
}

namespace {
struct ShaState { std::uint32_t h[8]; };
const ShaState kShaInit = {{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19}};

// One compression of `state` with a 32-byte message that follows a 64-byte keyed prefix:
// total length 96 bytes = 768 bits. Used for the PBKDF2 inner loop (no allocation, midstates cached).
void sha_tail32(ShaState& st, const unsigned char in[32]) {
  unsigned char blk[64] = {};
  std::memcpy(blk, in, 32);
  blk[32] = 0x80;
  const std::uint64_t bits = (64 + 32) * 8;
  for (int i = 0; i < 8; ++i) blk[56 + i] = static_cast<unsigned char>((bits >> (8 * (7 - i))) & 0xff);
  sha_block(st.h, blk);
}
void state_to_bytes(const ShaState& st, unsigned char out[32]) {
  for (int i = 0; i < 8; ++i)
    for (int j = 0; j < 4; ++j) out[i * 4 + j] = static_cast<unsigned char>((st.h[i] >> (24 - 8 * j)) & 0xff);
}
}  // namespace

std::string pbkdf2_sha256(std::string_view password, std::string_view salt, std::uint32_t iterations,
                          std::size_t dk_len) {
  if (iterations == 0) iterations = 1;
  std::string k(password);
  if (k.size() > 64) k = sha256(k);
  k.resize(64, '\0');
  unsigned char ipad[64], opad[64];
  for (std::size_t i = 0; i < 64; ++i) {
    ipad[i] = static_cast<unsigned char>(k[i] ^ 0x36);
    opad[i] = static_cast<unsigned char>(k[i] ^ 0x5c);
  }
  ShaState inner0 = kShaInit, outer0 = kShaInit;  // midstates after the key blocks
  sha_block(inner0.h, ipad);
  sha_block(outer0.h, opad);

  std::string out;
  for (std::uint32_t block = 1; out.size() < dk_len; ++block) {
    // U1 = HMAC(P, S || INT(block)) -- the only step with a variable-length message.
    std::string s(salt);
    for (int i = 3; i >= 0; --i) s.push_back(static_cast<char>((block >> (8 * i)) & 0xff));
    std::string u1 = hmac_sha256(password, s);
    unsigned char u[32], t[32];
    std::memcpy(u, u1.data(), 32);
    std::memcpy(t, u, 32);
    for (std::uint32_t i = 1; i < iterations; ++i) {
      ShaState a = inner0;
      sha_tail32(a, u);
      unsigned char ia[32];
      state_to_bytes(a, ia);
      ShaState b = outer0;
      sha_tail32(b, ia);
      state_to_bytes(b, u);
      for (int j = 0; j < 32; ++j) t[j] = static_cast<unsigned char>(t[j] ^ u[j]);
    }
    out.append(reinterpret_cast<const char*>(t), 32);
  }
  out.resize(dk_len);
  return out;
}

std::string to_hex(std::string_view raw) {
  static const char hex[] = "0123456789abcdef";
  std::string out;
  out.reserve(raw.size() * 2);
  for (unsigned char c : raw) { out.push_back(hex[c >> 4]); out.push_back(hex[c & 15]); }
  return out;
}

std::string md5_hex(std::string_view data) { return to_hex(md5(data)); }

namespace {
constexpr char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
}

std::string base64_nopad_encode(std::string_view raw) {
  std::string out;
  std::size_t i = 0;
  for (; i + 2 < raw.size(); i += 3) {
    std::uint32_t v = std::uint32_t(static_cast<unsigned char>(raw[i])) << 16 |
                      std::uint32_t(static_cast<unsigned char>(raw[i + 1])) << 8 |
                      std::uint32_t(static_cast<unsigned char>(raw[i + 2]));
    out.push_back(kB64[(v >> 18) & 63]); out.push_back(kB64[(v >> 12) & 63]);
    out.push_back(kB64[(v >> 6) & 63]); out.push_back(kB64[v & 63]);
  }
  if (i + 1 == raw.size()) {
    std::uint32_t v = std::uint32_t(static_cast<unsigned char>(raw[i])) << 16;
    out.push_back(kB64[(v >> 18) & 63]); out.push_back(kB64[(v >> 12) & 63]);
  } else if (i + 2 == raw.size()) {
    std::uint32_t v = std::uint32_t(static_cast<unsigned char>(raw[i])) << 16 |
                      std::uint32_t(static_cast<unsigned char>(raw[i + 1])) << 8;
    out.push_back(kB64[(v >> 18) & 63]); out.push_back(kB64[(v >> 12) & 63]); out.push_back(kB64[(v >> 6) & 63]);
  }
  return out;
}

bool base64_nopad_decode(std::string_view text, std::string& out) {
  out.clear();
  if (text.size() % 4 == 1) return false;
  std::uint32_t acc = 0;
  int bits = 0;
  for (char c : text) {
    const char* p = std::strchr(kB64, c);
    if (c == '\0' || !p) return false;
    acc = (acc << 6) | static_cast<std::uint32_t>(p - kB64);
    bits += 6;
    if (bits >= 8) { bits -= 8; out.push_back(static_cast<char>((acc >> bits) & 0xff)); }
  }
  return true;
}

bool constant_time_equal(std::string_view a, std::string_view b) noexcept {
  unsigned char diff = a.size() == b.size() ? 0 : 1;
  const std::size_t n = a.size() < b.size() ? a.size() : b.size();
  for (std::size_t i = 0; i < n; ++i) diff = static_cast<unsigned char>(diff | (a[i] ^ b[i]));
  return diff == 0;
}

std::string random_bytes(std::size_t n) {
  static thread_local std::random_device rd;
  std::string out(n, '\0');
  for (std::size_t i = 0; i < n; ++i) out[i] = static_cast<char>(rd() & 0xff);
  return out;
}

}  // namespace gateway::crypto
