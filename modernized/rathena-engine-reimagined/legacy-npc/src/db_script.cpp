#include "legacy_npc/db_script.hpp"

#include <cctype>
#include <cstdlib>

namespace legacy_npc {

namespace {

std::string trim(std::string_view s) {
  std::size_t a = 0, b = s.size();
  while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
  return std::string(s.substr(a, b - a));
}

bool is_ident_char(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

// one argument: an integer literal, a quoted string, or a bare constant name; anything else (operators, calls, variables) is not
// straight-line data
bool parse_arg(const std::string& a, Value& out) {
  if (a.empty()) return false;
  if (a.front() == '"') {
    if (a.size() < 2 || a.back() != '"') return false;
    out = a.substr(1, a.size() - 2);
    return true;
  }
  std::size_t i = 0;
  if (a[0] == '-' || a[0] == '+') i = 1;
  if (i < a.size() && std::isdigit(static_cast<unsigned char>(a[i]))) {
    char* end = nullptr;
    long long v = std::strtoll(a.c_str(), &end, 0);
    if (*end != '\0') return false;
    out = static_cast<std::int64_t>(v);
    return true;
  }
  for (char c : a)
    if (!is_ident_char(c)) return false;
  out = a;  // constant name (RC_Brute, ELE_Fire ...): resolved by the ruleset's vocabulary, not here
  return true;
}

}  // namespace

// Straight-line bonus scripts (the 29,585 `Script` strings are mostly `bonus` lines) become a table of deltas: values, no memory writes,
// cacheable. Anything else (conditions, assignments, calls, expressions in arguments) needs the interpreter.
CompiledDbScript compile_db_script(std::string_view script) {
  CompiledDbScript out;
  std::vector<std::string> stmts;
  std::string cur;
  bool in_str = false;
  for (char c : script) {
    if (c == '"') in_str = !in_str;
    if (c == ';' && !in_str) { stmts.push_back(trim(cur)); cur.clear(); }
    else cur += c;
  }
  if (!trim(cur).empty()) stmts.push_back(trim(cur));  // a final statement without ';' is still a statement
  for (auto& st : stmts) {
    if (st.empty()) continue;
    std::size_t p = 0;
    while (p < st.size() && is_ident_char(st[p])) ++p;
    std::string cmd = st.substr(0, p);
    bool is_bonus = cmd == "bonus" || (cmd.size() == 6 && cmd.compare(0, 5, "bonus") == 0 && cmd[5] >= '2' && cmd[5] <= '5');
    if (!is_bonus) {
      out.reason = "not straight-line: '" + cmd + "'";
      out.deltas.clear();
      return out;
    }
    // split the rest at commas outside strings
    std::vector<std::string> parts;
    std::string a;
    in_str = false;
    for (std::size_t i = p; i < st.size(); ++i) {
      char c = st[i];
      if (c == '"') in_str = !in_str;
      if (c == ',' && !in_str) { parts.push_back(trim(a)); a.clear(); }
      else a += c;
    }
    parts.push_back(trim(a));
    if (parts.empty() || parts[0].empty()) { out.reason = "bonus without a name"; out.deltas.clear(); return out; }
    BonusDelta d;
    d.bonus = parts[0];
    for (char c : d.bonus)
      if (!is_ident_char(c)) { out.reason = "computed bonus name"; out.deltas.clear(); return out; }
    for (std::size_t i = 1; i < parts.size(); ++i) {
      Value v;
      if (!parse_arg(parts[i], v)) { out.reason = "expression argument '" + parts[i] + "'"; out.deltas.clear(); return out; }
      d.args.push_back(std::move(v));
    }
    out.deltas.push_back(std::move(d));
  }
  out.kind = CompiledDbScript::Kind::BonusTable;
  return out;
}

}  // namespace legacy_npc
