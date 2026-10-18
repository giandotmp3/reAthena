#pragma once
// Private helpers shared by codec_set.cpp and codec.cpp.
#include <cstddef>
#include <cstdint>

#include "netproto/spec.hpp"

namespace netproto::detail {

inline bool is_int(FieldType t) {
  switch (t) {
    case FieldType::U8: case FieldType::U16: case FieldType::U32: case FieldType::U64:
    case FieldType::I8: case FieldType::I16: case FieldType::I32: case FieldType::I64: return true;
    default: return false;
  }
}
inline bool is_float(FieldType t) { return t == FieldType::F32; }
inline bool is_signed(FieldType t) {
  return t == FieldType::I8 || t == FieldType::I16 || t == FieldType::I32 || t == FieldType::I64;
}

/// Size of one element (one array slot) in bytes; for FixedString/Raw the whole field (0 for a tail).
inline std::size_t elem_size(const FieldSpec& f) {
  switch (f.type) {
    case FieldType::U8: case FieldType::I8: return 1;
    case FieldType::U16: case FieldType::I16: return 2;
    case FieldType::U32: case FieldType::I32: case FieldType::F32: return 4;
    case FieldType::U64: case FieldType::I64: return 8;
    case FieldType::Pos3: return 3;
    case FieldType::Pos6: return 6;
    case FieldType::FixedString: case FieldType::Raw: return f.width;
  }
  return 0;
}
/// Total bytes of the field (elements x array_len).
inline std::size_t field_bytes(const FieldSpec& f) {
  if (f.type == FieldType::FixedString || f.type == FieldType::Raw) return f.width;
  return elem_size(f) * (f.array_len ? f.array_len : 1);
}

}  // namespace netproto::detail
