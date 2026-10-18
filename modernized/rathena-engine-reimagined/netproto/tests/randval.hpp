#pragma once
// Random field values valid for a FieldSpec, for the property tests. Seeded generators: failures reproduce.
#include <limits>
#include <random>

#include "netproto/codec_set.hpp"

namespace fx {
using namespace netproto;

// Random values for every field kind. Seeded: failures reproduce.
inline FieldValue random_value(std::mt19937_64& g, const FieldSpec& f) {
  auto ri = [&](unsigned bits_) -> std::int64_t {
    if (g() % 6 == 0) {  // boundary values
      const bool w64 = bits_ == 64;
      const std::int64_t pick[] = {0, 1, -1, w64 ? std::numeric_limits<std::int64_t>::max() : (std::int64_t{1} << (bits_ - 1)) - 1,
                                   w64 ? std::numeric_limits<std::int64_t>::min() : -(std::int64_t{1} << (bits_ - 1))};
      return pick[g() % 5] & (w64 ? std::int64_t{-1} : (std::int64_t{1} << bits_) - 1);
    }
    return static_cast<std::int64_t>(g() & (bits_ == 64 ? ~0ULL : ((1ULL << bits_) - 1)));
  };
  auto canon = [&](std::int64_t v, FieldType t) -> std::int64_t {
    switch (t) {
      case FieldType::I8: return static_cast<std::int8_t>(v);
      case FieldType::I16: return static_cast<std::int16_t>(v);
      case FieldType::I32: return static_cast<std::int32_t>(v);
      case FieldType::U8: return static_cast<std::uint8_t>(v);
      case FieldType::U16: return static_cast<std::uint16_t>(v);
      case FieldType::U32: return static_cast<std::uint32_t>(v);
      default: return v;
    }
  };
  auto width_bits = [&](FieldType t) -> unsigned {
    switch (t) {
      case FieldType::U8: case FieldType::I8: return 8;
      case FieldType::U16: case FieldType::I16: return 16;
      case FieldType::U32: case FieldType::I32: return 32;
      default: return 64;
    }
  };
  switch (f.type) {
    case FieldType::FixedString: {
      std::string s;
      const std::size_t cap = f.width ? f.width - 1 : 20;
      const std::size_t n = g() % (std::min<std::size_t>(cap, 20) + 1);
      for (std::size_t i = 0; i < n; ++i) s += static_cast<char>(1 + g() % 126);
      return s;
    }
    case FieldType::Raw: {
      Bytes b(f.width ? f.width : g() % 20);
      for (auto& x : b) x = static_cast<std::uint8_t>(g());
      return b;
    }
    case FieldType::Pos3: return Pos3{static_cast<std::uint16_t>(g() % 1024), static_cast<std::uint16_t>(g() % 1024), static_cast<std::uint8_t>(g() % 16)};
    case FieldType::Pos6:
      return Pos6{static_cast<std::uint16_t>(g() % 1024), static_cast<std::uint16_t>(g() % 1024), static_cast<std::uint16_t>(g() % 1024),
                  static_cast<std::uint16_t>(g() % 1024), static_cast<std::uint8_t>(g() % 16), static_cast<std::uint8_t>(g() % 16)};
    case FieldType::F32: return static_cast<double>(static_cast<float>(static_cast<std::int32_t>(g() % 20000) - 10000) / 8.0f);
    default: break;
  }
  if (f.bit_width) return static_cast<std::int64_t>(g() & ((1ULL << f.bit_width) - 1));
  const unsigned wb = width_bits(f.type);
  if (f.array_len == 1) return canon(ri(wb), f.type);
  if (wb == 8) {
    Bytes b(f.array_len);
    for (auto& x : b) x = static_cast<std::uint8_t>(g());
    return b;
  }
  std::vector<std::int64_t> v;
  for (std::size_t i = 0; i < f.array_len; ++i) v.push_back(canon(ri(wb), f.type));
  return v;
}
}  // namespace fx
