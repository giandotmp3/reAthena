#include "tooling/sim_cli.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>

#include "tooling/sim_conformance.hpp"
#include "tooling/sim_coverage.hpp"
#include "tooling/sim_player.hpp"
#include "tooling/sim_scenario.hpp"

namespace tooling::sim {

namespace fs = std::filesystem;

namespace {

const char* kHelp =
    "rathena-sim: headless test harness for the rathena engine (virtual clock, seeded RNG, SimPlayers, Luau scenarios)\n"
    "\n"
    "  rathena-sim run <scenario.luau | dir>... [--seed N] [--json] [--verbose]\n"
    "      run scenarios, each in a fresh sim built from its --@ directives\n"
    "  rathena-sim coverage [--json | --json-out FILE] [--suites] [--no-run] [--no-static]\n"
    "                       [--test-output FILE]... [--tests-bin PATH]... [--rules FILE]... [--scenarios DIR]...\n"
    "                       [--require SPEC[,SPEC...]] [--require-level passing|covered]\n"
    "      per BR rule id: which tests cover it, which rules have none. Exit status is 0 unless --require is given\n"
    "      and a required rule (id, family prefix, range BR-X-001..010, or 'all') does not reach the level.\n"
    "  rathena-sim conformance --ruleset prerenewal|renewal [--json] [--no-suite] [--no-golden] [--subset N]\n"
    "      replay the golden legacy cases of a ruleset package through the sim's script host\n"
    "  rathena-sim load [--players N] [--seconds S] [--seed N]\n"
    "      N random-walking, chatting SimPlayers; reports wall time per virtual second\n"
    "  rathena-sim mods\n"
    "      mods found under the engine tree\n";

bool take(std::vector<std::string>& a, const std::string& flag) {
  auto it = std::find(a.begin(), a.end(), flag);
  if (it == a.end()) return false;
  a.erase(it);
  return true;
}
std::vector<std::string> take_all(std::vector<std::string>& a, const std::string& flag) {
  std::vector<std::string> out;
  for (;;) {
    auto it = std::find(a.begin(), a.end(), flag);
    if (it == a.end()) break;
    if (it + 1 == a.end()) throw SimError(flag + " needs a value");
    out.push_back(*(it + 1));
    a.erase(it, it + 2);
  }
  return out;
}
std::string take_one(std::vector<std::string>& a, const std::string& flag, const std::string& dflt = "") {
  auto v = take_all(a, flag);
  return v.empty() ? dflt : v.back();
}
std::vector<std::string> commas(const std::vector<std::string>& in) {
  std::vector<std::string> out;
  for (auto& s : in) {
    std::stringstream ss(s);
    std::string tok;
    while (std::getline(ss, tok, ',')) if (!tok.empty()) out.push_back(tok);
  }
  return out;
}

int cmd_run(std::vector<std::string> a, std::ostream& out, std::ostream& err) {
  const bool json = take(a, "--json");
  const bool verbose = take(a, "--verbose") || take(a, "-v");
  const std::string seed = take_one(a, "--seed");
  std::vector<std::string> files;
  for (auto& p : a) {
    if (fs::is_directory(p)) for (auto& f : list_scenarios(p)) files.push_back(f);
    else files.push_back(p);
  }
  if (files.empty()) { err << "run: no scenario given\n"; return 2; }
  VArr results;
  int failed = 0;
  for (auto& f : files) {
    ScenarioResult r;
    try {
      ScenarioSpec spec = parse_scenario(f);
      if (!seed.empty()) spec.cfg.seed = std::stoull(seed);
      r = run_scenario_spec(spec);
    } catch (const std::exception& e) {
      r.name = fs::path(f).stem().string();
      r.message = e.what();
    }
    if (!r.passed) ++failed;
    if (json) {
      VMap m;
      m["name"] = r.name;
      m["file"] = f;
      m["passed"] = r.passed;
      m["assertions"] = static_cast<double>(r.assertions);
      m["virtual_ms"] = static_cast<double>(r.virtual_ms);
      m["wall_ms"] = r.wall_ms;
      VArr tags;
      for (auto& t : r.rule_tags) tags.push_back(t);
      m["rules"] = Val(std::move(tags));
      char h[24];
      std::snprintf(h, sizeof h, "%016llx", static_cast<unsigned long long>(r.trace_hash));
      m["trace_hash"] = std::string(h);
      m["main_rng_draws"] = static_cast<double>(r.main_rng_draws);
      if (!r.passed) m["message"] = r.message;
      results.push_back(Val(std::move(m)));
    } else {
      char b[200];
      std::snprintf(b, sizeof b, "%s  %-44s %3llu assertions, %7lld virtual ms, %7.0f wall ms", r.passed ? "PASS" : "FAIL", r.name.c_str(),
                    static_cast<unsigned long long>(r.assertions), static_cast<long long>(r.virtual_ms), r.wall_ms);
      out << b;
      if (!r.rule_tags.empty()) {
        out << "  [";
        for (size_t i = 0; i < r.rule_tags.size(); ++i) out << (i ? " " : "") << r.rule_tags[i];
        out << "]";
      }
      out << "\n";
      if (!r.passed) out << "      " << r.message << "\n";
      if (verbose)
        for (auto& l : r.output) out << "      | " << l << "\n";
    }
  }
  if (json) out << to_json(Val(std::move(results)), 2) << "\n";
  else out << "\n" << (files.size() - static_cast<size_t>(failed)) << " passed, " << failed << " failed\n";
  return failed ? 1 : 0;
}

int cmd_coverage(std::vector<std::string> a, std::ostream& out, std::ostream& err) {
  CoverageOptions opt;
  const bool json = take(a, "--json");
  const std::string json_out = take_one(a, "--json-out");
  opt.run_suites = take(a, "--suites");
  opt.run_scenarios = !take(a, "--no-run");
  opt.static_scan = !take(a, "--no-static");
  for (auto& f : take_all(a, "--test-output")) opt.test_outputs.push_back(f);
  for (auto& f : take_all(a, "--tests-bin")) opt.test_binaries.push_back(f);
  for (auto& f : take_all(a, "--rules")) opt.rule_files.push_back(f);
  for (auto& f : take_all(a, "--scenarios")) opt.scenario_dirs.push_back(f);
  const auto require = commas(take_all(a, "--require"));
  const std::string level = take_one(a, "--require-level", "passing");
  if (!a.empty()) { err << "coverage: unexpected argument " << a[0] << "\n"; return 2; }
  CoverageResult r = build_coverage(opt);
  if (!json_out.empty()) {
    std::ofstream f(json_out);
    f << to_json(to_val(r), 2) << "\n";
  }
  if (json) out << to_json(to_val(r), 2) << "\n";
  else out << render_text(r);
  if (require.empty()) return 0;  // the report never fails the run on its own
  RequireOutcome ro = check_require(r, require, level);
  if (!ro.error.empty()) { err << "coverage --require: " << ro.error << "\n"; return 2; }
  if (!ro.ok) {
    err << "coverage --require: " << ro.failing.size() << " required rule(s) are not " << level << ":";
    for (size_t i = 0; i < ro.failing.size() && i < 30; ++i) err << " " << ro.failing[i];
    if (ro.failing.size() > 30) err << " ...";
    err << "\n";
    return 1;
  }
  return 0;
}

int cmd_conformance(std::vector<std::string> a, std::ostream& out, std::ostream& err) {
  ConformanceOptions o;
  o.ruleset = take_one(a, "--ruleset");
  const bool json = take(a, "--json");
  o.suite = !take(a, "--no-suite");
  o.golden = !take(a, "--no-golden");
  const std::string sub = take_one(a, "--subset");
  if (!sub.empty()) o.subset_per_category = std::stoul(sub);
  const std::string seed = take_one(a, "--seed");
  if (!seed.empty()) o.seed = std::stoull(seed);
  if (o.ruleset.empty()) { err << "conformance: --ruleset prerenewal|renewal is required\n"; return 2; }
  if (!a.empty()) { err << "conformance: unexpected argument " << a[0] << "\n"; return 2; }
  ConformanceReport r = run_conformance(o);
  if (json) out << to_json(to_val(r), 2) << "\n";
  else out << render_text(r);
  return r.ok() ? 0 : 1;
}

int cmd_load(std::vector<std::string> a, std::ostream& out, std::ostream& err) {
  const int players = std::stoi(take_one(a, "--players", "200"));
  const double seconds = std::stod(take_one(a, "--seconds", "10"));
  const uint64_t seed = std::stoull(take_one(a, "--seed", "1"));
  if (!a.empty()) { err << "load: unexpected argument " << a[0] << "\n"; return 2; }
  LoadReport r = run_load(players, static_cast<int64_t>(seconds * 1000), seed);
  if (!r.error.empty()) { err << "load: " << r.error << "\n"; return 1; }
  char b[400];
  std::snprintf(b, sizeof b,
                "load: %d players, %.1f virtual s in %.3f wall s (setup %.3f s): %.4f wall s per virtual s\n"
                "      %llu steps, %llu walks, %llu chats, %llu server frames (%llu bytes) delivered, trace %016llx\n",
                r.players, r.virtual_s, r.wall_s, r.setup_wall_s, r.wall_per_virtual_s, static_cast<unsigned long long>(r.steps),
                static_cast<unsigned long long>(r.walks), static_cast<unsigned long long>(r.chats), static_cast<unsigned long long>(r.frames_received),
                static_cast<unsigned long long>(r.bytes_received), static_cast<unsigned long long>(r.trace_hash));
  out << b;
  return 0;
}

int cmd_mods(std::ostream& out) {
  const fs::path root = fs::weakly_canonical(fs::path(TOOLING_SOURCE_DIR) / "..");
  // discovery is internal to Sim; list through a manifest scan of the same roots
  std::vector<fs::path> hits;
  std::error_code ec;
  for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec); it != fs::recursive_directory_iterator(); ++it) {
    const std::string n = it->path().filename().string();
    if (it->is_directory(ec) && (n == "build" || n == "out" || n == "3rdparty" || n == ".git" || n == "__pycache__" || n == "tests" || n == "m0")) {
      it.disable_recursion_pending();
      continue;
    }
    if (it->is_regular_file(ec) && n == "mod.toml") hits.push_back(it->path());
  }
  std::sort(hits.begin(), hits.end());
  for (auto& h : hits) {
    std::ifstream in(h);
    std::string line, id, ver;
    while (std::getline(in, line)) {
      if (line.rfind("id = ", 0) == 0) id = line.substr(5);
      if (line.rfind("version = ", 0) == 0) ver = line.substr(10);
    }
    out << id << " " << ver << "  " << fs::relative(h.parent_path(), root).string() << "\n";
  }
  return 0;
}

}  // namespace

LoadReport run_load(int players, int64_t virtual_ms, uint64_t seed, const std::vector<std::string>& mods) {
  using Clock = std::chrono::steady_clock;
  LoadReport rep;
  rep.players = players;
  try {
    const auto t0 = Clock::now();
    SimConfig cfg;
    cfg.seed = seed;
    cfg.mods = mods.empty() ? std::vector<std::string>{"rathena.base", "ruleset.prerenewal"} : mods;
    cfg.map_w = cfg.map_h = 256;
    cfg.keep_trace = false;
    Sim sim(cfg);
    std::vector<SimPlayer*> ps;
    for (int i = 0; i < players; ++i) {
      Sim::PlayerSpec s;
      s.name = "load" + std::to_string(i);
      s.x = 40 + (i % 20) * 8;
      s.y = 40 + (i / 20) * 8;
      s.connect = false;
      ps.push_back(&sim.add_player(s));
      ps.back()->set_history_cap(64);
    }
    SimPlayer::connect_all(sim, ps);
    rep.setup_wall_s = std::chrono::duration<double>(Clock::now() - t0).count();

    DeterministicRng rng = sim.rng().derive("load-driver");
    const auto t1 = Clock::now();
    const int64_t period = 100;  // the driver acts every 100 ms of virtual time
    for (int64_t t = 0; t < virtual_ms; t += period) {
      for (auto* p : ps) {
        if (rng.rnd_range(0, 19) == 0) {  // about one walk per player per 2 s
          auto pos = p->position();
          if (pos) {
            int nx = std::clamp(pos->x + rng.rnd_range(-12, 12), 4, cfg.map_w - 5);
            int ny = std::clamp(pos->y + rng.rnd_range(-12, 12), 4, cfg.map_h - 5);
            p->walk(nx, ny);
            ++rep.walks;
          }
        }
        if (rng.rnd_range(0, 199) == 0) {  // about one chat per player per 20 s
          p->say("hello from " + p->name());
          ++rep.chats;
        }
      }
      sim.run_for(std::min<int64_t>(period, virtual_ms - t));
      for (auto* p : ps) {
        for (auto& e : p->drain_received()) {
          ++rep.frames_received;
          rep.bytes_received += e.raw.size();
        }
      }
    }
    rep.wall_s = std::chrono::duration<double>(Clock::now() - t1).count();
    rep.virtual_s = static_cast<double>(virtual_ms) / 1000.0;
    rep.wall_per_virtual_s = rep.virtual_s > 0 ? rep.wall_s / rep.virtual_s : 0;
    rep.steps = sim.steps();
    rep.trace_events = sim.trace_count();
    rep.trace_hash = sim.trace_hash();
    auto rr = sim.rng_report();
    rep.main_rng_draws = rr.main_draws;
    rep.native_rng_draws = rr.native_draws + rng.draws();
  } catch (const std::exception& e) {
    rep.error = e.what();
  }
  return rep;
}

int cli_main(int argc, char** argv, std::ostream& out, std::ostream& err) {
  std::vector<std::string> a(argv + 1, argv + argc);
  if (a.empty() || a[0] == "help" || a[0] == "--help" || a[0] == "-h") {
    out << kHelp;
    return a.empty() ? 2 : 0;
  }
  const std::string cmd = a[0];
  a.erase(a.begin());
  try {
    if (cmd == "run") return cmd_run(a, out, err);
    if (cmd == "coverage") return cmd_coverage(a, out, err);
    if (cmd == "conformance") return cmd_conformance(a, out, err);
    if (cmd == "load") return cmd_load(a, out, err);
    if (cmd == "mods") return cmd_mods(out);
  } catch (const std::exception& e) {
    err << "rathena-sim " << cmd << ": " << e.what() << "\n";
    return 2;
  }
  err << "rathena-sim: unknown command '" << cmd << "'\n" << kHelp;
  return 2;
}

}  // namespace tooling::sim
