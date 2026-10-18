#pragma once
// Conformance driver (BR-RULESET-119): runs the golden suites of the ruleset packages through the
// sim's script host. The package is loaded by the real mod loader (resolve, grants, bundle, one
// sandboxed Luau state per mod); the replay itself is the package's own golden_runner executed
// inside that state, against the data recorded from the real legacy battle code. Not the lupa runner.
#include <filesystem>
#include <string>
#include <vector>

#include "tooling/sim_value.hpp"
#include "tooling/sim_world.hpp"

namespace tooling::sim {

struct ConformanceOptions {
  std::string ruleset = "prerenewal";  // prerenewal | renewal
  std::filesystem::path engine_root;   // default: the tree this binary was built from
  bool golden = true;                  // replay the recorded legacy cases
  bool suite = true;                   // also run the package's own tests/main under the sim host
  size_t subset_per_category = 0;      // 0: every recorded case; N: N per category (+ every worked example)
  uint64_t seed = 1;
};

struct CategoryRate {
  std::string name;
  uint64_t matched = 0, total = 0;
  std::string first_mismatch;
};
struct SuiteTest {
  std::string status;  // PASS | FAIL | XFAIL | XPASS
  std::string rule, title;
};
struct ConformanceReport {
  std::string ruleset, mod_id, error;
  std::vector<std::string> load_order;  // as resolved by the mod loader
  uint64_t generation = 0;
  uint64_t golden_total = 0, golden_matched = 0, stage_mismatch = 0;
  std::vector<CategoryRate> categories;
  std::map<std::string, uint64_t> mismatch_classes;
  bool suite_ran = false;
  uint64_t suite_pass = 0, suite_xfail = 0, suite_fail = 0, suite_xpass = 0;
  std::vector<SuiteTest> suite_tests;
  double load_ms = 0, replay_ms = 0, suite_ms = 0;
  bool ok() const {
    return error.empty() && golden_matched == golden_total && (!suite_ran || (suite_fail == 0 && suite_xpass == 0));
  }
  double match_rate() const { return golden_total ? 100.0 * static_cast<double>(golden_matched) / static_cast<double>(golden_total) : 0.0; }
};

ConformanceReport run_conformance(const ConformanceOptions& opt);
Val to_val(const ConformanceReport& r);
std::string render_text(const ConformanceReport& r);

}  // namespace tooling::sim
