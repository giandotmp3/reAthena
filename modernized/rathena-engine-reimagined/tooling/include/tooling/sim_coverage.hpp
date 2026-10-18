#pragma once
// BR-ID coverage report (BR-RULESET-119). The universe of rules is read from the spec documents
// (rows `| BR-xxx-nnn | ... |` of analysis/rathena/BR_RULESET.md and AI_NATIVE_SPEC.md). Evidence
// that a rule is covered comes from:
//   scenario       a Luau scenario that declares the rule (`--@rule`); it is RUN, so the status is real
//   ruleset-suite  a test of a ruleset package's own suite executed under the sim host (opt-in)
//   test-output    PASS / FAIL / XFAIL / XPASS / SKIP lines of a test binary (tooling_tests, ...)
//   static         a rule id written in a test, conformance, rules or scenario file of any unit
//                  (status "tagged", or "pending" when the line / file says so). Not executed.
// Rule status: failing (any failing evidence) > passing > pending > tagged > uncovered.
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "tooling/sim_value.hpp"

namespace tooling::sim {

struct Evidence {
  std::string kind, source, name, status;  // status: passing | failing | pending | tagged
};
struct RuleInfo {
  std::string id, title, origin;
  std::vector<Evidence> evidence;
  std::string status() const;
};
struct CoverageOptions {
  std::filesystem::path repo_root;    // default: found above the engine tree
  std::filesystem::path engine_root;  // default: the tree this binary was built from
  std::vector<std::filesystem::path> rule_files;     // default: BR_RULESET.md, AI_NATIVE_SPEC.md, AI_NATIVE_SPEC_ENGINE.md
  std::vector<std::filesystem::path> scenario_dirs;  // default: tooling/scenarios
  bool run_scenarios = true;
  bool run_suites = false;            // also run both rulesets' own suites (and the golden replay) under the sim host
  std::vector<std::filesystem::path> test_outputs;   // saved output of test binaries
  std::vector<std::filesystem::path> test_binaries;  // executed; their output is parsed
  bool static_scan = true;
  uint64_t seed = 1;
};
struct CoverageResult {
  std::map<std::string, RuleInfo> rules;
  std::map<std::string, std::vector<Evidence>> other_tags;  // ids outside the universe (TL-*, EC-*, ...)
  std::vector<std::string> notes;                           // what was and was not executed
  std::map<std::string, size_t> family_sizes;
};

CoverageResult build_coverage(const CoverageOptions& opt);
Val to_val(const CoverageResult& r);
std::string render_text(const CoverageResult& r);

struct RequireOutcome {
  bool ok = true;
  std::vector<std::string> failing;  // required rules that do not reach the level
  std::string error;                 // unknown rule ids, bad spec
};
// specs: "all", a family prefix ("BR-RULESET"), an id, a range ("BR-RULESET-001..010").
// level: "passing" (default) or "covered" (any evidence at all).
RequireOutcome check_require(const CoverageResult& r, const std::vector<std::string>& specs, const std::string& level = "passing");

// Parses the PASS/FAIL/XFAIL/XPASS/SKIP (or `--list`) lines of a test binary's output.
std::vector<std::pair<std::string, Evidence>> parse_test_output(const std::string& text, const std::string& source);

}  // namespace tooling::sim
