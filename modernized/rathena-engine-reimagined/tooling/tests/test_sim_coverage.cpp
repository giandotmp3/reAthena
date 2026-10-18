// The BR-ID coverage report. Hermetic cases use a small rule file, a scenario directory and a mock engine
// tree written on the fly; the last ones read the real spec documents and the real scenarios.
// Tags: BR-RULESET-119 (coverage keyed by BR-RULESET id) and TL-SIM-09x (tooling-local).
#include <cstring>
#include <fstream>
#include <sstream>

#include "sim_fixture.hpp"
#include "tooling/sim_cli.hpp"
#include "tooling/sim_coverage.hpp"

using namespace tooling::sim;
namespace fs = std::filesystem;

namespace {

// Minimal JSON well-formedness check (the report must be machine readable).
struct Json {
  const std::string& s;
  size_t i = 0;
  void ws() { while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i; }
  bool lit(const char* w) { size_t n = std::strlen(w); if (s.compare(i, n, w) == 0) { i += n; return true; } return false; }
  bool str() {
    if (s[i] != '"') return false;
    for (++i; i < s.size(); ++i) {
      if (s[i] == '\\') ++i;
      else if (s[i] == '"') { ++i; return true; }
      else if (static_cast<unsigned char>(s[i]) < 0x20) return false;
    }
    return false;
  }
  bool value() {
    ws();
    if (i >= s.size()) return false;
    if (s[i] == '{') {
      ++i; ws();
      if (s[i] == '}') { ++i; return true; }
      for (;;) {
        ws(); if (!str()) return false; ws();
        if (s[i++] != ':') return false;
        if (!value()) return false;
        ws();
        if (s[i] == ',') { ++i; continue; }
        return s[i++] == '}';
      }
    }
    if (s[i] == '[') {
      ++i; ws();
      if (s[i] == ']') { ++i; return true; }
      for (;;) {
        if (!value()) return false;
        ws();
        if (s[i] == ',') { ++i; continue; }
        return s[i++] == ']';
      }
    }
    if (s[i] == '"') return str();
    if (lit("true") || lit("false") || lit("null")) return true;
    size_t st = i;
    while (i < s.size() && (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '-' || s[i] == '.' || s[i] == 'e' || s[i] == 'E' || s[i] == '+')) ++i;
    return i > st;
  }
  bool document() { bool ok = value(); ws(); return ok && i == s.size(); }
};
bool valid_utf8(const std::string& s) {
  for (size_t i = 0; i < s.size();) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    size_t n = c < 0x80 ? 1 : (c >= 0xc2 && c <= 0xdf ? 2 : (c >= 0xe0 && c <= 0xef ? 3 : (c >= 0xf0 && c <= 0xf4 ? 4 : 0)));
    if (n == 0 || i + n > s.size()) return false;
    for (size_t k = 1; k < n; ++k)
      if ((static_cast<unsigned char>(s[i + k]) & 0xc0) != 0x80) return false;
    i += n;
  }
  return true;
}
bool valid_json(const std::string& s) { Json j{s}; return valid_utf8(s) && j.document(); }

// rule file with 4 rules, a scenario dir with a passing and a failing scenario
struct Mock {
  simtest::TempDir d{"coverage"};
  Mock() {
    d.write("rules.md", "# rules\n| ID | Rule |\n|---|---|\n| BR-ZZ-001 | passing rule | x | P0 |\n| BR-ZZ-002 | failing rule | x | P0 |\n"
                        "| BR-ZZ-003 | only tagged in a test | x | P0 |\n| BR-ZZ-004 | pending in a test | x | P0 |\n| BR-ZZ-005 | nobody tests this | x | P1 |\n"
                        "A prose mention of BR-ZZ-005 is not a rule row.\n");
    d.write("scenarios/good.luau", "--@rule BR-ZZ-001\n--@mods rathena.base\nsim.assert_true(true)\n");
    d.write("scenarios/bad.luau", "--@rule BR-ZZ-002 TL-LOCAL-1\n--@mods rathena.base\nsim.fail('this scenario fails')\n");
    d.write("tree/unit-a/tests/t.cpp", "TEST(\"BR-ZZ-003\", only_tagged) {}\nPENDING_TEST(\"BR-ZZ-004\", later) {}\nTEST(\"BR-ZZ-099\", not_in_the_spec) {}\n");
    d.write("tree/unit-b/build/skipped.cpp", "TEST(\"BR-ZZ-005\", in_a_build_dir) {}\n");
  }
  CoverageOptions options(bool run = true, bool scan = true) const {
    CoverageOptions o;
    o.rule_files = {d.path / "rules.md"};
    o.scenario_dirs = {d.path / "scenarios"};
    o.engine_root = d.path / "tree";
    o.run_scenarios = run;
    o.static_scan = scan;
    return o;
  }
};

}  // namespace

TEST("BR-RULESET-119", coverage_status_comes_from_running_the_scenarios_and_unknown_ids_are_not_rules) {
  simtest::need_luau();
  Mock m;
  CoverageResult r = build_coverage(m.options());
  CHECK(r.rules.size() == 5);  // only table rows define rules, not prose
  CHECK(r.rules.at("BR-ZZ-001").status() == "passing");
  CHECK(r.rules.at("BR-ZZ-002").status() == "failing");    // the scenario ran and failed
  CHECK(r.rules.at("BR-ZZ-003").status() == "tagged");     // static tag only
  CHECK(r.rules.at("BR-ZZ-004").status() == "pending");    // PENDING_TEST
  CHECK(r.rules.at("BR-ZZ-005").status() == "uncovered");  // the only mention is in a build dir, which is not scanned
  CHECK(r.rules.at("BR-ZZ-001").evidence.size() == 1 && r.rules.at("BR-ZZ-001").evidence[0].kind == "scenario");
  CHECK(r.rules.at("BR-ZZ-001").evidence[0].name == "good");
  CHECK(r.other_tags.count("BR-ZZ-099") && r.other_tags.count("TL-LOCAL-1"));
  // without running, scenarios only count as tagged
  CoverageResult st = build_coverage(m.options(false));
  CHECK(st.rules.at("BR-ZZ-001").status() == "tagged" && st.rules.at("BR-ZZ-002").status() == "tagged");
}

TEST("BR-RULESET-119", coverage_json_is_valid_and_the_text_summary_names_the_uncovered_rules) {
  simtest::need_luau();
  Mock m;
  CoverageResult r = build_coverage(m.options());
  const std::string js = to_json(to_val(r), 2);
  CHECK(valid_json(js));
  CHECK(js.find("\"id\": \"BR-ZZ-001\"") != std::string::npos && js.find("\"uncovered\": [") != std::string::npos);
  CHECK(js.find("\"BR-ZZ-005\"") != std::string::npos);
  const std::string txt = render_text(r);
  CHECK(txt.find("5 rules") != std::string::npos);
  CHECK(txt.find("UNCOVERED (1): BR-ZZ-005") != std::string::npos);
  CHECK(txt.find("BR-ZZ-001  passing") != std::string::npos && txt.find("scenario  good") != std::string::npos);
}

TEST("BR-RULESET-119", test_binary_output_is_parsed_into_passing_pending_and_failing_evidence) {
  const std::string out =
      "PASS   BR-ZZ-001 first_test\nFAIL   BR-ZZ-002 second_test: boom\nXFAIL  BR-ZZ-004 third (not implemented)\n"
      "XPASS  BR-ZZ-003 fourth: implemented\nSKIP   BR-ZZ-005 fifth (no vm)\nBR-ZZ-001 ACTIVE  listed\nBR-ZZ-002 PENDING listed2\nnoise line\n";
  auto ev = parse_test_output(out, "bin");
  CHECK(ev.size() == 7);
  CHECK(ev[0].first == "BR-ZZ-001" && ev[0].second.status == "passing" && ev[0].second.name == "first_test");
  CHECK(ev[1].second.status == "failing" && ev[2].second.status == "pending" && ev[3].second.status == "failing");
  CHECK(ev[4].second.status == "tagged" && ev[5].second.status == "tagged" && ev[6].second.status == "pending");
  Mock m;
  m.d.write("out.txt", out);
  auto o = m.options(false, false);
  o.test_outputs = {m.d.path / "out.txt"};
  CoverageResult r = build_coverage(o);
  CHECK(r.rules.at("BR-ZZ-001").status() == "passing");
  CHECK(r.rules.at("BR-ZZ-002").status() == "failing");  // a failing test beats a pending listing
  CHECK(r.rules.at("BR-ZZ-005").status() == "tagged");
}

TEST("BR-RULESET-119", the_cli_exits_non_zero_only_when_require_is_given_and_not_met) {
  simtest::need_luau();
  Mock m;
  auto cli = [&](std::vector<std::string> args, std::string* out = nullptr, std::string* err = nullptr) {
    std::vector<std::string> full = {"rathena-sim", "coverage", "--rules", (m.d.path / "rules.md").string(), "--scenarios",
                                     (m.d.path / "scenarios").string()};
    full.insert(full.end(), args.begin(), args.end());
    std::vector<char*> argv;
    for (auto& s : full) argv.push_back(s.data());
    std::ostringstream o, e;
    int rc = cli_main(static_cast<int>(argv.size()), argv.data(), o, e);
    if (out) *out = o.str();
    if (err) *err = e.str();
    return rc;
  };
  std::string out, err;
  // uncovered and failing rules exist, and the exit status is still 0 without --require
  CHECK(cli({"--no-static"}, &out, &err) == 0);
  CHECK(out.find("UNCOVERED") != std::string::npos && err.empty());
  CHECK(cli({"--no-static", "--require", "BR-ZZ-001"}) == 0);
  CHECK(cli({"--no-static", "--require", "BR-ZZ-002"}, &out, &err) == 1);   // failing
  CHECK(err.find("BR-ZZ-002") != std::string::npos);
  CHECK(cli({"--no-static", "--require", "BR-ZZ-001..002"}, &out, &err) == 1);  // a range
  CHECK(cli({"--no-static", "--require", "BR-ZZ"}) == 1);                     // a family prefix
  CHECK(cli({"--no-static", "--require", "all"}) == 1);
  CHECK(cli({"--no-static", "--require", "BR-ZZ-001", "--require", "BR-ZZ-002", "--require-level", "covered"}) == 0);  // covered = any evidence
  CHECK(cli({"--no-static", "--require", "BR-ZZ-005", "--require-level", "covered"}) == 1);
  CHECK(cli({"--no-static", "--require", "BR-NOPE-1"}, &out, &err) == 2);     // unknown rule: a usage error, not a pass
  CHECK(err.find("unknown rule or family") != std::string::npos);
  // --json prints a document, --json-out writes it next to the text summary
  CHECK(cli({"--no-static", "--json"}, &out) == 0 && valid_json(out));
  CHECK(cli({"--no-static", "--json-out", (m.d.path / "cov.json").string()}, &out) == 0);
  std::ifstream f(m.d.path / "cov.json");
  std::stringstream ss;
  ss << f.rdbuf();
  CHECK(valid_json(ss.str()) && out.find("UNCOVERED") != std::string::npos);
}

TEST("BR-RULESET-119", the_universe_is_read_from_the_spec_documents_and_the_shipped_scenarios_cover_rules) {
  simtest::need_luau();
  CoverageOptions o;
  o.static_scan = false;
  CoverageResult r = build_coverage(o);
  CHECK(r.family_sizes.at("BR-RULESET") == 119);   // BR-RULESET-001..119 of BR_RULESET.md
  CHECK(r.family_sizes.at("BR-LOGIN") == 39 && r.family_sizes.at("BR-CHAR") == 55 && r.family_sizes.at("BR-MAP") == 61);
  CHECK(r.family_sizes.at("BR-INTER") == 19 && r.family_sizes.at("BR-WEB") == 17);
  CHECK(r.rules.size() == 310);
  for (const char* id : {"BR-RULESET-020", "BR-RULESET-115", "BR-RULESET-116", "BR-MAP-057", "BR-MAP-059", "BR-MAP-060"})
    if (r.rules.at(id).status() != "passing") throw CheckFailure(std::string(id) + " is " + r.rules.at(id).status());
  CHECK(r.rules.at("BR-RULESET-020").evidence.size() >= 3);  // several scenarios cover it
  CHECK(r.rules.at("BR-WEB-001").status() == "uncovered");   // the web API has no test in this unit
  CHECK(!r.rules.at("BR-RULESET-001").title.empty());
  const std::string js = to_json(to_val(r), 2);
  CHECK(valid_json(js));
}

TEST("TL-SIM-090", static_scan_finds_tags_in_other_units_tests_and_marks_pending_ones) {
  Mock m;
  CoverageOptions o = m.options(false, true);
  CoverageResult r = build_coverage(o);
  auto& e3 = r.rules.at("BR-ZZ-003").evidence;
  CHECK(e3.size() == 1 && e3[0].kind == "static" && e3[0].source == "unit-a/tests/t.cpp" && e3[0].status == "tagged");
  CHECK(r.rules.at("BR-ZZ-004").evidence[0].status == "pending");
}
