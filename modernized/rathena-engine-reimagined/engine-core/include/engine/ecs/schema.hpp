#pragma once
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include "engine/ecs/entity.hpp"

namespace engine {

// Script-declared component schemas: typed fields registered at load time.
// STUB (EC-ECS-005).
enum class FieldType : uint8_t { Int, Double, String, Bool };
using FieldValue = std::variant<int64_t, double, std::string, bool>;

struct FieldDef {
  std::string name;
  FieldType type;
};

using SchemaId = uint32_t;

class SchemaStore {
public:
  // Registers (or re-registers an identical) schema keyed by a namespaced id.
  // Re-registering with a different shape is an error.
  SchemaId register_schema(const std::string& namespaced_id,
                           std::vector<FieldDef> fields);
  void set(EntityId e, SchemaId s, const std::string& field, FieldValue v);
  // Throws on type mismatch; returns the default for unset fields.
  FieldValue get(EntityId e, SchemaId s, const std::string& field) const;
};

}  // namespace engine
