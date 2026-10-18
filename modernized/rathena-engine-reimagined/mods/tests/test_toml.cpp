// TOML subset parser/writer. Tags MOD-TOML-*.
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>

#include "harness.hpp"
#include "mods/toml.hpp"

namespace fs = std::filesystem;
using namespace mods;
using toml::Value;

static std::string slurp_file(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  std::ostringstream o;
  o << in.rdbuf();
  return o.str();
}

static toml::ParseError must_fail(const std::string& text) {
  try { toml::parse(text, "t.toml"); }
  catch (const toml::ParseError& e) { return e; }
  throw CheckFailure("expected a ParseError for: " + text);
}

static const Value& at(const Value& v, const char* key) {
  const Value* f = v.find(key);
  if (!f) throw CheckFailure(std::string("missing key ") + key);
  return *f;
}

TEST("MOD-TOML-001", scalars_strings_ints_bools_floats) {
  Value v = toml::parse("a = \"x\"\nb = 42\nc = true\nd = false\ne = -7\nf = 0xFF\ng = 1_000\nh = 1.5\ni = 'lit\\n'\nj = +3\n");
  CHECK(at(v, "a").s == "x");
  CHECK(at(v, "b").i == 42 && at(v, "b").is_int());
  CHECK(at(v, "c").b && !at(v, "d").b && at(v, "c").is_bool());
  CHECK(at(v, "e").i == -7);
  CHECK(at(v, "f").i == 255);
  CHECK(at(v, "g").i == 1000);
  CHECK(at(v, "h").type == Value::Type::Float && at(v, "h").d == 1.5);
  CHECK(at(v, "i").s == "lit\\n");  // literal strings do not unescape
  CHECK(at(v, "j").i == 3);
}

TEST("MOD-TOML-002", string_escapes_and_multiline) {
  Value v = toml::parse(
      "a = \"t\\tq\\\"b\\\\ \\u00e9 \\U0001F600\"\n"
      "b = \"\"\"\nline1\nline2 \\\n   continued\"\"\"\n"
      "c = '''\nraw \\n\n'''\n"
      "d = \"\"\"a\"\"\"\"\n");
  CHECK(at(v, "a").s == "t\tq\"b\\ \xC3\xA9 \xF0\x9F\x98\x80");
  CHECK(at(v, "b").s == "line1\nline2 continued");
  CHECK(at(v, "c").s == "raw \\n\n");
  CHECK(at(v, "d").s == "a\"");
}

TEST("MOD-TOML-003", unknown_escape_is_rejected_with_line_and_column) {
  auto e = must_fail("a = 1\nb = \"x\\qy\"\n");
  CHECK(e.line() == 2 && e.column() == 7);
  CHECK(std::string(e.what()).find("unknown escape sequence '\\q'") != std::string::npos);
  CHECK(std::string(e.what()).find("t.toml:2:7") == 0);
  must_fail("a = \"\\u12\"\n");               // short \u
  must_fail("a = \"\\uD800\"\n");             // surrogate
  must_fail("a = \"\\U00110000\"\n");         // beyond Unicode
  must_fail("a = \"\\x41\"\n");               // not a TOML escape
}

TEST("MOD-TOML-004", duplicate_keys_and_tables_are_rejected) {
  auto e = must_fail("a = 1\nb = 2\na = 3\n");
  CHECK(e.line() == 3 && e.column() == 1);
  CHECK(std::string(e.what()).find("duplicate key 'a' (first defined at line 1)") != std::string::npos);
  must_fail("[t]\nx = 1\n[t]\ny = 2\n");              // table twice
  must_fail("[a]\nb = 1\n[a.b]\n");                    // key then table
  must_fail("a.b = 1\n[a]\n");                          // dotted-defined table reopened
  must_fail("a = { x = 1 }\n[a.y]\n");                  // inline table extended
  must_fail("a = { x = 1, x = 2 }\n");                  // duplicate inside inline table
  must_fail("a = 1\n[a]\n");
  must_fail("[[a]]\n[a]\n");
  must_fail("a = []\n[[a]]\n");                         // static array is not an array of tables
}

TEST("MOD-TOML-005", tables_dotted_keys_and_arrays_of_tables) {
  Value v = toml::parse(
      "top = 1\n"
      "x.y.z = 2\n"
      "[a.b]\nk = 3\n[a]\nj = 4\n"
      "[[items]]\nn = 1\n[items.sub]\nq = true\n[[items]]\nn = 2\n"
      "[\"quoted key\".\"b.c\"]\nv = 5\n");
  CHECK(at(at(at(v, "x"), "y"), "z").i == 2);
  CHECK(at(at(v, "a"), "j").i == 4 && at(at(at(v, "a"), "b"), "k").i == 3);
  const Value& items = at(v, "items");
  CHECK(items.is_array() && items.a.size() == 2);
  CHECK(at(items.a[0], "n").i == 1 && at(at(items.a[0], "sub"), "q").b);
  CHECK(at(items.a[1], "n").i == 2 && items.a[1].find("sub") == nullptr);
  CHECK(at(at(at(v, "quoted key"), "b.c"), "v").i == 5);
}

TEST("MOD-TOML-006", inline_tables_and_arrays) {
  Value v = toml::parse(
      "d = { \"rathena.base\" = \"^1\", other = \"^0.1\" }\n"
      "e = {}\n"
      "n = { a.b = 1, c = [1, 2, { z = 3 }] }\n"
      "arr = [\n  \"a\",   # comment\n  \"b\",\n  [1, 2],\n  { x = 1 },\n]\n"
      "mixed = [1, \"two\", 3.5, true]\n");
  CHECK(at(at(v, "d"), "rathena.base").s == "^1");
  CHECK(at(v, "e").is_table() && at(v, "e").t.empty());
  CHECK(at(at(at(v, "n"), "a"), "b").i == 1);
  CHECK(at(at(v, "n"), "c").a[2].find("z")->i == 3);
  CHECK(at(v, "arr").a.size() == 4 && at(v, "mixed").a.size() == 4);
  must_fail("a = { x = 1,\n y = 2 }\n");     // newline in an inline table
  must_fail("a = { x = 1, }\n");              // trailing comma (TOML 1.0)
  must_fail("a = [1, 2\n");                   // unterminated array
  must_fail("a = [1 2]\n");
  must_fail("a = { x = 1 y = 2 }\n");
}

TEST("MOD-TOML-007", numbers_are_checked) {
  for (const char* bad : {"a = 01\n", "a = 1__0\n", "a = _1\n", "a = 1_\n", "a = 0x\n", "a = 0xG\n", "a = +0x1\n",
                          "a = 9223372036854775808\n", "a = 1.\n", "a = .5\n", "a = 1e\n", "a = --1\n", "a = 0b102\n",
                          "a = 1e999\n", "a = 12abc\n", "a = tru\n", "a = \n"})
    must_fail(bad);
  Value v = toml::parse("a = -9223372036854775808\nb = 9223372036854775807\nc = 0o17\nd = 0b101\ne = 6.02e23\nf = inf\ng = -inf\nh = nan\n");
  CHECK(at(v, "a").i == INT64_MIN && at(v, "b").i == INT64_MAX && at(v, "c").i == 15 && at(v, "d").i == 5);
  CHECK(at(v, "e").d == 6.02e23);
  CHECK(at(v, "f").d > 1e308 && at(v, "g").d < -1e308 && at(v, "h").d != at(v, "h").d);
}

TEST("MOD-TOML-008", malformed_input_reports_line_and_column) {
  struct Case { const char* text; unsigned line, col; const char* needle; };
  const Case cases[] = {
      {"a = 1 2\n", 1, 7, "expected end of line"},
      {"= 1\n", 1, 1, "expected a key"},
      {"a 1\n", 1, 3, "expected '='"},
      {"a = \n", 1, 5, "expected a value"},
      {"[t\nx = 1\n", 1, 3, "expected ']'"},
      {"[[t]\n", 1, 4, "expected ']]'"},
      {"\n\nk = \"unterminated\n", 3, 18, "newline in a single-line string"},
      {"s = 'abc", 1, 5, "unterminated string"},
      {"a = 1979-05-27\n", 1, 5, "date/time"},
      {"a = 07:32:00\n", 1, 5, "date/time"},
      {"ok = 1\nbad key = 2\n", 2, 5, "expected '='"},
      {"a = \"x\"\r\nb = \r2\r\n", 2, 5, "bare carriage return"},
      {"a = \"\x01\"\n", 1, 6, "control character 0x01"},
      {"a = \"\xFF\"\n", 1, 6, "invalid UTF-8"},
      {"a = \"\xC0\xAF\"\n", 1, 6, "invalid UTF-8"},
      {"a = \"\xED\xA0\x80\"\n", 1, 6, "invalid UTF-8"},
      {"a = @\n", 1, 5, "unexpected '@'"},
      {"a = 1 # ok\n# c\nb = [1,\n  2 x]\n", 4, 5, "expected ',' or ']'"},
      {"a = '''\nnever closed\n", 1, 5, "unterminated string"},
      {"x = \"\"\"abc\"\"\"\"\"\"\n", 1, 16, "too many quotes"},
  };
  for (const auto& c : cases) {
    auto e = must_fail(c.text);
    if (e.line() != c.line || e.column() != c.col || std::string(e.message()).find(c.needle) == std::string::npos) {
      std::ostringstream o;
      o << "case [" << c.text << "] got " << e.what() << " expected " << c.line << ":" << c.col << " '" << c.needle << "'";
      throw CheckFailure(o.str());
    }
  }
}

TEST("MOD-TOML-009", limits_nesting_and_size) {
  std::string deep(toml::kMaxDepth + 2, '[');
  deep = "a = " + deep + std::string(toml::kMaxDepth + 2, ']') + "\n";
  must_fail(deep);
  std::string ok = "a = " + std::string(toml::kMaxDepth - 2, '[') + std::string(toml::kMaxDepth - 2, ']') + "\n";
  toml::parse(ok);
  std::string big(toml::kMaxBytes + 1, ' ');
  must_fail(big);
  toml::parse("a = 1\n\xEF\xBB\xBF" == std::string() ? "" : "\xEF\xBB\xBF" "a = 1\n");  // leading BOM accepted
  Value crlf = toml::parse("a = 1\r\n[t]\r\nb = \"\"\"x\r\ny\"\"\"\r\n");
  CHECK(at(at(crlf, "t"), "b").s == "x\ny");
}

TEST("MOD-TOML-010", value_positions_are_recorded) {
  Value v = toml::parse("a = 1\n\n[t]\n  name = \"x\"\n");
  CHECK(at(v, "a").line == 1 && at(v, "a").col == 5);
  CHECK(at(at(v, "t"), "name").line == 4 && at(at(v, "t"), "name").col == 10);
  CHECK(at(v, "t").line == 3);
}

// ---- round trip --------------------------------------------------------------
static std::vector<fs::path> real_toml_files() {
  std::vector<fs::path> out;
  fs::path root = MODS_ENGINE_ROOT;
  for (auto it = fs::recursive_directory_iterator(root); it != fs::recursive_directory_iterator(); ++it) {
    const auto& p = it->path();
    if (it->is_directory()) {
      auto n = p.filename().string();
      if (n == "build" || n == ".git" || n == "node_modules" || n == "out" || n.rfind("build-", 0) == 0) it.disable_recursion_pending();
      continue;
    }
    if (p.extension() == ".toml") out.push_back(p);
  }
  std::sort(out.begin(), out.end());
  return out;
}

TEST("MOD-TOML-011", all_real_tomls_in_the_engine_tree_parse_and_round_trip) {
  auto files = real_toml_files();
  CHECK(files.size() >= 10);  // 3 mod.toml + base + package.toml + config examples
  std::size_t manifests = 0;
  for (const auto& f : files) {
    std::string text = slurp_file(f);
    Value a = toml::parse(text, f.string());
    std::string w = toml::write(a);
    Value b = toml::parse(w, "written");
    if (!(a == b)) throw CheckFailure("round trip changed " + f.string() + "\n" + w);
    CHECK(toml::write(b) == w);  // canonical text is a fixed point
    if (f.filename() == "mod.toml") ++manifests;
  }
  CHECK(manifests >= 4);  // legacy-npc, ruleset-prerenewal, ruleset-renewal, rathena.base
}

TEST("MOD-TOML-012", third_party_tomls_parse_or_fail_cleanly) {
  // Not required to parse (they use features outside the subset), but must
  // never crash or throw anything but ParseError.
  fs::path root = fs::path(MODS_ENGINE_ROOT) / ".." / ".." / "3rdparty";
  if (!fs::is_directory(root)) SKIP_TEST("3rdparty not present");
  std::size_t n = 0;
  for (const auto& e : fs::recursive_directory_iterator(root)) {
    if (!e.is_regular_file() || e.path().extension() != ".toml") continue;
    ++n;
    try { Value a = toml::parse(slurp_file(e.path())); Value b = toml::parse(toml::write(a)); CHECK(a == b); }
    catch (const toml::ParseError&) {}
  }
  CHECK(n > 0);
}

static const char* const kCorpus[] = {
    "", "\n", "# only a comment\n", "a=1", "a = 1 # trailing\n", "[a]\n[a.b]\n[a.b.c]\nx = 1\n",
    "k = [ ]\nj = [[]]\nm = [[1], [2, 3]]\n", "\"\" = 1\n", "'a b' = 2\n", "a.\"b c\".d = true\n",
    "s = \"\"\"\"\"\"\ne = ''''''\n", "a = \"\\u0000\"\n", "x = 1e5\ny = 1_0.5_0e1_0\n",
    "[[a]]\n[[a]]\n[a.b]\n", "t = { a = { b = { c = [ { d = 1 } ] } } }\n",
    "a = '\"quoted\"'\nb = \"'single'\"\n", "key = \"\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80\"\n",
};

TEST("MOD-TOML-013", corpus_round_trips_and_writer_output_is_valid) {
  for (const char* c : kCorpus) {
    Value a = toml::parse(c);
    Value b = toml::parse(toml::write(a));
    if (!(a == b)) throw CheckFailure(std::string("round trip failed for: ") + c);
  }
  // programmatic: awkward keys and strings survive the writer
  Value t = Value::make_table();
  t.set("plain", Value::make_string("a\"b\\c\n\t\x01\x7f z"));
  t.set("we ird.key", Value::make_int(1));
  t.set("", Value::make_bool(true));
  Value nested = Value::make_table();
  nested.set("x y", Value::make_string(""));
  t.set("n.t", nested);
  Value arr = Value::make_array();
  arr.a.push_back(Value::make_table());
  arr.a.push_back(Value::make_array());
  t.set("arr", arr);
  CHECK(toml::parse(toml::write(t)) == t);
}

TEST("MOD-TOML-014", fuzz_mutations_never_crash_and_survivors_round_trip) {
  std::vector<std::string> seeds;
  for (const auto& f : real_toml_files()) seeds.push_back(slurp_file(f));
  for (const char* c : kCorpus) seeds.push_back(c);
  std::mt19937_64 rng(0xC0FFEE);  // deterministic
  const char alphabet[] = "[]{}=\"'\\#.,\n\r \t0aZ_-+:\xC3\xA9";
  std::size_t parsed = 0, rejected = 0;
  for (int iter = 0; iter < 30000; ++iter) {
    std::string s = seeds[rng() % seeds.size()];
    int edits = 1 + static_cast<int>(rng() % 4);
    for (int k = 0; k < edits && !s.empty(); ++k) {
      std::size_t pos = rng() % s.size();
      switch (rng() % 5) {
        case 0: s.erase(pos, 1 + rng() % 3); break;
        case 1: s.insert(pos, 1, alphabet[rng() % (sizeof alphabet - 1)]); break;
        case 2: s[pos] = alphabet[rng() % (sizeof alphabet - 1)]; break;
        case 3: s.resize(pos); break;
        default: s.insert(pos, s.substr(rng() % s.size(), 1 + rng() % 8)); break;
      }
    }
    try {
      Value a = toml::parse(s);
      ++parsed;
      Value b = toml::parse(toml::write(a));
      if (!(a == b)) throw CheckFailure("mutant round trip failed:\n" + s);
    } catch (const toml::ParseError& e) {
      ++rejected;
      if (e.line() == 0 || e.column() == 0) throw CheckFailure("error without a position for:\n" + s);
    }
  }
  CHECK(parsed > 1000 && rejected > 1000);  // the mutator exercises both outcomes
}

TEST("MOD-TOML-015", every_prefix_of_every_real_toml_is_handled) {
  for (const auto& f : real_toml_files()) {
    std::string text = slurp_file(f);
    for (std::size_t n = 0; n <= text.size(); n += (text.size() > 600 ? 7 : 1)) {
      try { toml::parse(text.substr(0, n)); }
      catch (const toml::ParseError&) {}
    }
  }
}
