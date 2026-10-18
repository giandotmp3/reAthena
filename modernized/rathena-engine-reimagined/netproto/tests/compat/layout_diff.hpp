#pragma once
// Layout comparison between a private catalog (the gateway's wire table, the sim's table) and the netproto
// full catalog. A "disagreement" is anything a migration would have to decide: a different opcode, length,
// direction or gate, or a field at the same offset with a different kind/width.
#include <sstream>
#include <string>
#include <vector>

#include "netproto/spec.hpp"

namespace compat {
using namespace netproto;

inline std::string type_name(const FieldSpec& f) {
  static const char* n[] = {"u8", "u16", "u32", "i8", "i16", "i32", "i64", "string", "raw", "pos3", "pos6", "u64", "f32"};
  std::ostringstream o;
  o << n[static_cast<int>(f.type)];
  if (f.type == FieldType::FixedString || f.type == FieldType::Raw) o << "[" << f.width << "]";
  if (f.array_len > 1) o << "x" << f.array_len;
  return o.str();
}

/// Bytes a field occupies (tail = 0).
inline std::size_t extent(const FieldSpec& f) {
  switch (f.type) {
    case FieldType::U8: case FieldType::I8: return 1u * f.array_len;
    case FieldType::U16: case FieldType::I16: return 2u * f.array_len;
    case FieldType::U32: case FieldType::I32: case FieldType::F32: return 4u * f.array_len;
    case FieldType::U64: case FieldType::I64: return 8u * f.array_len;
    case FieldType::Pos3: return 3;
    case FieldType::Pos6: return 6;
    default: return f.width;
  }
}

inline bool same_kind(const FieldSpec& a, const FieldSpec& b) {
  // identical type, or an opaque byte run of the same size (u8[N] / raw[N] / string[N] are the same bytes)
  auto bytes_like = [](const FieldSpec& f) { return f.type == FieldType::Raw || f.type == FieldType::FixedString || (f.type == FieldType::U8 && f.array_len > 1); };
  const bool wide = a.type == FieldType::FixedString || a.type == FieldType::Raw;
  if (a.type == b.type && a.array_len == b.array_len && (!wide || a.width == b.width)) return true;
  return bytes_like(a) && bytes_like(b) && extent(a) == extent(b) && extent(a) > 0;
}

inline std::string key(const FieldSpec& f) { return (is_row_field(f) ? "row@" : "@") + std::to_string(f.offset); }

/// Compares the layout `mine` (private) with `theirs` (netproto); appends one line per disagreement.
inline void diff_version(const std::string& name, const PacketDef& dm, const PacketVersion& mine, const PacketDef& dt,
                         const PacketVersion& theirs, std::vector<std::string>& out) {
  auto add = [&](const std::string& s) { out.push_back(name + ": " + s); };
  if (mine.opcode != theirs.opcode) add("opcode differs");
  if (dm.direction != dt.direction) add("direction differs");
  if (dm.gate != dt.gate) add("gate differs (" + std::to_string(static_cast<int>(dm.gate)) + " vs " + std::to_string(static_cast<int>(dt.gate)) + ")");
  if (mine.length.kind != theirs.length.kind) { add("fixed/variable differs"); return; }
  if (mine.length.kind == LengthKind::Fixed) {
    if (mine.length.fixed_size != theirs.length.fixed_size)
      add("fixed length " + std::to_string(mine.length.fixed_size) + " vs " + std::to_string(theirs.length.fixed_size));
  } else {
    if (mine.length.prefix_offset != theirs.length.prefix_offset) add("length prefix offset differs");
    if (mine.length.header_size != theirs.length.header_size)
      add("header " + std::to_string(mine.length.header_size) + " vs " + std::to_string(theirs.length.header_size));
    if (mine.length.repeat.has_value() != theirs.length.repeat.has_value()) add("repeat group present on one side only");
    else if (mine.length.repeat && mine.length.repeat->stride != theirs.length.repeat->stride)
      add("row stride " + std::to_string(mine.length.repeat->stride) + " vs " + std::to_string(theirs.length.repeat->stride));
  }
  if (!mine.fields_known || !theirs.fields_known) {
    if (mine.fields_known != theirs.fields_known) add(mine.fields_known ? "netproto has no field layout" : "private table has no field layout");
    return;
  }
  for (const FieldSpec& f : mine.fields) {
    const FieldSpec* hit = nullptr;
    for (const FieldSpec& g : theirs.fields)
      if (g.offset == f.offset && is_row_field(g) == is_row_field(f) && g.bit_width == 0) { hit = &g; break; }
    if (!hit) {
      // a private field inside a larger netproto field (e.g. a u32 id the struct splits in two) is not an error
      bool covered = false;
      for (const FieldSpec& g : theirs.fields)
        if (is_row_field(g) == is_row_field(f) && g.offset <= f.offset && f.offset + extent(f) <= g.offset + extent(g) && extent(g) > 0) covered = true;
      if (!covered) add("private field '" + field_key(f) + "' " + type_name(f) + " " + key(f) + " has no netproto field");
      else add("private field '" + field_key(f) + "' " + key(f) + " lies inside a larger netproto field");
      continue;
    }
    if (!same_kind(f, *hit)) add("field " + key(f) + " private '" + field_key(f) + "' " + type_name(f) + " vs netproto '" + field_key(*hit) + "' " + type_name(*hit));
  }
}
}  // namespace compat
