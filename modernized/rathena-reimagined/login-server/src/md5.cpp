// RFC 1321 MD5 (wire compatibility only; see md5.hpp).
#include "rathena/login/md5.hpp"

#include <cstring>
#include <vector>

namespace rathena::login {
namespace {

constexpr std::uint32_t kK[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
    0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
    0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
    0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
    0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
    0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
constexpr int kS[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
                        5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
                        4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                        6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};

constexpr std::uint32_t rotl(std::uint32_t x, int c) {
  return (x << c) | (x >> (32 - c));
}

}  // namespace

Md5Digest md5(std::span<const std::uint8_t> data) {
  std::uint32_t a0 = 0x67452301, b0 = 0xefcdab89, c0 = 0x98badcfe, d0 = 0x10325476;
  std::vector<std::uint8_t> msg(data.begin(), data.end());
  const std::uint64_t bit_len = static_cast<std::uint64_t>(data.size()) * 8u;
  msg.push_back(0x80);
  while (msg.size() % 64 != 56) msg.push_back(0);
  for (int i = 0; i < 8; ++i) msg.push_back(static_cast<std::uint8_t>(bit_len >> (8 * i)));

  for (std::size_t off = 0; off < msg.size(); off += 64) {
    std::uint32_t m[16];
    for (int i = 0; i < 16; ++i) {
      const std::size_t p = off + static_cast<std::size_t>(i) * 4;
      m[i] = std::uint32_t{msg[p]} | (std::uint32_t{msg[p + 1]} << 8) |
             (std::uint32_t{msg[p + 2]} << 16) | (std::uint32_t{msg[p + 3]} << 24);
    }
    std::uint32_t a = a0, b = b0, c = c0, d = d0;
    for (int i = 0; i < 64; ++i) {
      std::uint32_t f;
      int g;
      if (i < 16) { f = (b & c) | (~b & d); g = i; }
      else if (i < 32) { f = (d & b) | (~d & c); g = (5 * i + 1) % 16; }
      else if (i < 48) { f = b ^ c ^ d; g = (3 * i + 5) % 16; }
      else { f = c ^ (b | ~d); g = (7 * i) % 16; }
      f = f + a + kK[i] + m[g];
      a = d;
      d = c;
      c = b;
      b = b + rotl(f, kS[i]);
    }
    a0 += a; b0 += b; c0 += c; d0 += d;
  }
  Md5Digest out{};
  const std::uint32_t words[4] = {a0, b0, c0, d0};
  for (int w = 0; w < 4; ++w)
    for (int i = 0; i < 4; ++i)
      out[static_cast<std::size_t>(w * 4 + i)] = static_cast<std::uint8_t>(words[w] >> (8 * i));
  return out;
}

Md5Digest md5(std::string_view data) {
  return md5(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(data.data()), data.size()));
}

std::string to_hex(const Md5Digest& d) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string s;
  s.reserve(32);
  for (auto b : d) {
    s.push_back(kHex[b >> 4]);
    s.push_back(kHex[b & 0xF]);
  }
  return s;
}

}  // namespace rathena::login
