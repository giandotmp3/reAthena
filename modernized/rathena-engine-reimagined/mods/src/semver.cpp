#include "mods/semver.hpp"

#include <cstring>

#include "mods/common.hpp"

namespace mods {

std::optional<SemVer> parse_semver(const std::string& s) {
  SemVer v;
  std::uint32_t* parts[3] = {&v.major, &v.minor, &v.patch};
  std::size_t i = 0;
  for (int p = 0; p < 3; ++p) {
    if (i >= s.size()) return std::nullopt;
    std::uint64_t n = 0;
    std::size_t start = i;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
      n = n * 10 + static_cast<std::uint64_t>(s[i] - '0');
      if (n > 0xFFFFFFFFull) return std::nullopt;
      ++i;
    }
    if (i == start) return std::nullopt;
    if (s[start] == '0' && i - start > 1) return std::nullopt;  // no leading zeros
    *parts[p] = static_cast<std::uint32_t>(n);
    if (p < 2) {
      if (i >= s.size() || s[i] != '.') return std::nullopt;
      ++i;
    }
  }
  if (i != s.size()) return std::nullopt;
  return v;
}

std::string to_string(const SemVer& v) {
  return std::to_string(v.major) + "." + std::to_string(v.minor) + "." + std::to_string(v.patch);
}

bool VersionRange::matches(const SemVer& v) const {
  for (const auto& c : all) {
    bool ok = false;
    switch (c.op) {
      case Comparator::Op::Ge: ok = v >= c.v; break;
      case Comparator::Op::Gt: ok = v > c.v; break;
      case Comparator::Op::Le: ok = v <= c.v; break;
      case Comparator::Op::Lt: ok = v < c.v; break;
      case Comparator::Op::Eq: ok = v == c.v; break;
    }
    if (!ok) return false;
  }
  return true;
}

namespace {

struct Partial {
  std::uint32_t n[3] = {0, 0, 0};
  int specified = 0;  // how many leading components were given
};

bool parse_partial(const std::string& t, Partial& out, std::string& why) {
  std::size_t i = 0;
  bool wild = false;
  for (int p = 0; p < 3; ++p) {
    if (i >= t.size()) break;
    if (t[i] == 'x' || t[i] == 'X' || t[i] == '*') {
      wild = true;
      ++i;
    } else {
      if (wild) { why = "a number cannot follow a wildcard in \"" + t + "\""; return false; }
      std::size_t start = i;
      std::uint64_t n = 0;
      while (i < t.size() && t[i] >= '0' && t[i] <= '9') {
        n = n * 10 + static_cast<std::uint64_t>(t[i] - '0');
        if (n > 0xFFFFFFFEull) { why = "version component too large in \"" + t + "\""; return false; }
        ++i;
      }
      if (i == start) { why = "expected a version number in \"" + t + "\""; return false; }
      if (t[start] == '0' && i - start > 1) { why = "leading zero in \"" + t + "\""; return false; }
      out.n[p] = static_cast<std::uint32_t>(n);
      out.specified = p + 1;
    }
    if (p < 2 && i < t.size()) {
      if (t[i] != '.') { why = "unexpected character '" + std::string(1, t[i]) + "' in \"" + t + "\""; return false; }
      ++i;
      if (i >= t.size()) { why = "trailing '.' in \"" + t + "\""; return false; }
    }
  }
  if (i != t.size()) { why = "unexpected trailing text in \"" + t + "\""; return false; }
  return out.specified > 0 || wild;
}

SemVer sv(std::uint32_t a, std::uint32_t b, std::uint32_t c) { return SemVer{a, b, c}; }

}  // namespace

std::optional<VersionRange> parse_range(const std::string& text, std::string* why_out) {
  std::string why;
  auto fail = [&](const std::string& m) -> std::optional<VersionRange> {
    if (why_out) *why_out = m;
    return std::nullopt;
  };
  // tokenize: whitespace and commas separate; an operator-only token joins the next one
  std::vector<std::string> toks;
  std::string cur;
  auto flush = [&] { if (!cur.empty()) { toks.push_back(cur); cur.clear(); } };
  for (char ch : text) {
    if (ch == ' ' || ch == '\t' || ch == ',') flush();
    else cur += ch;
  }
  flush();
  std::vector<std::string> terms;
  for (std::size_t k = 0; k < toks.size(); ++k) {
    const std::string& t = toks[k];
    bool op_only = t == "^" || t == "~" || t == ">=" || t == "<=" || t == ">" || t == "<" || t == "=";
    if (op_only) {
      if (k + 1 >= toks.size()) return fail("operator \"" + t + "\" has no version");
      terms.push_back(t + toks[++k]);
    } else {
      terms.push_back(t);
    }
  }
  VersionRange r;
  for (const std::string& term : terms) {
    std::string op;
    std::size_t i = 0;
    for (const char* cand : {">=", "<=", "^", "~", ">", "<", "="})
      if (term.compare(0, std::strlen(cand), cand) == 0) { op = cand; i = op.size(); break; }
    Partial p;
    if (term.substr(i) == "*" || term.substr(i) == "x" || term.substr(i) == "X") {
      if (!op.empty() && op != "=" && op != ">=" ) return fail("operator \"" + op + "\" cannot apply to a wildcard");
      continue;  // matches anything
    }
    if (!parse_partial(term.substr(i), p, why)) return fail(why.empty() ? "malformed version in \"" + term + "\"" : why);
    const std::uint32_t M = p.n[0], m = p.n[1], pa = p.n[2];
    auto add = [&](Comparator::Op o, SemVer v) { r.all.push_back({o, v}); };
    using O = Comparator::Op;
    if (op == "^") {
      add(O::Ge, sv(M, m, pa));
      if (M > 0 || p.specified == 1) add(O::Lt, M > 0 ? sv(M + 1, 0, 0) : sv(1, 0, 0));
      else if (m > 0 || p.specified == 2) add(O::Lt, m > 0 ? sv(0, m + 1, 0) : sv(0, 1, 0));
      else add(O::Lt, sv(0, 0, pa + 1));
    } else if (op == "~") {
      add(O::Ge, sv(M, m, pa));
      add(O::Lt, p.specified >= 2 ? sv(M, m + 1, 0) : sv(M + 1, 0, 0));
    } else if (op == ">=") {
      add(O::Ge, sv(M, m, pa));
    } else if (op == "<") {
      add(O::Lt, sv(M, m, pa));
    } else if (op == ">") {
      if (p.specified == 3) add(O::Gt, sv(M, m, pa));
      else if (p.specified == 2) add(O::Ge, sv(M, m + 1, 0));
      else add(O::Ge, sv(M + 1, 0, 0));
    } else if (op == "<=") {
      if (p.specified == 3) add(O::Le, sv(M, m, pa));
      else if (p.specified == 2) add(O::Lt, sv(M, m + 1, 0));
      else add(O::Lt, sv(M + 1, 0, 0));
    } else {  // "=" or bare
      if (p.specified == 3) add(O::Eq, sv(M, m, pa));
      else if (p.specified == 2) { add(O::Ge, sv(M, m, 0)); add(O::Lt, sv(M, m + 1, 0)); }
      else { add(O::Ge, sv(M, 0, 0)); add(O::Lt, sv(M + 1, 0, 0)); }
    }
  }
  return r;
}

bool satisfies(const SemVer& v, const std::string& range) {
  auto r = parse_range(range);
  return r && r->matches(v);
}

}  // namespace mods
