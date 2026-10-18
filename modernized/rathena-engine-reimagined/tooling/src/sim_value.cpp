#include "tooling/sim_value.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace tooling::sim {

namespace {
const char* type_name(const Val& v) {
  switch (v.kind) {
    case Val::Kind::Nil: return "nil";
    case Val::Kind::Bool: return "boolean";
    case Val::Kind::Num: return "number";
    case Val::Kind::Str: return "string";
    case Val::Kind::Arr: return "array";
    case Val::Kind::Map: return "map";
  }
  return "?";
}
[[noreturn]] void bad(const Val& v, const char* want) {
  throw ValueError(std::string("expected ") + want + ", got " + type_name(v));
}
}  // namespace

bool Val::truthy() const {
  if (is_nil()) return false;
  if (is_bool()) return b;
  return true;
}
const std::string& Val::str() const {
  if (kind != Kind::Str) bad(*this, "string");
  return s;
}
double Val::num() const {
  if (kind != Kind::Num) bad(*this, "number");
  return d;
}
bool Val::boolean() const {
  if (kind != Kind::Bool) bad(*this, "boolean");
  return b;
}
const VArr& Val::arr() const {
  if (kind != Kind::Arr) bad(*this, "array");
  return a;
}
const VMap& Val::map() const {
  if (kind != Kind::Map) bad(*this, "map");
  return m;
}
VArr& Val::arr_mut() {
  if (is_nil()) kind = Kind::Arr;
  if (kind != Kind::Arr) bad(*this, "array");
  return a;
}
VMap& Val::map_mut() {
  if (is_nil()) kind = Kind::Map;
  if (kind != Kind::Map) bad(*this, "map");
  return m;
}
const Val* Val::get(const std::string& key) const {
  if (kind != Kind::Map) return nullptr;
  auto it = m.find(key);
  return it == m.end() ? nullptr : &it->second;
}
std::string Val::str_or(const std::string& key, const std::string& dflt) const {
  const Val* f = get(key);
  return f && f->is_str() ? f->s : dflt;
}
double Val::num_or(const std::string& key, double dflt) const {
  const Val* f = get(key);
  return f && f->is_num() ? f->d : dflt;
}
bool Val::bool_or(const std::string& key, bool dflt) const {
  const Val* f = get(key);
  return f && f->is_bool() ? f->b : dflt;
}

namespace {
void enc(const Val& val, std::string& out) {
  if (val.is_nil()) {
    out += 'N';
  } else if (val.is_bool()) {
    out += val.b ? 'T' : 'F';
  } else if (val.is_num()) {
    char buf[40];
    std::snprintf(buf, sizeof buf, "%.17g", val.d);
    out += 'n';
    out += buf;
    out += ';';
  } else if (val.is_str()) {
    out += 's';
    out += std::to_string(val.s.size());
    out += ':';
    out += val.s;
  } else if (val.is_arr()) {
    out += 'a';
    out += std::to_string(val.a.size());
    out += ':';
    for (const auto& e : val.a) enc(e, out);
  } else {
    const auto& m = val.m;
    out += 'm';
    out += std::to_string(m.size());
    out += ':';
    for (const auto& [k, e] : m) {
      out += 's';
      out += std::to_string(k.size());
      out += ':';
      out += k;
      enc(e, out);
    }
  }
}

size_t find_char(std::string_view t, char c, size_t from) {
  size_t p = t.find(c, from);
  if (p == std::string_view::npos) throw ValueError("malformed value: missing '" + std::string(1, c) + "'");
  return p;
}
size_t parse_count(std::string_view t, size_t from, size_t to) {
  if (to <= from || to - from > 12) throw ValueError("malformed value: bad length");
  size_t n = 0;
  for (size_t i = from; i < to; ++i) {
    if (t[i] < '0' || t[i] > '9') throw ValueError("malformed value: bad length digit");
    n = n * 10 + static_cast<size_t>(t[i] - '0');
  }
  return n;
}

Val dec(std::string_view t, size_t& pos, int depth) {
  if (depth > 64) throw ValueError("value nested too deeply");
  if (pos >= t.size()) throw ValueError("malformed value: unexpected end");
  char c = t[pos];
  switch (c) {
    case 'N': ++pos; return Val();
    case 'T': ++pos; return Val(true);
    case 'F': ++pos; return Val(false);
    case 'n': {
      size_t e = find_char(t, ';', pos);
      std::string num(t.substr(pos + 1, e - pos - 1));
      pos = e + 1;
      char* end = nullptr;
      double d = std::strtod(num.c_str(), &end);
      if (end == num.c_str() || *end != '\0') throw ValueError("malformed number '" + num + "'");
      return Val(d);
    }
    case 's': {
      size_t e = find_char(t, ':', pos);
      size_t n = parse_count(t, pos + 1, e);
      if (e + 1 + n > t.size()) throw ValueError("malformed value: string overruns input");
      Val r(std::string(t.substr(e + 1, n)));
      pos = e + 1 + n;
      return r;
    }
    case 'a': {
      size_t e = find_char(t, ':', pos);
      size_t n = parse_count(t, pos + 1, e);
      pos = e + 1;
      VArr a;
      for (size_t i = 0; i < n; ++i) a.push_back(dec(t, pos, depth + 1));
      return Val(std::move(a));
    }
    case 'm': {
      size_t e = find_char(t, ':', pos);
      size_t n = parse_count(t, pos + 1, e);
      pos = e + 1;
      VMap m;
      for (size_t i = 0; i < n; ++i) {
        Val k = dec(t, pos, depth + 1);
        if (!k.is_str()) throw ValueError("malformed value: map key is not a string");
        m[k.str()] = dec(t, pos, depth + 1);
      }
      return Val(std::move(m));
    }
    default:
      throw ValueError(std::string("malformed value: unknown tag '") + c + "'");
  }
}

// Length of the valid UTF-8 sequence starting at s[i], or 0 when the bytes there are not valid UTF-8.
size_t utf8_len(const std::string& s, size_t i) {
  const unsigned char c = static_cast<unsigned char>(s[i]);
  size_t n = c < 0x80 ? 1 : (c >= 0xc2 && c <= 0xdf ? 2 : (c >= 0xe0 && c <= 0xef ? 3 : (c >= 0xf0 && c <= 0xf4 ? 4 : 0)));
  if (n == 0 || i + n > s.size()) return 0;
  for (size_t k = 1; k < n; ++k)
    if ((static_cast<unsigned char>(s[i + k]) & 0xc0) != 0x80) return 0;
  if (n == 3) {
    const unsigned char c1 = static_cast<unsigned char>(s[i + 1]);
    if ((c == 0xe0 && c1 < 0xa0) || (c == 0xed && c1 > 0x9f)) return 0;  // overlong, surrogates
  }
  if (n == 4) {
    const unsigned char c1 = static_cast<unsigned char>(s[i + 1]);
    if ((c == 0xf0 && c1 < 0x90) || (c == 0xf4 && c1 > 0x8f)) return 0;
  }
  return n;
}

// JSON must be UTF-8: bytes that are not valid UTF-8 (a title cut in the middle of a character, binary
// from a packet) are written as U+FFFD instead of being passed through.
void json_str(const std::string& s, std::string& out) {
  out += '"';
  for (size_t i = 0; i < s.size();) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (c >= 0x80) {
      const size_t n = utf8_len(s, i);
      if (n == 0) { out += "\\ufffd"; ++i; }
      else { out.append(s, i, n); i += n; }
      continue;
    }
    ++i;
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); out += b; }
        else out += static_cast<char>(c);
    }
  }
  out += '"';
}

void json(const Val& v, int indent, int level, std::string& out) {
  auto nl = [&](int l) {
    if (indent <= 0) return;
    out += '\n';
    out.append(static_cast<size_t>(indent * l), ' ');
  };
  if (v.is_nil()) out += "null";
  else if (v.is_bool()) out += v.b ? "true" : "false";
  else if (v.is_num()) {
    char buf[40];
    if (std::floor(v.d) == v.d && std::fabs(v.d) < 9.0e15) std::snprintf(buf, sizeof buf, "%lld", static_cast<long long>(v.d));
    else std::snprintf(buf, sizeof buf, "%.17g", v.d);
    out += buf;
  } else if (v.is_str()) json_str(v.s, out);
  else if (v.is_arr()) {
    const auto& a = v.a;
    if (a.empty()) { out += "[]"; return; }
    out += '[';
    for (size_t i = 0; i < a.size(); ++i) {
      if (i) out += ',';
      nl(level + 1);
      json(a[i], indent, level + 1, out);
    }
    nl(level);
    out += ']';
  } else {
    const auto& m = v.m;
    if (m.empty()) { out += "{}"; return; }
    out += '{';
    bool first = true;
    for (const auto& [k, e] : m) {
      if (!first) out += ',';
      first = false;
      nl(level + 1);
      json_str(k, out);
      out += indent > 0 ? ": " : ":";
      json(e, indent, level + 1, out);
    }
    nl(level);
    out += '}';
  }
}
}  // namespace

std::string encode(const Val& v) {
  std::string out;
  enc(v, out);
  return out;
}
Val decode(std::string_view text, size_t& pos) { return dec(text, pos, 0); }
Val decode_all(std::string_view text) {
  size_t pos = 0;
  Val v = dec(text, pos, 0);
  if (pos != text.size()) throw ValueError("malformed value: trailing bytes");
  return v;
}
std::string utf8_truncate(const std::string& s, size_t max_bytes) {
  if (s.size() <= max_bytes) return s;
  size_t n = max_bytes;
  while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xc0) == 0x80) --n;
  return s.substr(0, n);
}

std::string to_json(const Val& v, int indent) {
  std::string out;
  json(v, indent, 0, out);
  return out;
}

}  // namespace tooling::sim
