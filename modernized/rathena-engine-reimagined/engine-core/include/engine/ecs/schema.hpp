#pragma once
#include <cstdint>
#include <map>
#include <optional>
#include <unordered_map>
#include <string>
#include <variant>
#include <vector>

#include "engine/ecs/entity.hpp"

namespace engine {

// Script-declared component schemas: typed fields registered at load time.
// Ids must be namespaced ("<namespace>:<name>", both parts non-empty).
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
  // Drops every stored value of the entity (call when it is destroyed).
  void remove_entity(EntityId e);
  std::optional<SchemaId> find(const std::string& namespaced_id) const;

private:
  struct Schema {
    std::string id;
    std::vector<FieldDef> fields;
  };
  const FieldDef& field_def(SchemaId s, const std::string& field) const;
  static uint64_t key(EntityId e) { return (uint64_t(e.generation) << 32) | e.index; }

  std::vector<Schema> schemas_;
  std::unordered_map<std::string, SchemaId> by_name_;
  // (entity key, schema) -> field -> value. Keyed by generation too, so a
  // stale handle can never read or write a later entity's values.
  std::map<std::pair<uint64_t, SchemaId>, std::unordered_map<std::string, FieldValue>> values_;
};

}  // namespace engine
