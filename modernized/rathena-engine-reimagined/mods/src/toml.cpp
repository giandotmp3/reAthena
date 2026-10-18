#include "mods/toml.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace mods::toml {

ParseError::ParseError(std::string source, unsigned line, unsigned column, std::string message)
    : std::runtime_error((source.empty() ? std::string() : source + ":") + std::to_string(line) +
                         ":" + std::to_string(column) + ": " + message),
      line_(line), column_(column), message_(std::move(message)) {}

// ---- Value helpers ----------------------------------------------------------
Value::Value() = default;
Value::~Value() = default;
Value::Value(const Value&) = default;
Value::Value(Value&&) noexcept = default;
Value& Value::operator=(const Value&) = default;
Value& Value::operator=(Value&&) noexcept = default;

Value Value::make_string(std::string v) { Value x; x.type = Type::String; x.s = std::move(v); return x; }
Value Value::make_int(std::int64_t v) { Value x; x.type = Type::Integer; x.i = v; return x; }
Value Value::make_bool(bool v) { Value x; x.type = Type::Boolean; x.b = v; return x; }
Value Value::make_table() { Value x; x.type = Type::Table; return x; }
Value Value::make_array() { Value x; x.type = Type::Array; return x; }

const char* Value::type_name() const {
  switch (type) {
    case Type::String: return "string";
    case Type::Integer: return "integer";
    case Type::Float: return "float";
    case Type::Boolean: return "boolean";
    case Type::Array: return "array";
    case Type::Table: return "table";
  }
  return "?";
}
const Value* Value::find(std::string_view key) const {
  for (const auto& kv : t) if (kv.first == key) return &kv.second;
  return nullptr;
}
Value* Value::find(std::string_view key) {
  for (auto& kv : t) if (kv.first == key) return &kv.second;
  return nullptr;
}
Value& Value::set(std::string key, Value v) {
  t.emplace_back(std::move(key), std::move(v));
  return t.back().second;
}

bool operator==(const Value& x, const Value& y) {
  if (x.type != y.type) return false;
  switch (x.type) {
    case Value::Type::String: return x.s == y.s;
    case Value::Type::Integer: return x.i == y.i;
    case Value::Type::Boolean: return x.b == y.b;
    case Value::Type::Float: return x.d == y.d || (std::isnan(x.d) && std::isnan(y.d));
    case Value::Type::Array:
      if (x.a.size() != y.a.size()) return false;
      for (std::size_t k = 0; k < x.a.size(); ++k) if (!(x.a[k] == y.a[k])) return false;
      return true;
    case Value::Type::Table:
      if (x.t.size() != y.t.size()) return false;
      for (const auto& kv : x.t) {
        const Value* o = y.find(kv.first);
        if (!o || !(kv.second == *o)) return false;
      }
      return true;
  }
  return false;
}

namespace {

bool is_bare(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
}
bool is_digit(char c) { return c >= '0' && c <= '9'; }
int hexval(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

void append_utf8(std::string& out, std::uint32_t cp) {
  if (cp < 0x80) out += static_cast<char>(cp);
  else if (cp < 0x800) {
    out += static_cast<char>(0xC0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    out += static_cast<char>(0xE0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (cp >> 18));
    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  }
}

class Parser {
public:
  Parser(std::string_view text, std::string source) : s_(text), source_(std::move(source)) {
    line_starts_.push_back(0);
    for (std::size_t k = 0; k < s_.size(); ++k)
      if (s_[k] == '\n') line_starts_.push_back(k + 1);
  }

  Value run() {
    if (s_.size() > kMaxBytes) fail(0, "document is larger than " + std::to_string(kMaxBytes) + " bytes");
    validate_bytes();
    if (s_.size() >= 3 && s_.substr(0, 3) == "\xEF\xBB\xBF") p_ = 3;
    Value root = Value::make_table();
    root.line = 1; root.col = 1;
    Value* cur = &root;
    while (true) {
      skip_ws();
      if (eof()) break;
      char c = s_[p_];
      if (c == '#') { skip_comment(); continue; }
      if (c == '\n') { ++p_; continue; }
      if (c == '\r') { p_ += 2; continue; }  // validated: always \r\n
      if (c == '[') {
        cur = parse_header(root);
      } else {
        auto keys = parse_key_path();
        skip_ws();
        if (eof() || s_[p_] != '=') fail(p_, "expected '=' after key");
        ++p_;
        skip_ws();
        if (eof() || s_[p_] == '\n' || s_[p_] == '\r') fail(p_, "expected a value after '='");
        Value v = parse_value(0);
        insert_kv(*cur, keys, std::move(v));
      }
      end_of_line();
    }
    return root;
  }

private:
  using KeyPath = std::vector<std::pair<std::string, std::size_t>>;

  std::string_view s_;
  std::string source_;
  std::size_t p_ = 0;
  std::vector<std::size_t> line_starts_;

  bool eof() const { return p_ >= s_.size(); }

  unsigned line_of(std::size_t off) const {
    auto it = std::upper_bound(line_starts_.begin(), line_starts_.end(), off);
    return static_cast<unsigned>(it - line_starts_.begin());
  }
  unsigned col_of(std::size_t off) const {
    return static_cast<unsigned>(off - line_starts_[line_of(off) - 1] + 1);
  }
  [[noreturn]] void fail(std::size_t off, const std::string& m) const {
    if (off > s_.size()) off = s_.size();
    throw ParseError(source_, line_of(off), col_of(off), m);
  }
  void stamp(Value& v, std::size_t off) const { v.line = line_of(off); v.col = col_of(off); }

  // Whole-document byte checks: UTF-8 well-formedness, no control characters
  // (except tab and newline), CR only as part of CRLF.
  void validate_bytes() const {
    std::size_t k = 0;
    while (k < s_.size()) {
      unsigned char c = static_cast<unsigned char>(s_[k]);
      if (c < 0x80) {
        if (c == '\r') {
          if (k + 1 >= s_.size() || s_[k + 1] != '\n') fail(k, "bare carriage return (only CRLF line endings are allowed)");
        } else if ((c < 0x20 && c != '\t' && c != '\n') || c == 0x7F) {
          char buf[48];
          std::snprintf(buf, sizeof buf, "control character 0x%02X is not allowed", c);
          fail(k, buf);
        }
        ++k;
        continue;
      }
      int len = (c >= 0xC2 && c <= 0xDF) ? 2 : (c >= 0xE0 && c <= 0xEF) ? 3 : (c >= 0xF0 && c <= 0xF4) ? 4 : 0;
      if (!len || k + static_cast<std::size_t>(len) > s_.size()) fail(k, "invalid UTF-8");
      std::uint32_t cp = len == 2 ? (c & 0x1Fu) : len == 3 ? (c & 0x0Fu) : (c & 0x07u);
      for (int m = 1; m < len; ++m) {
        unsigned char cc = static_cast<unsigned char>(s_[k + static_cast<std::size_t>(m)]);
        if ((cc & 0xC0) != 0x80) fail(k, "invalid UTF-8");
        cp = (cp << 6) | (cc & 0x3Fu);
      }
      if ((len == 3 && cp < 0x800) || (len == 4 && cp < 0x10000) || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
        fail(k, "invalid UTF-8");
      k += static_cast<std::size_t>(len);
    }
  }

  void skip_ws() { while (!eof() && (s_[p_] == ' ' || s_[p_] == '\t')) ++p_; }
  void skip_comment() { while (!eof() && s_[p_] != '\n') ++p_; }
  // After a statement: optional comment, then newline or EOF.
  void end_of_line() {
    skip_ws();
    if (eof()) return;
    if (s_[p_] == '#') { skip_comment(); }
    if (eof()) return;
    if (s_[p_] == '\n') { ++p_; return; }
    if (s_[p_] == '\r') { p_ += 2; return; }
    fail(p_, "expected end of line after value, found '" + std::string(1, s_[p_]) + "'");
  }

  // ---- keys ---------------------------------------------------------------
  std::string parse_one_key(std::size_t& at) {
    at = p_;
    if (eof()) fail(p_, "expected a key");
    char c = s_[p_];
    if (c == '"') return parse_basic_string(false);
    if (c == '\'') return parse_literal_string(false);
    std::size_t b = p_;
    while (!eof() && is_bare(s_[p_])) ++p_;
    if (p_ == b) fail(p_, std::string("expected a key, found '") + c + "'");
    return std::string(s_.substr(b, p_ - b));
  }
  KeyPath parse_key_path() {
    KeyPath path;
    while (true) {
      skip_ws();
      std::size_t at;
      std::string k = parse_one_key(at);
      path.emplace_back(std::move(k), at);
      skip_ws();
      if (!eof() && s_[p_] == '.') { ++p_; continue; }
      break;
    }
    return path;
  }
  static std::string path_str(const KeyPath& kp, std::size_t upto) {
    std::string r;
    for (std::size_t k = 0; k < upto; ++k) { if (k) r += '.'; r += kp[k].first; }
    return r;
  }

  // ---- tables -------------------------------------------------------------
  void insert_kv(Value& tbl, const KeyPath& keys, Value val) {
    Value* t = &tbl;
    for (std::size_t k = 0; k + 1 < keys.size(); ++k) {
      Value* child = t->find(keys[k].first);
      if (!child) {
        Value nt = Value::make_table();
        nt.dotted = true;
        stamp(nt, keys[k].second);
        child = &t->set(keys[k].first, std::move(nt));
      } else if (!child->is_table() || child->is_inline || !child->dotted) {
        fail(keys[k].second, "key '" + path_str(keys, k + 1) + "' is already defined as " +
                                 (child->is_table() && !child->is_inline ? "a table header" : std::string("a ") + child->type_name() +
                                     (child->is_inline ? " (inline tables cannot be extended)" : "")) +
                                 " at line " + std::to_string(child->line));
      }
      t = child;
    }
    const auto& last = keys.back();
    if (const Value* ex = t->find(last.first))
      fail(last.second, "duplicate key '" + path_str(keys, keys.size()) + "' (first defined at line " + std::to_string(ex->line) + ")");
    t->set(last.first, std::move(val));
  }

  Value* parse_header(Value& root) {
    std::size_t start = p_;
    bool aot = p_ + 1 < s_.size() && s_[p_ + 1] == '[';
    p_ += aot ? 2 : 1;
    KeyPath keys = parse_key_path();
    skip_ws();
    if (aot) {
      if (p_ + 1 >= s_.size() || s_[p_] != ']' || s_[p_ + 1] != ']') fail(p_, "expected ']]' to close the array-of-tables header");
      p_ += 2;
    } else {
      if (eof() || s_[p_] != ']') fail(p_, "expected ']' to close the table header");
      ++p_;
    }
    Value* t = &root;
    for (std::size_t k = 0; k + 1 < keys.size(); ++k) {
      Value* child = t->find(keys[k].first);
      if (!child) {
        Value nt = Value::make_table();
        stamp(nt, keys[k].second);
        child = &t->set(keys[k].first, std::move(nt));
      } else if (child->is_array() && child->array_of_tables && !child->a.empty()) {
        child = &child->a.back();
      } else if (!child->is_table()) {
        fail(keys[k].second, "key '" + path_str(keys, k + 1) + "' is a " + child->type_name() + ", not a table");
      } else if (child->is_inline) {
        fail(keys[k].second, "cannot extend inline table '" + path_str(keys, k + 1) + "'");
      } else if (child->dotted) {
        fail(keys[k].second, "cannot extend table '" + path_str(keys, k + 1) + "' that was defined with dotted keys");
      }
      t = child;
    }
    const auto& last = keys.back();
    Value* child = t->find(last.first);
    if (!aot) {
      if (!child) {
        Value nt = Value::make_table();
        nt.header_defined = true;
        stamp(nt, start);
        return &t->set(last.first, std::move(nt));
      }
      if (!child->is_table() || child->is_inline || child->dotted || child->header_defined)
        fail(last.second, "table '" + path_str(keys, keys.size()) + "' is already defined (first at line " + std::to_string(child->line) + ")");
      child->header_defined = true;  // implicit parent now defined explicitly
      return child;
    }
    if (!child) {
      Value arr = Value::make_array();
      arr.array_of_tables = true;
      stamp(arr, start);
      child = &t->set(last.first, std::move(arr));
    } else if (!child->is_array() || !child->array_of_tables) {
      fail(last.second, "key '" + path_str(keys, keys.size()) + "' is already defined as " + child->type_name() + " at line " + std::to_string(child->line));
    }
    Value nt = Value::make_table();
    nt.header_defined = true;
    stamp(nt, start);
    child->a.push_back(std::move(nt));
    return &child->a.back();
  }

  // ---- strings ------------------------------------------------------------
  // Positioned at the opening quote. `multi` selects the """ form.
  std::string parse_basic_string(bool multi) {
    std::size_t open = p_;
    p_ += multi ? 3 : 1;
    std::string out;
    if (multi) {
      if (!eof() && s_[p_] == '\n') ++p_;
      else if (p_ + 1 < s_.size() && s_[p_] == '\r' && s_[p_ + 1] == '\n') p_ += 2;
    }
    while (true) {
      if (eof()) fail(open, "unterminated string");
      char c = s_[p_];
      if (c == '"') {
        if (!multi) { ++p_; return out; }
        std::size_t n = 0;
        while (p_ + n < s_.size() && s_[p_ + n] == '"') ++n;
        if (n >= 3) {
          if (n > 5) fail(p_ + 5, "too many quotes at the end of a multi-line string");
          out.append(n - 3, '"');
          p_ += n;
          return out;
        }
        out.append(n, '"');
        p_ += n;
        continue;
      }
      if (c == '\n' || c == '\r') {
        if (!multi) fail(p_, "newline in a single-line string");
        if (c == '\r') { ++p_; }  // CRLF -> LF
        out += '\n';
        ++p_;
        continue;
      }
      if (c != '\\') { out += c; ++p_; continue; }
      // escape
      std::size_t esc = p_;
      ++p_;
      if (eof()) fail(esc, "unterminated escape sequence");
      char e = s_[p_++];
      switch (e) {
        case 'b': out += '\b'; break;
        case 't': out += '\t'; break;
        case 'n': out += '\n'; break;
        case 'f': out += '\f'; break;
        case 'r': out += '\r'; break;
        case '"': out += '"'; break;
        case '\\': out += '\\'; break;
        case 'u':
        case 'U': {
          int digits = e == 'u' ? 4 : 8;
          std::uint32_t cp = 0;
          for (int d = 0; d < digits; ++d) {
            if (eof() || hexval(s_[p_]) < 0) fail(esc, std::string("\\") + e + " escape needs " + std::to_string(digits) + " hex digits");
            cp = (cp << 4) | static_cast<std::uint32_t>(hexval(s_[p_++]));
          }
          if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) fail(esc, "escape is not a Unicode scalar value");
          append_utf8(out, cp);
          break;
        }
        case ' ': case '\t': case '\n': case '\r': {
          if (!multi) fail(esc, "unknown escape sequence '\\" + std::string(1, e == '\n' ? 'n' : e) + "'");
          // line-ending backslash: only whitespace up to a newline is allowed
          std::size_t q = p_ - 1;
          while (q < s_.size() && (s_[q] == ' ' || s_[q] == '\t')) ++q;
          if (q < s_.size() && s_[q] == '\r') ++q;
          if (q >= s_.size() || s_[q] != '\n') fail(esc, "unknown escape sequence (a backslash before whitespace must end the line)");
          while (q < s_.size() && (s_[q] == ' ' || s_[q] == '\t' || s_[q] == '\n' || s_[q] == '\r')) ++q;
          p_ = q;
          break;
        }
        default:
          fail(esc, std::string("unknown escape sequence '\\") + e + "'");
      }
    }
  }
  std::string parse_literal_string(bool multi) {
    std::size_t open = p_;
    p_ += multi ? 3 : 1;
    std::string out;
    if (multi) {
      if (!eof() && s_[p_] == '\n') ++p_;
      else if (p_ + 1 < s_.size() && s_[p_] == '\r' && s_[p_ + 1] == '\n') p_ += 2;
    }
    while (true) {
      if (eof()) fail(open, "unterminated string");
      char c = s_[p_];
      if (c == '\'') {
        if (!multi) { ++p_; return out; }
        std::size_t n = 0;
        while (p_ + n < s_.size() && s_[p_ + n] == '\'') ++n;
        if (n >= 3) {
          if (n > 5) fail(p_ + 5, "too many quotes at the end of a multi-line string");
          out.append(n - 3, '\'');
          p_ += n;
          return out;
        }
        out.append(n, '\'');
        p_ += n;
        continue;
      }
      if (c == '\n' || c == '\r') {
        if (!multi) fail(p_, "newline in a single-line string");
        if (c == '\r') ++p_;
        out += '\n';
        ++p_;
        continue;
      }
      out += c;
      ++p_;
    }
  }

  // ---- values -------------------------------------------------------------
  static bool is_delim(char c) {
    return c == ' ' || c == '\t' || c == ',' || c == ']' || c == '}' || c == '#' || c == '\n' || c == '\r';
  }

  Value parse_value(unsigned depth) {
    if (depth >= kMaxDepth) fail(p_, "nesting is deeper than " + std::to_string(kMaxDepth) + " levels");
    if (eof()) fail(p_, "expected a value");
    std::size_t at = p_;
    char c = s_[p_];
    Value v;
    if (c == '"') {
      bool multi = s_.substr(p_, 3) == "\"\"\"";
      v = Value::make_string(parse_basic_string(multi));
    } else if (c == '\'') {
      bool multi = s_.substr(p_, 3) == "'''";
      v = Value::make_string(parse_literal_string(multi));
    } else if (c == '[') {
      v = parse_array(depth);
    } else if (c == '{') {
      v = parse_inline_table(depth);
    } else {
      v = parse_scalar_token();
    }
    stamp(v, at);
    return v;
  }

  Value parse_array(unsigned depth) {
    ++p_;  // [
    Value arr = Value::make_array();
    while (true) {
      skip_ws_nl_comments();
      if (eof()) fail(p_, "unterminated array");
      if (s_[p_] == ']') { ++p_; return arr; }
      arr.a.push_back(parse_value(depth + 1));
      skip_ws_nl_comments();
      if (eof()) fail(p_, "unterminated array");
      if (s_[p_] == ',') { ++p_; continue; }
      if (s_[p_] == ']') { ++p_; return arr; }
      fail(p_, std::string("expected ',' or ']' in array, found '") + s_[p_] + "'");
    }
  }
  void skip_ws_nl_comments() {
    while (!eof()) {
      char c = s_[p_];
      if (c == ' ' || c == '\t' || c == '\n') ++p_;
      else if (c == '\r') p_ += 2;
      else if (c == '#') skip_comment();
      else break;
    }
  }

  Value parse_inline_table(unsigned depth) {
    std::size_t open = p_;
    ++p_;  // {
    Value tbl = Value::make_table();
    tbl.is_inline = true;
    skip_ws();
    if (!eof() && s_[p_] == '}') { ++p_; return tbl; }
    while (true) {
      skip_ws();
      if (eof()) fail(open, "unterminated inline table");
      if (s_[p_] == '\n' || s_[p_] == '\r') fail(p_, "newlines are not allowed inside an inline table");
      auto keys = parse_key_path();
      skip_ws();
      if (eof() || s_[p_] != '=') fail(p_, "expected '=' after key in inline table");
      ++p_;
      skip_ws();
      if (eof() || s_[p_] == '\n' || s_[p_] == '\r') fail(p_, "expected a value in inline table");
      Value v = parse_value(depth + 1);
      insert_kv(tbl, keys, std::move(v));
      skip_ws();
      if (eof()) fail(open, "unterminated inline table");
      if (s_[p_] == ',') {
        ++p_;
        skip_ws();
        if (!eof() && s_[p_] == '}') fail(p_, "trailing comma is not allowed in an inline table");
        continue;
      }
      if (s_[p_] == '}') { ++p_; return tbl; }
      if (s_[p_] == '\n' || s_[p_] == '\r') fail(p_, "newlines are not allowed inside an inline table");
      fail(p_, std::string("expected ',' or '}' in inline table, found '") + s_[p_] + "'");
    }
  }

  Value parse_scalar_token() {
    std::size_t b = p_;
    while (!eof() && !is_delim(s_[p_])) ++p_;
    std::string_view tok = s_.substr(b, p_ - b);
    if (tok.empty()) fail(b, std::string("unexpected '") + s_[b] + "' where a value was expected");
    if (tok == "true") return Value::make_bool(true);
    if (tok == "false") return Value::make_bool(false);
    if (looks_like_datetime(tok)) fail(b, "date/time values are not supported");
    if (!(is_digit(tok[0]) || tok[0] == '+' || tok[0] == '-' || tok[0] == 'i' || tok[0] == 'n'))
      fail(b, "unexpected '" + std::string(tok) + "' where a value was expected (strings need quotes)");
    if (tok == "inf" || tok == "+inf" || tok == "-inf" || tok == "nan" || tok == "+nan" || tok == "-nan") {
      Value v; v.type = Value::Type::Float;
      v.d = tok.back() == 'f' ? (tok[0] == '-' ? -INFINITY : INFINITY) : NAN;
      return v;
    }
    return parse_number(tok, b);
  }
  static bool looks_like_datetime(std::string_view t) {
    if (t.size() >= 10 && is_digit(t[0]) && is_digit(t[1]) && is_digit(t[2]) && is_digit(t[3]) && t[4] == '-' &&
        is_digit(t[5]) && is_digit(t[6]) && t[7] == '-')
      return true;
    return t.size() >= 5 && is_digit(t[0]) && is_digit(t[1]) && t[2] == ':' && is_digit(t[3]);
  }

  Value parse_number(std::string_view tok, std::size_t at) {
    auto bad = [&](const char* why) -> void { fail(at, std::string("invalid number '") + std::string(tok) + "': " + why); };
    std::size_t k = 0;
    bool neg = false;
    if (tok[0] == '+' || tok[0] == '-') { neg = tok[0] == '-'; k = 1; }
    if (k >= tok.size()) bad("no digits");
    // prefixed integers
    if (tok[k] == '0' && k + 1 < tok.size() && (tok[k + 1] == 'x' || tok[k + 1] == 'o' || tok[k + 1] == 'b')) {
      if (k != 0) bad("sign not allowed on prefixed integers");
      int base = tok[1] == 'x' ? 16 : tok[1] == 'o' ? 8 : 2;
      std::string digits;
      bool prev_us = true;
      for (std::size_t m = 2; m < tok.size(); ++m) {
        char c = tok[m];
        if (c == '_') { if (prev_us) bad("misplaced '_'"); prev_us = true; continue; }
        int hv = hexval(c);
        if (hv < 0 || hv >= base) bad("digit out of range for the base");
        digits += c;
        prev_us = false;
      }
      if (digits.empty() || prev_us) bad("no digits");
      std::uint64_t u = 0;
      auto r = std::from_chars(digits.data(), digits.data() + digits.size(), u, base);
      if (r.ec != std::errc() || r.ptr != digits.data() + digits.size() || u > static_cast<std::uint64_t>(INT64_MAX))
        bad("integer out of range");
      return Value::make_int(static_cast<std::int64_t>(u));
    }
    // decimal: validate the grammar, collect a clean copy
    std::string clean;
    if (neg) clean += '-';
    bool is_float = false;
    std::size_t m = k;
    auto digits_run = [&](bool leading_zero_check) {
      std::size_t start = m;
      bool prev_us = true;
      std::string run;
      while (m < tok.size() && (is_digit(tok[m]) || tok[m] == '_')) {
        if (tok[m] == '_') { if (prev_us) bad("misplaced '_'"); prev_us = true; }
        else { run += tok[m]; prev_us = false; }
        ++m;
      }
      if (m == start) bad("expected digits");
      if (prev_us) bad("misplaced '_'");
      if (leading_zero_check && run.size() > 1 && run[0] == '0') bad("leading zeros are not allowed");
      clean += run;
    };
    digits_run(true);
    if (m < tok.size() && tok[m] == '.') { is_float = true; clean += '.'; ++m; digits_run(false); }
    if (m < tok.size() && (tok[m] == 'e' || tok[m] == 'E')) {
      is_float = true; clean += 'e'; ++m;
      if (m < tok.size() && (tok[m] == '+' || tok[m] == '-')) clean += tok[m++];
      digits_run(false);
    }
    if (m != tok.size()) bad("unexpected character");
    if (!is_float) {
      std::int64_t v = 0;
      auto r = std::from_chars(clean.data(), clean.data() + clean.size(), v);
      if (r.ec != std::errc() || r.ptr != clean.data() + clean.size()) bad("integer out of range");
      return Value::make_int(v);
    }
    Value v; v.type = Value::Type::Float;
    auto r = std::from_chars(clean.data(), clean.data() + clean.size(), v.d);
    if (r.ec != std::errc() || r.ptr != clean.data() + clean.size() || std::isinf(v.d)) bad("float out of range");
    return v;
  }
};

// ---- writer -----------------------------------------------------------------
std::string quote(const std::string& s) {
  std::string o = "\"";
  for (unsigned char c : s) {
    switch (c) {
      case '"': o += "\\\""; break;
      case '\\': o += "\\\\"; break;
      case '\b': o += "\\b"; break;
      case '\t': o += "\\t"; break;
      case '\n': o += "\\n"; break;
      case '\f': o += "\\f"; break;
      case '\r': o += "\\r"; break;
      default:
        if (c < 0x20 || c == 0x7F) { char b[8]; std::snprintf(b, sizeof b, "\\u%04X", c); o += b; }
        else o += static_cast<char>(c);
    }
  }
  return o + "\"";
}
std::string key_text(const std::string& k) {
  bool bare = !k.empty();
  for (char c : k) if (!is_bare(c)) bare = false;
  return bare ? k : quote(k);
}
void write_inline(std::string& o, const Value& v) {
  switch (v.type) {
    case Value::Type::String: o += quote(v.s); break;
    case Value::Type::Integer: o += std::to_string(v.i); break;
    case Value::Type::Boolean: o += v.b ? "true" : "false"; break;
    case Value::Type::Float: {
      if (std::isnan(v.d)) { o += "nan"; break; }
      if (std::isinf(v.d)) { o += v.d < 0 ? "-inf" : "inf"; break; }
      char b[40];
      std::snprintf(b, sizeof b, "%.17g", v.d);
      std::string t = b;
      if (t.find_first_of(".en") == std::string::npos) t += ".0";
      o += t;
      break;
    }
    case Value::Type::Array: {
      o += "[";
      for (std::size_t k = 0; k < v.a.size(); ++k) { if (k) o += ", "; write_inline(o, v.a[k]); }
      o += "]";
      break;
    }
    case Value::Type::Table: {
      if (v.t.empty()) { o += "{}"; break; }
      o += "{ ";
      for (std::size_t k = 0; k < v.t.size(); ++k) {
        if (k) o += ", ";
        o += key_text(v.t[k].first) + " = ";
        write_inline(o, v.t[k].second);
      }
      o += " }";
      break;
    }
  }
}
void write_table(std::string& o, const Value& tbl, const std::string& path) {
  for (const auto& kv : tbl.t)
    if (!kv.second.is_table()) { o += key_text(kv.first) + " = "; write_inline(o, kv.second); o += "\n"; }
  for (const auto& kv : tbl.t) {
    if (!kv.second.is_table()) continue;
    std::string p = path.empty() ? key_text(kv.first) : path + "." + key_text(kv.first);
    o += "\n[" + p + "]\n";
    write_table(o, kv.second, p);
  }
}

}  // namespace

Value parse(std::string_view text, const std::string& source) { return Parser(text, source).run(); }

std::string write(const Value& root) {
  std::string o;
  write_table(o, root, "");
  return o;
}

}  // namespace mods::toml
