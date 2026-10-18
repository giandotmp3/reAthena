#pragma once
// Compact, generated representation of a packet catalog: tools/codegen/netproto_codegen.py emits
// arrays of these rows (one row per packet VERSION) instead of thousands of lines of builder code.
// Rows of one packet are consecutive and share name/direction/category/gate.
#include <cstdint>
#include <span>

#include "netproto/spec.hpp"

namespace netproto::generated {

struct FieldRow {
  const char* name;
  std::uint16_t offset;
  std::uint8_t type;      // FieldType
  std::uint16_t width;
  std::uint16_t array_len;
  std::uint8_t row;       // 1 = repeat-group field
  std::uint8_t bit_shift;
  std::uint8_t bit_width;
  const char* present_when;  // "" = always
};

struct PacketRow {
  const char* name;
  std::uint8_t direction;  // Direction
  std::uint8_t gate;       // Phase
  const char* category;
  const char* applies;
  const char* source;
  std::uint16_t opcode;
  std::uint8_t variable;      // 0 fixed, 1 variable
  std::uint8_t fields_known;
  std::uint16_t fixed_size;
  std::uint16_t prefix_offset;
  std::uint16_t header_size;
  std::uint16_t repeat_offset;
  std::uint16_t repeat_stride;  // 0 = no repeat group
  std::uint32_t field_first, field_count;
  std::uint32_t offset_first, offset_count;  // handler offsets
};



// The generator writes FieldType / Direction / Phase as numbers; keep them tied to the enums.
static_assert(static_cast<int>(FieldType::U8) == 0 && static_cast<int>(FieldType::U16) == 1 &&
              static_cast<int>(FieldType::U32) == 2 && static_cast<int>(FieldType::I8) == 3 &&
              static_cast<int>(FieldType::I16) == 4 && static_cast<int>(FieldType::I32) == 5 &&
              static_cast<int>(FieldType::I64) == 6 && static_cast<int>(FieldType::FixedString) == 7 &&
              static_cast<int>(FieldType::Raw) == 8 && static_cast<int>(FieldType::Pos3) == 9 &&
              static_cast<int>(FieldType::Pos6) == 10 && static_cast<int>(FieldType::U64) == 11 &&
              static_cast<int>(FieldType::F32) == 12);
static_assert(static_cast<int>(Direction::ClientToServer) == 0 && static_cast<int>(Direction::ServerToClient) == 1 &&
              static_cast<int>(Direction::Inter) == 2 && static_cast<int>(Direction::Any) == 3);
static_assert(static_cast<int>(Phase::PreSession) == 0 && static_cast<int>(Phase::PreMapLoad) == 1 &&
              static_cast<int>(Phase::InWorld) == 2);

[[nodiscard]] inline PacketCatalog build_catalog(std::span<const PacketRow> packets, std::span<const FieldRow> fields,
                            std::span<const std::uint16_t> handler_offsets) {
  PacketCatalog c;
  PacketDef cur;
  bool open = false;
  for (const PacketRow& r : packets) {
    if (open && cur.name != r.name) {
      c.add(std::move(cur));
      cur = PacketDef{};
      open = false;
    }
    if (!open) {
      cur.name = r.name;
      cur.direction = static_cast<Direction>(r.direction);
      cur.category = r.category;
      cur.gate = static_cast<Phase>(r.gate);
      open = true;
    }
    PacketVersion v;
    v.applies = *VersionPredicate::parse(r.applies);
    v.opcode = r.opcode;
    v.source = r.source;
    if (r.variable) {
      v.length.kind = LengthKind::Variable;
      v.length.prefix_offset = r.prefix_offset;
      v.length.header_size = r.header_size;
      if (r.repeat_stride) v.length.repeat = RepeatGroup{r.repeat_offset, r.repeat_stride};
    } else {
      v.length.kind = LengthKind::Fixed;
      v.length.fixed_size = r.fixed_size;
    }
    v.fields_known = r.fields_known != 0;
    for (std::uint32_t i = 0; i < r.offset_count; ++i) v.handler_offsets.push_back(handler_offsets[r.offset_first + i]);
    for (std::uint32_t i = 0; i < r.field_count; ++i) {
      const FieldRow& f = fields[r.field_first + i];
      FieldSpec s;
      s.name = f.name;
      s.offset = f.offset;
      s.type = static_cast<FieldType>(f.type);
      s.width = f.width;
      s.array_len = f.array_len;
      s.row = f.row != 0;
      s.bit_shift = f.bit_shift;
      s.bit_width = f.bit_width;
      if (f.present_when[0]) s.present_when = *VersionPredicate::parse(f.present_when);
      v.fields.push_back(std::move(s));
    }
    cur.versions.push_back(std::move(v));
  }
  if (open) c.add(std::move(cur));
  return c;
}


}  // namespace netproto::generated
