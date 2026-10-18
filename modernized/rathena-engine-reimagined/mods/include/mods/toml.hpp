#pragma once
// TOML subset parser and writer for mod.toml, mods.lock and unit config files.
//
// Supported (TOML 1.0 semantics): comments, bare/quoted/dotted keys, tables
// `[a.b]`, arrays of tables `[[a]]`, inline tables, arrays (multi-line,
// trailing comma, mixed types), basic and literal strings (single and
// multi-line), integers (decimal with `_`, 0x/0o/0b), floats (incl. inf, nan),
// booleans.
// Rejected with a line:column error: date/time values (not needed by any
// manifest), duplicate keys, redefined tables, extension of inline tables or
// of tables created by dotted keys, unknown escapes, invalid UTF-8, control
// characters, integer overflow, nesting deeper than kMaxDepth, documents over
// kMaxBytes. Nothing here allocates unbounded memory relative to the input.
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mods::toml {

inline constexpr std::size_t kMaxBytes = 1u << 20;  // 1 MiB per document
inline constexpr unsigned kMaxDepth = 64;

// what() is "<source>:<line>:<column>: <message>" (source omitted when empty).
class ParseError : public std::runtime_error {
public:
  ParseError(std::string source, unsigned line, unsigned column, std::string message);
  unsigned line() const noexcept { return line_; }
  unsigned column() const noexcept { return column_; }
  const std::string& message() const noexcept { return message_; }
private:
  unsigned line_, column_;
  std::string message_;
};

struct Value {
  // Special members are defined in toml.cpp, where Value is complete (the
  // table member is a vector of pair<string, Value>, which strict standard
  // libraries refuse to instantiate for an incomplete Value).
  Value();
  ~Value();
  Value(const Value&);
  Value(Value&&) noexcept;
  Value& operator=(const Value&);
  Value& operator=(Value&&) noexcept;

  enum class Type { String, Integer, Float, Boolean, Array, Table };
  Type type = Type::Table;
  bool b = false;
  std::int64_t i = 0;
  double d = 0;
  std::string s;
  std::vector<Value> a;                           // Array
  std::vector<std::pair<std::string, Value>> t;   // Table, in document order
  unsigned line = 0, col = 0;                     // position of the value (1-based); 0 if synthetic

  // Parser bookkeeping (not part of equality).
  bool is_inline = false;      // inline table: closed to extension
  bool header_defined = false; // defined by a [header]
  bool dotted = false;         // created by a dotted key
  bool array_of_tables = false;

  static Value make_string(std::string v);
  static Value make_int(std::int64_t v);
  static Value make_bool(bool v);
  static Value make_table();
  static Value make_array();

  bool is_string() const { return type == Type::String; }
  bool is_int() const { return type == Type::Integer; }
  bool is_bool() const { return type == Type::Boolean; }
  bool is_array() const { return type == Type::Array; }
  bool is_table() const { return type == Type::Table; }
  const char* type_name() const;

  const Value* find(std::string_view key) const;
  Value* find(std::string_view key);
  // Appends (no duplicate check; for building values programmatically).
  Value& set(std::string key, Value v);
};

// Structural equality: tables compare as unordered key sets, NaN == NaN.
bool operator==(const Value& x, const Value& y);
inline bool operator!=(const Value& x, const Value& y) { return !(x == y); }

// Parses a whole document; the result is a Table. Throws ParseError.
Value parse(std::string_view text, const std::string& source = {});

// Canonical TOML text for a table. parse(write(v)) == v for every v that
// parse() can return.
std::string write(const Value& root);

}  // namespace mods::toml
