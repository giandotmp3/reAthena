#pragma once
#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <string_view>

#include "legacy_npc/types.hpp"

namespace legacy_npc {

enum class Scope {
  Character,       // name        -> character, persistent
  CharacterTemp,   // @name       -> character, temp
  World,           // $name       -> world, persistent
  WorldTemp,       // $@name      -> world, temp
  Account,         // #name       -> account
  AccountGlobal,   // ##name      -> account, login-wide
  Npc,             // .name       -> NPC object table
  CallFrame,       // .@name      -> Luau locals per call frame
  Instance         // 'name       -> instance table
};

inline constexpr std::size_t kMaxKeyLen = 32;          // legacy: key < 33 chars
inline constexpr std::size_t kMaxStringValueLen = 254; // legacy: value < 255

struct VarRef {
  Scope scope = Scope::Character;
  std::string name;  // without sigil and without string suffix
  bool is_string = false;
  std::optional<std::int32_t> index;
};

std::string_view scope_name(Scope s);
// nullopt when the text is not a valid legacy variable reference.
std::optional<VarRef> parse_var(std::string_view text);
// Namespaced storage key under mod id "legacy": "legacy:<scope>/<name>[$]".
std::string storage_key(const VarRef& v);

// Sparse array: storing 0 or "" deletes the element; missing reads as 0 / "".
class SparseArray {
 public:
  void set(std::int32_t idx, const Value& v);
  std::int64_t get_int(std::int32_t idx) const;
  std::string get_str(std::int32_t idx) const;
  std::size_t stored_count() const { return items_.size(); }

 private:
  std::map<std::int32_t, Value> items_;
};

class VariableStore {
 public:
  virtual ~VariableStore() = default;
  virtual void set(const VarRef& v, const Value& val) = 0;
  virtual Value get(const VarRef& v) const = 0;
};

class InMemoryVariableStore : public VariableStore {
 public:
  void set(const VarRef& v, const Value& val) override;
  Value get(const VarRef& v) const override;
  std::size_t stored_count() const;

 private:
  std::map<std::string, SparseArray> arrays_;  // keyed by storage_key
};

// Param variables (Zeny, BaseLevel...) are not variables: shim over attributes.
bool is_param_variable(std::string_view name);
std::string param_attribute_id(std::string_view name);  // "legacy:param/<lower>"

class AttributeBackend {
 public:
  virtual ~AttributeBackend() = default;
  virtual std::int64_t get(const std::string& attribute_id) const = 0;
  virtual void set(const std::string& attribute_id, std::int64_t value) = 0;
};

class ParamShim {
 public:
  explicit ParamShim(AttributeBackend& b) : backend_(b) {}
  std::int64_t read(std::string_view name) const;
  void write(std::string_view name, std::int64_t v);

 private:
  AttributeBackend& backend_;
};

}  // namespace legacy_npc
