// The Luau test suites of luau/tests, run inside a script-host sandbox (the package bundled exactly as a mod loads it), one runner
// case per Luau case. The Luau CLI runs the same files: `luau luau/tests/run_cli.luau`.
#include <cstdio>
#include <memory>
#include <string>

#include "legacy_npc/package.hpp"
#include "mini_test.hpp"

using namespace legacy_npc;

void register_luau_suites() {
  if (!script_host::ModSandbox::kLuauAvailable) {
    mt::registry().push_back({"LNPC-LUAU", "luau_suites", [] { throw mt::Skip("Luau VM not linked"); }, false, ""});
    return;
  }
  static std::unique_ptr<PackageHost> host;  // lives for the whole run
  PackageOptions opt;
  opt.with_tests = true;
  host = std::make_unique<PackageHost>(opt);
  host->clear_log();
  auto r = host->run("luau-tests", "LNPC_H.run()");
  if (r.status != script_host::RunStatus::Ok) {
    std::string why = r.error;
    mt::registry().push_back({"LNPC-LUAU", "luau_suites_run", [why] { throw mt::Failure("Luau suites did not run: " + why); }, false, ""});
    return;
  }
  int cases = 0;
  for (auto& rec : host->records("@case")) {
    if (rec.size() < 4) continue;
    bool ok = rec[1] == "PASS", skip = rec[1] == "SKIP";
    std::string tag = rec[2], name = rec[3], err = rec.size() > 4 ? rec[4] : "";
    mt::registry().push_back({tag, name, [ok, skip, err] { if (skip) throw mt::Skip(err); if (!ok) throw mt::Failure(err); }, false, ""});
    ++cases;
  }
  int expected = -1;
  for (auto& rec : host->records("@summary")) expected = std::atoi(rec[1].c_str()) + std::atoi(rec[2].c_str()) + std::atoi(rec[3].c_str());
  mt::registry().push_back({"LNPC-LUAU", "luau_suite_ran_every_case", [cases, expected] {
    if (cases != expected || cases == 0) throw mt::Failure("collected " + std::to_string(cases) + " cases, harness reported " + std::to_string(expected));
  }, false, ""});
}
