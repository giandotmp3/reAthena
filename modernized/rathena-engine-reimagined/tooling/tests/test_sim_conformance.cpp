// `rathena-sim conformance`: the golden suites of the ruleset packages, run through the sim's script host
// with the real Luau (not the lupa runner), the package loaded by the real mod loader.
// Tag BR-RULESET-119 (verification keyed by BR-RULESET id); TL-SIM-08x are tooling-local.
#include <fstream>
#include <sstream>

#include "sim_fixture.hpp"
#include "tooling/sim_cli.hpp"
#include "tooling/sim_conformance.hpp"
#include "tooling/sim_coverage.hpp"

using namespace tooling::sim;
namespace fs = std::filesystem;

namespace {

ConformanceReport run(const std::string& rs, bool suite, size_t subset = 0, const fs::path& root = {}) {
  simtest::need_luau();
  ConformanceOptions o;
  o.ruleset = rs;
  o.suite = suite;
  o.subset_per_category = subset;
  o.engine_root = root;
  return run_conformance(o);
}

void copy_package(const fs::path& from, const fs::path& to, bool with_build = true) {
  fs::create_directories(to);
  for (auto& e : fs::directory_iterator(from)) {
    const std::string n = e.path().filename().string();
    if (n == "build" || n == "docs" || n == "tools" || n == "__pycache__") continue;
    fs::copy(e.path(), to / n, fs::copy_options::recursive);
  }
  fs::remove_all(to / "conformance" / "golden_gen");
  // the recorded cases are expanded into build/ (git-ignored in the unit); take the unit's copy
  if (!with_build) return;
  fs::create_directories(to / "build");
  for (auto& e : fs::directory_iterator(from / "build"))
    if (e.path().filename().string().rfind("golden_", 0) == 0) fs::copy_file(e.path(), to / "build" / e.path().filename());
}

}  // namespace

TEST("BR-RULESET-119", prerenewal_golden_cases_all_match_the_real_legacy_code_through_the_sim_host) {
  ConformanceReport r = run("prerenewal", true);
  if (!r.error.empty()) throw CheckFailure(r.error);
  CHECK(r.golden_total == 5424 && r.golden_matched == 5424);   // the package's recorded legacy cases
  CHECK(r.stage_mismatch == 0 && r.mismatch_classes.empty());
  CHECK(r.categories.size() >= 30);
  CHECK(r.load_order.size() == 2 && r.load_order[0] == "rathena.base" && r.load_order[1] == "ruleset.prerenewal");
  CHECK(r.generation == 1);
  // the package suite under the sim host: 106 pass / 76 expected-failure (96/86 before the status layer converted ten pending rules)
  CHECK(r.suite_ran && r.suite_pass == 106 && r.suite_xfail == 76 && r.suite_fail == 0 && r.suite_xpass == 0);
  CHECK(r.ok());
  std::printf("      prerenewal: %llu/%llu golden, suite %llu pass / %llu xfail, load %.0f ms, replay %.0f ms (%.1f us/case)\n",
              static_cast<unsigned long long>(r.golden_matched), static_cast<unsigned long long>(r.golden_total),
              static_cast<unsigned long long>(r.suite_pass), static_cast<unsigned long long>(r.suite_xfail), r.load_ms, r.replay_ms,
              r.replay_ms * 1000.0 / static_cast<double>(r.golden_total));
}

TEST("BR-RULESET-119", renewal_golden_cases_all_match_the_real_legacy_code_through_the_sim_host) {
  ConformanceReport r = run("renewal", true);
  if (!r.error.empty()) throw CheckFailure(r.error);
  CHECK(r.golden_total == 8858 && r.golden_matched == 8858);
  CHECK(r.stage_mismatch == 0);
  CHECK(r.load_order.size() == 2 && r.load_order[1] == "ruleset.renewal");
  CHECK(r.suite_ran && r.suite_pass == 111 && r.suite_fail == 0 && r.suite_xpass == 0);
  CHECK(r.ok());
  std::printf("      renewal: %llu/%llu golden, suite %llu pass, load %.0f ms, replay %.0f ms (%.1f us/case)\n",
              static_cast<unsigned long long>(r.golden_matched), static_cast<unsigned long long>(r.golden_total),
              static_cast<unsigned long long>(r.suite_pass), r.load_ms, r.replay_ms, r.replay_ms * 1000.0 / static_cast<double>(r.golden_total));
}

TEST("BR-RULESET-119", the_comparison_bites_a_mutated_package_is_reported_as_mismatching) {
  simtest::need_luau();
  simtest::TempDir d("mutant");
  const fs::path root = fs::path(TOOLING_SOURCE_DIR) / "..";
  fs::create_directories(d.path / "mods" / "base");
  fs::copy(root / "mods" / "base" / "rathena.base", d.path / "mods" / "base" / "rathena.base", fs::copy_options::recursive);
  copy_package(root / "ruleset-prerenewal", d.path / "ruleset-prerenewal");
  // an off-by-one in the skill ratio of a plain attack (the package's `100 + bonuses`)
  const fs::path weapon = d.path / "ruleset-prerenewal" / "scripts" / "damage" / "weapon.luau";
  std::ifstream in(weapon);
  std::stringstream ss;
  ss << in.rdbuf();
  std::string src = ss.str();
  const std::string needle = "local r = 100 + (src.kind";
  auto at = src.find(needle);
  CHECK(at != std::string::npos);
  src.replace(at, needle.size(), "local r = 101 + (src.kind");
  std::ofstream(weapon) << src;

  ConformanceReport clean = run("prerenewal", false, 3);
  CHECK(clean.error.empty() && clean.golden_matched == clean.golden_total && clean.golden_total > 100);
  ConformanceReport bad = run("prerenewal", false, 3, d.path);
  CHECK(bad.error.empty());
  CHECK(bad.golden_total == clean.golden_total && bad.golden_matched < bad.golden_total);
  CHECK(!bad.ok() && !bad.mismatch_classes.empty());
  CHECK(render_text(bad).find("MISMATCH") != std::string::npos);
}

TEST("TL-SIM-080", the_conformance_cli_prints_match_rates_and_exits_non_zero_on_failure) {
  simtest::need_luau();
  auto cli = [&](std::vector<std::string> args, std::string* out = nullptr, std::string* err = nullptr) {
    std::vector<std::string> full = {"rathena-sim", "conformance"};
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
  CHECK(cli({"--ruleset", "prerenewal", "--subset", "2", "--no-suite"}, &out) == 0);
  CHECK(out.find("golden weapon attack:") != std::string::npos && out.find("(100.00%)") != std::string::npos);
  CHECK(out.find("loaded by the mod loader in order: rathena.base ruleset.prerenewal") != std::string::npos);
  CHECK(cli({"--ruleset", "renewal", "--subset", "2", "--no-suite", "--json"}, &out) == 0);
  CHECK(out.find("\"match_percent\": 100") != std::string::npos && out.find("\"ruleset\": \"renewal\"") != std::string::npos);
  CHECK(cli({}, &out, &err) == 2 && err.find("--ruleset") != std::string::npos);
  CHECK(cli({"--ruleset", "bogus"}, &out, &err) == 1 && out.find("unknown ruleset") != std::string::npos);
}

TEST("BR-RULESET-119", the_coverage_report_takes_ruleset_suite_results_and_golden_replays_as_evidence) {
  simtest::need_luau();
  CoverageOptions o;
  o.run_scenarios = false;
  o.static_scan = false;
  o.run_suites = true;
  CoverageResult r = build_coverage(o);
  const auto& g = r.rules.at("BR-RULESET-119");
  CHECK(g.status() == "passing");
  int golden = 0;
  for (auto& e : g.evidence) golden += e.kind == "ruleset-golden" && e.status == "passing";
  CHECK(golden == 2);                                       // both packages' replays
  CHECK(r.rules.at("BR-RULESET-020").status() == "passing"); // from the packages' own tagged tests
  CHECK(r.rules.at("BR-RULESET-001").status() == "passing");
  int pending = 0;
  for (auto& [id, ri] : r.rules) pending += ri.status() == "pending" && id.rfind("BR-RULESET", 0) == 0;
  CHECK(pending > 40);                                       // the pre-renewal package's 86 expected failures
}

TEST("TL-SIM-081", a_clean_checkout_without_the_expanded_golden_data_is_expanded_into_the_sim_work_dir) {
  simtest::need_luau();
  simtest::TempDir d("clean");
  const fs::path root = fs::path(TOOLING_SOURCE_DIR) / "..";
  fs::create_directories(d.path / "mods" / "base");
  fs::copy(root / "mods" / "base" / "rathena.base", d.path / "mods" / "base" / "rathena.base", fs::copy_options::recursive);
  fs::create_directories(d.path / "ruleset-prerenewal");
  copy_package(root / "ruleset-prerenewal", d.path / "ruleset-prerenewal", false);
  fs::copy(root / "ruleset-prerenewal" / "tools", d.path / "ruleset-prerenewal" / "tools", fs::copy_options::recursive);
  CHECK(!fs::exists(d.path / "ruleset-prerenewal" / "build"));
  ConformanceReport r = run("prerenewal", false, 0, d.path);
  if (!r.error.empty()) throw CheckFailure(r.error);
  CHECK(r.golden_total == 5424 && r.golden_matched == 5424);  // the same recorded data, expanded by sim/expand_golden.py
  CHECK(!fs::exists(d.path / "ruleset-prerenewal" / "build"));  // and nothing was written into the package
}
