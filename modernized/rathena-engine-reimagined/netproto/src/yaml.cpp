#include "netproto/yaml.hpp"

#include <cctype>
#include <cstdio>

namespace netproto::yaml {

namespace {

[[noreturn]] void fail(int line, const std::string& what) {
  throw SpecError("yaml line " + std::to_string(line) + ": " + what);
}

struct Line {
  int indent = 0;
  std::string text;  // comment stripped, right-trimmed, indentation removed
  int no = 0;
};

// Removes a trailing comment, honouring quotes. Returns the cleaned text.
std::string strip_comment(const std::string& s, int line) {
  char q = 0;
  for (std::size_t i = 0; i < s.size(); ++i) {
    const char c = s[i];
    if (q) {
      if (c == '\\' && q == '"') { ++i; continue; }
      if (c == q) {
        if (q == '\'' && i + 1 < s.size() && s[i + 1] == '\'') { ++i; continue; }
        q = 0;
      }
    } else if ((c == '"' || c == '\'') && (i == 0 || s[i - 1] == ' ' || s[i - 1] == '[' || s[i - 1] == '{' ||
                                           s[i - 1] == ',' || s[i - 1] == ':' || s[i - 1] == '-')) {
      q = c;
    } else if (c == '#' && (i == 0 || s[i - 1] == ' ' || s[i - 1] == '\t')) {
      return s.substr(0, i);
    }
  }
  if (q) fail(line, "unterminated quoted scalar (multi-line scalars are not supported)");
  return s;
}

std::string rtrim(std::string s) {
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.pop_back();
  return s;
}

bool parse_int(const std::string& s, std::int64_t& out) {
  if (s.empty()) return false;
  std::size_t i = 0;
  bool neg = false;
  if (s[0] == '-' || s[0] == '+') { neg = s[0] == '-'; i = 1; }
  if (i >= s.size()) return false;
  std::uint64_t v = 0;
  if (s.size() - i > 2 && s[i] == '0' && (s[i + 1] == 'x' || s[i + 1] == 'X')) {
    i += 2;
    if (s.size() - i > 16) return false;
    for (; i < s.size(); ++i) {
      const char c = s[i];
      int d;
      if (c >= '0' && c <= '9') d = c - '0';
      else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
      else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
      else return false;
      v = v * 16 + static_cast<std::uint64_t>(d);
    }
  } else {
    if (s.size() - i > 18) return false;
    if (s[i] == '0' && s.size() - i > 1) return false;  // 007 is a string (YAML 1.1 would call it octal)
    for (; i < s.size(); ++i) {
      if (s[i] < '0' || s[i] > '9') return false;
      v = v * 10 + static_cast<std::uint64_t>(s[i] - '0');
    }
  }
  out = neg ? -static_cast<std::int64_t>(v) : static_cast<std::int64_t>(v);
  return true;
}

Node scalar_from_plain(const std::string& raw, int line) {
  Node n;
  n.line = line;
  const std::string s = rtrim(raw);
  if (s.empty() || s == "~" || s == "null") return n;
  if (s == "true" || s == "false") { n.kind = Node::Kind::Bool; n.b = s == "true"; return n; }
  if (s[0] == '&' || s[0] == '*' || s[0] == '!' || s[0] == '|' || s[0] == '>' || s[0] == '%' || s[0] == '@' || s[0] == '`')
    fail(line, "unsupported YAML feature near '" + s.substr(0, 12) + "'");
  std::int64_t v;
  if (parse_int(s, v)) { n.kind = Node::Kind::Int; n.i = v; return n; }
  n.kind = Node::Kind::Str;
  n.s = s;
  return n;
}

int hexval(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// Parses a quoted scalar starting at s[pos] (the quote); advances pos past the closing quote.
std::string parse_quoted(const std::string& s, std::size_t& pos, int line) {
  const char q = s[pos++];
  std::string out;
  while (pos < s.size()) {
    const char c = s[pos++];
    if (q == '"' && c == '\\') {
      if (pos >= s.size()) fail(line, "dangling escape");
      const char e = s[pos++];
      switch (e) {
        case '\\': out += '\\'; break;
        case '"': out += '"'; break;
        case '/': out += '/'; break;
        case 'n': out += '\n'; break;
        case 't': out += '\t'; break;
        case 'r': out += '\r'; break;
        case '0': out += '\0'; break;
        case 'x': {
          if (pos + 1 >= s.size() + 0 || hexval(s[pos]) < 0 || hexval(s[pos + 1]) < 0) fail(line, "bad \\x escape");
          out += static_cast<char>(hexval(s[pos]) * 16 + hexval(s[pos + 1]));
          pos += 2;
          break;
        }
        default: fail(line, std::string("unsupported escape \\") + e);
      }
    } else if (c == q) {
      if (q == '\'' && pos < s.size() && s[pos] == '\'') { out += '\''; ++pos; continue; }
      return out;
    } else {
      out += c;
    }
  }
  fail(line, "unterminated quoted scalar");
}

// ---- flow collections ----------------------------------------------------------------------
struct Flow {
  const std::string& s;
  std::size_t pos;
  int line;

  void ws() { while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t')) ++pos; }

  Node value(int depth) {
    if (depth > 32) fail(line, "flow collection nested too deeply");
    ws();
    if (pos >= s.size()) fail(line, "unexpected end of flow collection");
    if (s[pos] == '[') return seq(depth);
    if (s[pos] == '{') return map(depth);
    if (s[pos] == '"' || s[pos] == '\'') {
      Node n;
      n.line = line;
      n.kind = Node::Kind::Str;
      n.s = parse_quoted(s, pos, line);
      return n;
    }
    const std::size_t b = pos;
    while (pos < s.size() && s[pos] != ',' && s[pos] != ']' && s[pos] != '}') {
      if (s[pos] == ':' && (pos + 1 >= s.size() || s[pos + 1] == ' ')) break;  // a key, handled by map()
      ++pos;
    }
    return scalar_from_plain(s.substr(b, pos - b), line);
  }

  Node seq(int depth) {
    Node n;
    n.kind = Node::Kind::Seq;
    n.line = line;
    ++pos;  // [
    ws();
    if (pos < s.size() && s[pos] == ']') { ++pos; return n; }
    for (;;) {
      n.seq.push_back(value(depth + 1));
      ws();
      if (pos >= s.size()) fail(line, "unterminated flow sequence");
      if (s[pos] == ',') { ++pos; ws(); if (pos < s.size() && s[pos] == ']') { ++pos; return n; } continue; }
      if (s[pos] == ']') { ++pos; return n; }
      fail(line, "expected ',' or ']' in flow sequence");
    }
  }

  Node map(int depth) {
    Node n;
    n.kind = Node::Kind::Map;
    n.line = line;
    ++pos;  // {
    ws();
    if (pos < s.size() && s[pos] == '}') { ++pos; return n; }
    for (;;) {
      ws();
      std::string key;
      if (pos < s.size() && (s[pos] == '"' || s[pos] == '\'')) {
        key = parse_quoted(s, pos, line);
      } else {
        const std::size_t b = pos;
        while (pos < s.size() && s[pos] != ':' && s[pos] != ',' && s[pos] != '}') ++pos;
        key = rtrim(s.substr(b, pos - b));
      }
      ws();
      if (pos >= s.size() || s[pos] != ':') fail(line, "expected ':' after flow mapping key '" + key + "'");
      ++pos;
      for (const auto& k : n.keys) if (k == key) fail(line, "duplicate key '" + key + "'");
      n.keys.push_back(key);
      n.vals.push_back(value(depth + 1));
      ws();
      if (pos >= s.size()) fail(line, "unterminated flow mapping");
      if (s[pos] == ',') { ++pos; ws(); if (pos < s.size() && s[pos] == '}') { ++pos; return n; } continue; }
      if (s[pos] == '}') { ++pos; return n; }
      fail(line, "expected ',' or '}' in flow mapping");
    }
  }
};

Node parse_inline(const std::string& text, int line) {
  if (text.empty()) { Node n; n.line = line; return n; }
  if (text[0] == '[' || text[0] == '{') {
    Flow f{text, 0, line};
    Node n = f.value(0);
    f.ws();
    if (f.pos != text.size()) fail(line, "trailing characters after flow collection");
    return n;
  }
  if (text[0] == '"' || text[0] == '\'') {
    std::size_t pos = 0;
    Node n;
    n.line = line;
    n.kind = Node::Kind::Str;
    n.s = parse_quoted(text, pos, line);
    if (rtrim(text.substr(pos)).size() != 0) fail(line, "trailing characters after quoted scalar");
    return n;
  }
  return scalar_from_plain(text, line);
}

// Finds the `key:` split of a block mapping line, honouring quotes and flow brackets.
// Returns npos if the line is not a `key: value` / `key:` line.
std::size_t key_colon(const std::string& t) {
  if (t.empty() || t[0] == '[' || t[0] == '{') return std::string::npos;
  std::size_t i = 0;
  if (t[0] == '"' || t[0] == '\'') {
    const char q = t[0];
    ++i;
    while (i < t.size()) {
      if (t[i] == '\\' && q == '"') { i += 2; continue; }
      if (t[i] == q) { if (q == '\'' && i + 1 < t.size() && t[i + 1] == '\'') { i += 2; continue; } ++i; break; }
      ++i;
    }
    return (i < t.size() && t[i] == ':' && (i + 1 == t.size() || t[i + 1] == ' ')) ? i : std::string::npos;
  }
  for (; i < t.size(); ++i)
    if (t[i] == ':' && (i + 1 == t.size() || t[i + 1] == ' ')) return i;
  return std::string::npos;
}

struct Parser {
  std::vector<Line> L;
  std::size_t at = 0;

  [[nodiscard]] bool done() const { return at >= L.size(); }

  Node block(int indent) {
    if (done()) { Node n; return n; }
    const Line& l = L[at];
    if (l.text == "-" || l.text.rfind("- ", 0) == 0) return sequence(l.indent);
    (void)indent;
    return mapping(l.indent);
  }

  Node sequence(int indent) {
    Node n;
    n.kind = Node::Kind::Seq;
    n.line = L[at].no;
    while (!done() && L[at].indent == indent && (L[at].text == "-" || L[at].text.rfind("- ", 0) == 0)) {
      Line l = L[at];
      std::string rest = l.text == "-" ? std::string() : l.text.substr(2);
      std::size_t skip = 0;
      while (skip < rest.size() && rest[skip] == ' ') ++skip;
      rest = rest.substr(skip);
      if (rest.empty()) {
        ++at;
        if (!done() && L[at].indent > indent) n.seq.push_back(block(L[at].indent));
        else { Node e; e.line = l.no; n.seq.push_back(e); }
      } else if (key_colon(rest) != std::string::npos) {
        // "- key: v" opens a mapping whose indentation is the column of `key`
        L[at].indent = indent + 2 + static_cast<int>(skip);
        L[at].text = rest;
        n.seq.push_back(mapping(L[at].indent));
      } else if (rest == "-" || rest.rfind("- ", 0) == 0) {
        fail(l.no, "nested '- -' sequences are not supported");
      } else {
        ++at;
        n.seq.push_back(parse_inline(rest, l.no));
      }
    }
    return n;
  }

  Node mapping(int indent) {
    Node n;
    n.kind = Node::Kind::Map;
    n.line = L[at].no;
    while (!done() && L[at].indent == indent) {
      const Line l = L[at];
      if (l.text == "-" || l.text.rfind("- ", 0) == 0) break;
      const std::size_t c = key_colon(l.text);
      if (c == std::string::npos) fail(l.no, "expected 'key: value', got '" + l.text.substr(0, 40) + "'");
      std::string key;
      if (l.text[0] == '"' || l.text[0] == '\'') {
        std::size_t p = 0;
        key = parse_quoted(l.text, p, l.no);
      } else {
        key = rtrim(l.text.substr(0, c));
      }
      for (const auto& k : n.keys) if (k == key) fail(l.no, "duplicate key '" + key + "'");
      std::string rest = l.text.substr(c + 1);
      std::size_t skip = 0;
      while (skip < rest.size() && rest[skip] == ' ') ++skip;
      rest = rest.substr(skip);
      ++at;
      Node v;
      if (rest.empty()) {
        if (!done() && L[at].indent > indent) v = block(L[at].indent);
        else if (!done() && L[at].indent == indent && (L[at].text == "-" || L[at].text.rfind("- ", 0) == 0)) v = sequence(indent);
        else v.line = l.no;
      } else {
        v = parse_inline(rest, l.no);
      }
      n.keys.push_back(std::move(key));
      n.vals.push_back(std::move(v));
    }
    if (!done() && L[at].indent > indent) fail(L[at].no, "unexpected indentation");
    return n;
  }
};

void json_str(const std::string& s, std::string& out) {
  out += '"';
  for (unsigned char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\t': out += "\\t"; break;
      case '\r': out += "\\r"; break;
      default:
        if (c < 0x20 || c >= 0x7f) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); out += b; }
        else out += static_cast<char>(c);
    }
  }
  out += '"';
}

void json(const Node& n, std::string& out) {
  switch (n.kind) {
    case Node::Kind::Null: out += "null"; break;
    case Node::Kind::Bool: out += n.b ? "true" : "false"; break;
    case Node::Kind::Int: out += std::to_string(n.i); break;
    case Node::Kind::Str: json_str(n.s, out); break;
    case Node::Kind::Seq:
      out += '[';
      for (std::size_t i = 0; i < n.seq.size(); ++i) { if (i) out += ','; json(n.seq[i], out); }
      out += ']';
      break;
    case Node::Kind::Map:
      out += '{';
      for (std::size_t i = 0; i < n.keys.size(); ++i) {
        if (i) out += ',';
        json_str(n.keys[i], out);
        out += ':';
        json(n.vals[i], out);
      }
      out += '}';
      break;
  }
}

}  // namespace

const Node* Node::find(const std::string& key) const {
  if (kind != Kind::Map) return nullptr;
  for (std::size_t i = 0; i < keys.size(); ++i)
    if (keys[i] == key) return &vals[i];
  return nullptr;
}

const Node& Node::at(const std::string& key) const {
  if (const Node* n = find(key)) return *n;
  throw SpecError("yaml line " + std::to_string(line) + ": missing key '" + key + "'");
}

Node parse(const std::string& text) {
  Parser p;
  int no = 0;
  std::size_t pos = 0;
  bool first = true;
  while (pos <= text.size()) {
    std::size_t e = text.find('\n', pos);
    if (e == std::string::npos) e = text.size();
    std::string raw = text.substr(pos, e - pos);
    pos = e + 1;
    ++no;
    if (!raw.empty() && raw.back() == '\r') raw.pop_back();
    std::size_t ind = 0;
    while (ind < raw.size() && raw[ind] == ' ') ++ind;
    if (ind < raw.size() && raw[ind] == '\t') fail(no, "tab in indentation");
    std::string body = rtrim(strip_comment(raw.substr(ind), no));
    if (body.empty()) continue;
    if (first && body == "---" && ind == 0) { first = false; continue; }
    first = false;
    if (body == "---" || body == "...") fail(no, "multiple documents are not supported");
    p.L.push_back({static_cast<int>(ind), std::move(body), no});
    if (pos > text.size()) break;
  }
  if (p.L.empty()) { Node n; return n; }
  Node root = p.block(p.L[0].indent);
  if (!p.done()) fail(p.L[p.at].no, "unexpected content (bad indentation?)");
  return root;
}

std::string to_json(const Node& n) {
  std::string out;
  json(n, out);
  return out;
}

}  // namespace netproto::yaml
