#pragma once
// Dynamic value used on the scenario RPC channel between the Luau `sim` library and the host,
// and for machine-readable reports. Text encoding (binary safe, self-delimiting):
//   N            nil
//   T / F        booleans
//   n<num>;      number (%.17g)
//   s<len>:<bytes>
//   a<count>:    followed by <count> values
//   m<count>:    followed by <count> (string key, value) pairs
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace tooling::sim {

struct Val;
using VArr = std::vector<Val>;
using VMap = std::map<std::string, Val>;

struct Val {
  // A tagged struct rather than std::variant: GCC 13 at -O2 reports -Wmaybe-uninitialized inside
  // std::variant's move constructor for a variant that holds vectors and maps of itself.
  enum class Kind : uint8_t { Nil, Bool, Num, Str, Arr, Map };
  Kind kind = Kind::Nil;
  bool b = false;
  double d = 0;
  std::string s;
  VArr a;
  VMap m;

  Val() = default;
  Val(std::nullptr_t) {}
  Val(bool x) : kind(Kind::Bool), b(x) {}
  Val(int x) : kind(Kind::Num), d(static_cast<double>(x)) {}
  Val(int64_t x) : kind(Kind::Num), d(static_cast<double>(x)) {}
  Val(uint64_t x) : kind(Kind::Num), d(static_cast<double>(x)) {}
  Val(double x) : kind(Kind::Num), d(x) {}
  Val(const char* x) : kind(Kind::Str), s(x) {}
  Val(std::string x) : kind(Kind::Str), s(std::move(x)) {}
  Val(VArr x) : kind(Kind::Arr), a(std::move(x)) {}
  Val(VMap x) : kind(Kind::Map), m(std::move(x)) {}

  bool is_nil() const { return kind == Kind::Nil; }
  bool is_bool() const { return kind == Kind::Bool; }
  bool is_str() const { return kind == Kind::Str; }
  bool is_num() const { return kind == Kind::Num; }
  bool is_arr() const { return kind == Kind::Arr; }
  bool is_map() const { return kind == Kind::Map; }
  bool truthy() const;
  // Typed accessors throw ValueError with the offending type.
  const std::string& str() const;
  double num() const;
  int64_t i64() const { return static_cast<int64_t>(num()); }
  bool boolean() const;
  const VArr& arr() const;
  const VMap& map() const;
  VArr& arr_mut();
  VMap& map_mut();
  // Map field lookup; nullptr when this is not a map or the key is absent.
  const Val* get(const std::string& key) const;
  std::string str_or(const std::string& key, const std::string& dflt) const;
  double num_or(const std::string& key, double dflt) const;
  bool bool_or(const std::string& key, bool dflt) const;
};

struct ValueError : std::runtime_error {
  using std::runtime_error::runtime_error;
};

std::string encode(const Val& v);
// Decodes one value at `pos` (advanced past it). Throws ValueError on malformed input.
Val decode(std::string_view text, size_t& pos);
Val decode_all(std::string_view text);

// Cuts at a character boundary (never in the middle of a UTF-8 sequence).
std::string utf8_truncate(const std::string& s, size_t max_bytes);

// JSON text for reports (strings escaped, numbers integral when exact). Object keys sorted.
std::string to_json(const Val& v, int indent = 0);

}  // namespace tooling::sim
