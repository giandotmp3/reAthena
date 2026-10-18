#include "script_host/yaml_lite.hpp"

#include <cctype>

#include "script_host/not_implemented.hpp"

namespace script_host::yaml {

const Node* Node::find(const std::string& key) const {
  for (auto& [k, v] : map)
    if (k == key) return &v;
  return nullptr;
}

namespace {

struct Line {
  int indent;
  std::string text;  // trimmed, comment removed
  int number;        // 1-based source line
};

[[noreturn]] void fail(int line, const std::string& what) {
  throw LoadError("yaml line " + std::to_string(line) + ": " + what);
}

std::string strip_comment(const std::string& s) {
  char q = 0;
  for (std::size_t i = 0; i < s.size(); ++i) {
    char c = s[i];
    if (q) {
      if (c == '\\' && q == '"') ++i;
      else if (c == q) q = 0;
    } else if (c == '"' || c == '\'') {
      q = c;
    } else if (c == '#' && (i == 0 || s[i - 1] == ' ')) {
      return s.substr(0, i);
    }
  }
  return s;
}
std::string trim(const std::string& s) {
  std::size_t a = 0, b = s.size();
  while (a < b && (s[a] == ' ' || s[a] == '\r')) ++a;
  while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\r')) --b;
  return s.substr(a, b - a);
}

// ---- flow collections / scalars -------------------------------------------------
struct Flow {
  const std::string& s;
  std::size_t i;
  int line;
  void ws() { while (i < s.size() && s[i] == ' ') ++i; }
  Node value() {
    ws();
    if (i >= s.size()) fail(line, "unexpected end of flow value");
    if (s[i] == '{') return map();
    if (s[i] == '[') return seq();
    return scalar(true);
  }
  Node scalar(bool in_flow) {
    Node n;
    n.kind = Node::Kind::Scalar;
    if (s[i] == '"' || s[i] == '\'') {
      char q = s[i++];
      n.quoted = true;
      while (true) {
        if (i >= s.size()) fail(line, "unterminated string");
        char c = s[i++];
        if (c == q) {
          if (q == '\'' && i < s.size() && s[i] == '\'') { n.scalar += '\''; ++i; continue; }
          break;
        }
        if (c == '\\' && q == '"') {
          if (i >= s.size()) fail(line, "bad escape");
          char e = s[i++];
          n.scalar += e == 'n' ? '\n' : e == 't' ? '\t' : e;
        } else {
          n.scalar += c;
        }
      }
      return n;
    }
    std::size_t st = i;
    while (i < s.size()) {
      char c = s[i];
      if (in_flow && (c == ',' || c == '}' || c == ']')) break;
      if (in_flow && c == ':' && (i + 1 >= s.size() || s[i + 1] == ' ')) break;
      ++i;
    }
    n.scalar = trim(s.substr(st, i - st));
    if (n.scalar == "~" || n.scalar == "null") n.kind = Node::Kind::Null;
    return n;
  }
  Node seq() {
    Node n;
    n.kind = Node::Kind::Seq;
    ++i;
    ws();
    if (i < s.size() && s[i] == ']') { ++i; return n; }
    while (true) {
      n.seq.push_back(value());
      ws();
      if (i >= s.size()) fail(line, "unterminated [");
      if (s[i] == ',') { ++i; ws(); if (i < s.size() && s[i] == ']') { ++i; return n; } continue; }
      if (s[i] == ']') { ++i; return n; }
      fail(line, "expected , or ] in flow sequence");
    }
  }
  Node map() {
    Node n;
    n.kind = Node::Kind::Map;
    ++i;
    ws();
    if (i < s.size() && s[i] == '}') { ++i; return n; }
    while (true) {
      ws();
      if (i >= s.size()) fail(line, "unterminated {");
      Node k = scalar(true);
      ws();
      if (i >= s.size() || s[i] != ':') fail(line, "expected ':' in flow mapping");
      ++i;
      Node v = value();
      n.map.emplace_back(k.scalar, std::move(v));
      ws();
      if (i >= s.size()) fail(line, "unterminated {");
      if (s[i] == ',') { ++i; ws(); if (i < s.size() && s[i] == '}') { ++i; return n; } continue; }
      if (s[i] == '}') { ++i; return n; }
      fail(line, "expected , or } in flow mapping");
    }
  }
};

Node inline_value(const std::string& text, int line) {
  Flow f{text, 0, line};
  f.ws();
  if (f.i >= text.size()) return Node{};
  Node n;
  if (text[f.i] == '{' || text[f.i] == '[') n = f.value();
  else n = f.scalar(false);
  f.ws();
  if (f.i != text.size()) fail(line, "unexpected trailing text '" + text.substr(f.i) + "'");
  return n;
}

// ---- block structure ------------------------------------------------------------
struct Block {
  std::vector<Line> lines;
  std::size_t pos = 0;

  // "key: rest" split; returns false if the line is not a mapping entry.
  static bool split_key(const std::string& t, std::string& key, std::string& rest) {
    if (t.empty() || t[0] == '{' || t[0] == '[' || t[0] == '"' || t[0] == '\'') return false;
    for (std::size_t i = 0; i < t.size(); ++i) {
      if (t[i] == ':' && (i + 1 == t.size() || t[i + 1] == ' ')) {
        key = trim(t.substr(0, i));
        rest = trim(t.substr(i + 1));
        return !key.empty();
      }
    }
    return false;
  }

  Node parse_block(int indent) {
    const Line& first = lines[pos];
    if (first.text.rfind("- ", 0) == 0 || first.text == "-") return parse_seq(indent);
    std::string k, r;
    if (split_key(first.text, k, r)) return parse_map(indent);
    pos++;
    return inline_value(first.text, first.number);
  }

  Node parse_seq(int indent) {
    Node n;
    n.kind = Node::Kind::Seq;
    while (pos < lines.size() && lines[pos].indent == indent && (lines[pos].text.rfind("- ", 0) == 0 || lines[pos].text == "-")) {
      Line cur = lines[pos];
      std::string rest = cur.text == "-" ? "" : trim(cur.text.substr(2));
      if (rest.empty()) {
        pos++;
        if (pos < lines.size() && lines[pos].indent > indent) n.seq.push_back(parse_block(lines[pos].indent));
        else n.seq.push_back(Node{});
      } else {
        std::string k, r;
        if (split_key(rest, k, r)) {
          // "- key: v" opens a mapping whose entries align with the text after "- ".
          int inner = indent + static_cast<int>(cur.text.find_first_not_of(' ', 1));
          lines[pos] = {inner, rest, cur.number};
          n.seq.push_back(parse_map(inner));
        } else {
          pos++;
          n.seq.push_back(inline_value(rest, cur.number));
        }
      }
    }
    return n;
  }

  Node parse_map(int indent) {
    Node n;
    n.kind = Node::Kind::Map;
    while (pos < lines.size() && lines[pos].indent == indent) {
      Line cur = lines[pos];
      std::string k, r;
      if (!split_key(cur.text, k, r)) fail(cur.number, "expected 'key: value'");
      if (k.size() >= 2 && (k.front() == '"' || k.front() == '\'') && k.back() == k.front()) k = k.substr(1, k.size() - 2);
      for (auto& e : n.map)
        if (e.first == k) fail(cur.number, "duplicate key '" + k + "'");
      pos++;
      if (r.empty()) {
        if (pos < lines.size() && lines[pos].indent > indent) n.map.emplace_back(k, parse_block(lines[pos].indent));
        else if (pos < lines.size() && lines[pos].indent == indent && lines[pos].text.rfind("- ", 0) == 0)
          n.map.emplace_back(k, parse_seq(indent));  // sequences may sit at the key's indent
        else n.map.emplace_back(k, Node{});
      } else {
        n.map.emplace_back(k, inline_value(r, cur.number));
      }
    }
    if (pos < lines.size() && lines[pos].indent > indent) fail(lines[pos].number, "unexpected indentation");
    return n;
  }
};

}  // namespace

Node parse(const std::string& text) {
  Block b;
  int number = 0;
  std::size_t start = 0;
  while (start <= text.size()) {
    std::size_t end = text.find('\n', start);
    if (end == std::string::npos) end = text.size();
    std::string raw = text.substr(start, end - start);
    ++number;
    start = end + 1;
    if (raw.find('\t') != std::string::npos && raw.find_first_not_of(" \t\r") != std::string::npos && raw.find('\t') < raw.find_first_not_of(" \t\r"))
      fail(number, "tabs are not allowed for indentation");
    std::string stripped = strip_comment(raw);
    std::size_t ind = stripped.find_first_not_of(' ');
    if (ind == std::string::npos) continue;
    std::string t = trim(stripped);
    if (t.empty()) continue;
    if (t == "---" || t == "...") continue;
    b.lines.push_back({static_cast<int>(ind), t, number});
  }
  if (b.lines.empty()) return Node{};
  Node root = b.parse_block(b.lines[0].indent);
  if (b.pos < b.lines.size()) fail(b.lines[b.pos].number, "unexpected content");
  return root;
}

}  // namespace script_host::yaml
