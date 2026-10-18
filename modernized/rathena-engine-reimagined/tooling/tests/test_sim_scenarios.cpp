// Luau scenarios (scenarios/*.luau) and the DSL runner. Every shipped scenario is a test tagged with the
// rule it covers; the runner itself is tested with scenarios written on the fly.
#include <filesystem>
#include <sstream>

#include "sim_fixture.hpp"
#include "tooling/sim_cli.hpp"
#include "tooling/sim_scenario.hpp"

using namespace tooling::sim;
namespace fs = std::filesystem;

namespace {

std::string scen(const char* name) { return std::string(TOOLING_SOURCE_DIR) + "/scenarios/" + name + ".luau"; }

void passes(const char* name, std::vector<std::string> tags = {}) {
  simtest::need_luau();
  ScenarioResult r = run_scenario(scen(name));
  if (!r.passed) throw CheckFailure(std::string("scenario ") + name + " failed: " + r.message);
  CHECK(r.assertions >= 3);
  for (auto& t : tags) CHECK(std::find(r.rule_tags.begin(), r.rule_tags.end(), t) != r.rule_tags.end());
}

ScenarioResult run_text(const std::string& source, const std::string& directives = "") {
  static int n = 0;
  simtest::TempDir d("scenario" + std::to_string(n++));
  d.write("t.luau", directives + source);
  return run_scenario((d.path / "t.luau").string());
}

}  // namespace

TEST("TL-SIM-011", scenario_two_players_walk_and_see_each_other) { passes("two_players_see_each_other"); }
TEST("TL-SIM-020", scenario_legacy_npc_dialogue_driven_by_simplayer_answers) { passes("npc_dialogue_hypnotist"); }
TEST("BR-RULESET-020", scenario_damage_golden_prerenewal_pipeline_through_the_ruleset_package) {
  passes("damage_prerenewal_golden", {"BR-RULESET-020", "BR-RULESET-031", "BR-RULESET-037", "BR-RULESET-115"});
}
TEST("BR-RULESET-020", scenario_damage_golden_renewal_pipeline_through_the_ruleset_package) { passes("damage_renewal_golden"); }
TEST("BR-RULESET-020", scenario_example_bash_card_fix_through_packets) { passes("example_bash", {"BR-RULESET-020"}); }
TEST("TL-SIM-005", scenario_reload_during_pending_select_menu) { passes("reload_during_select"); }
TEST("TL-SIM-006", scenario_reload_with_pending_status_tick) { passes("reload_with_pending_status_tick"); }
TEST("BR-RULESET-116", scenario_mod_hook_cancel_and_ordering) { passes("mod_hook_cancel_ordering", {"BR-RULESET-116", "BR-RULESET-117"}); }
TEST("BR-RULESET-115", scenario_hook_rng_streams_do_not_touch_the_main_stream) { passes("hook_rng_streams"); }
TEST("BR-MAP-057", scenario_chat_speaker_name_cannot_be_forged) { passes("chat_speaker_integrity"); }
TEST("BR-MAP-059", scenario_packet_gating_and_connect_checks_with_raw_frames) { passes("packet_gating", {"BR-MAP-059", "BR-MAP-060"}); }
TEST("TL-SIM-030", scenario_position_survives_logout_in_sqlite) { passes("persistence_roundtrip"); }

TEST("TL-SIM-070", every_scenario_file_in_the_directory_passes_and_declares_at_least_one_rule) {
  simtest::need_luau();
  auto files = list_scenarios(std::string(TOOLING_SOURCE_DIR) + "/scenarios");
  CHECK(files.size() >= 12);
  for (auto& f : files) {
    ScenarioResult r = run_scenario(f);
    if (!r.passed) throw CheckFailure(f + ": " + r.message);
    if (r.rule_tags.empty()) throw CheckFailure(f + " declares no rule (--@rule)");
  }
}

TEST("TL-SIM-071", a_failing_assertion_fails_the_scenario_with_the_line_and_the_values) {
  simtest::need_luau();
  auto r = run_text("--@mods rathena.base\nlocal p = sim.player { name = 'a', x = 10, y = 10 }\nsim.assert_eq(1 + 1, 3, 'arithmetic')\n");
  CHECK(!r.passed);
  CHECK(r.message.find("arithmetic") != std::string::npos && r.message.find("expected 3, got 2") != std::string::npos);
  CHECK(r.message.find("t.luau:3") != std::string::npos);
  // an error() in the scenario and an unknown host command fail it too
  CHECK(!run_text("--@mods rathena.base\nerror('boom')\n").passed);
  auto bad = run_text("--@mods rathena.base\nsim.fail('nope')\n");
  CHECK(!bad.passed && bad.message.find("nope") != std::string::npos);
  // host errors are Luau errors: sim.try catches them
  auto ok = run_text("--@mods rathena.base\nlocal ok, err = sim.try(function() sim.eval('no.such.mod', 'x = 1') end)\nsim.assert_eq(ok, false)\nsim.assert_true(string.find(err, 'no.such.mod', 1, true) ~= nil, err)\n");
  if (!ok.passed) throw CheckFailure(ok.message);
}

TEST("TL-SIM-072", requests_and_replies_longer_than_one_dialogue_string_are_split_and_reassembled) {
  simtest::need_luau();
  auto r = run_text(
      "--@mods rathena.base\n"
      "local filler = string.rep('x', 9000)\n"
      "local lines = sim.eval('rathena.base', 'print(#[==[' .. filler .. ']==])\\nfor i = 1, 400 do print(string.rep(\"y\", 40) .. i) end')\n"
      "sim.assert_eq(lines[1], '9000', 'a 9000 byte request reached the mod intact')\n"
      "sim.assert_eq(#lines, 401, 'a reply of about 17 KB came back in pieces')\n"
      "sim.assert_eq(lines[401], string.rep('y', 40) .. '400', 'last line')\n");
  if (!r.passed) throw CheckFailure(r.message);
}

TEST("TL-SIM-073", the_scenario_state_is_sandboxed_and_a_runaway_loop_is_stopped_by_the_budget) {
  simtest::need_luau();
  auto r = run_text("--@mods rathena.base\nsim.assert_eq(os, nil)\nsim.assert_eq(io, nil)\nsim.assert_eq(require, nil)\nsim.assert_eq(loadstring, nil)\nsim.assert_eq(debug, nil)\n"
                    "sim.assert_eq(SIM_IMPORT, nil, 'no access to the mods modules')\n"
                    "sim.assert_eq(storage ~= nil, true)\n");
  if (!r.passed) throw CheckFailure(r.message);
  auto loop = run_text("--@mods rathena.base\nwhile true do end\n");
  CHECK(!loop.passed);
  CHECK(loop.message.find("budget") != std::string::npos || loop.message.find("Budget") != std::string::npos);
  CHECK(loop.wall_ms < 20000);
}

TEST("TL-SIM-074", directives_choose_mods_seed_map_and_declare_rules_in_both_spellings) {
  simtest::need_luau();
  simtest::TempDir d("directives");
  d.write("a.luau", "-- rule: BR-RULESET-020\n--@rule BR-MAP-059 TL-X-1\n--@mods rathena.base\n--@seed 5\n--@map 90x80\n--@memory-storage\nsim.rule('TL-X-2')\nsim.assert_eq(sim.seed(), 5)\nsim.assert_eq(#sim.mods(), 1)\nsim.assert_eq(sim.stats().storage_backend, 'memory')\nlocal p = sim.player { name = 'p', x = 89, y = 79 }\n");
  auto spec = parse_scenario((d.path / "a.luau").string());
  CHECK(spec.rule_tags == (std::vector<std::string>{"BR-RULESET-020", "BR-MAP-059", "TL-X-1"}));
  CHECK(spec.cfg.seed == 5 && spec.cfg.map_w == 90 && spec.cfg.map_h == 80 && !spec.cfg.sqlite);
  auto r = run_scenario((d.path / "a.luau").string());
  // (89,79) is the last column and row: outside the walkable terrain but inside the grid, so the player is placed
  if (!r.passed) throw CheckFailure(r.message);
  CHECK(std::find(r.rule_tags.begin(), r.rule_tags.end(), "TL-X-2") != r.rule_tags.end());  // declared at run time
  d.write("b.luau", "--@frobnicate yes\n");
  CHECK_THROWS(parse_scenario((d.path / "b.luau").string()));
}

TEST("BR-RULESET-115", a_scenario_run_twice_with_the_same_seed_has_the_same_wire_trace) {
  simtest::need_luau();
  auto a = run_scenario(scen("two_players_see_each_other"));
  auto b = run_scenario(scen("two_players_see_each_other"));
  CHECK(a.passed && b.passed && a.trace_hash == b.trace_hash && a.virtual_ms == b.virtual_ms);
  auto spec = parse_scenario(scen("damage_prerenewal_golden"));
  auto x = run_scenario_spec(spec);
  spec.cfg.seed = 8;
  auto y = run_scenario_spec(spec);
  CHECK(x.passed && y.passed);
  CHECK(x.main_rng_draws == y.main_rng_draws);  // the pipeline's draw COUNT is a function of the inputs, not the seed
}

TEST("TL-SIM-075", the_kill_switch_keeps_eval_working_after_a_mod_is_disabled_and_enabled_again) {
  auto sim = simtest::make_sim({"sim.hook.alpha", "sim.hook.beta"});
  sim->disable_mod("sim.hook.alpha");
  sim->enable_mod("sim.hook.alpha");
  auto lines = sim->eval("sim.hook.alpha", "print('still printing')").lines;  // the re-activated state has the sim's print sink
  CHECK(lines.size() == 1 && lines[0] == "still printing");
  CHECK(sim->generation("sim.hook.alpha") >= 2);
  auto r = sim->dispatch("sim:stage", 1);
  CHECK(r.ran.size() == 2 && r.value == 2);
}

TEST("TL-SIM-076", the_cli_runs_scenarios_lists_mods_and_rejects_unknown_commands) {
  simtest::need_luau();
  auto cli = [&](std::vector<std::string> args, std::string* out, std::string* err) {
    std::vector<std::string> full = {"rathena-sim"};
    full.insert(full.end(), args.begin(), args.end());
    std::vector<char*> argv;
    for (auto& s : full) argv.push_back(s.data());
    std::ostringstream o, e;
    int rc = cli_main(static_cast<int>(argv.size()), argv.data(), o, e);
    *out = o.str();
    *err = e.str();
    return rc;
  };
  std::string out, err;
  CHECK(cli({}, &out, &err) == 2 && out.find("rathena-sim run") != std::string::npos);
  CHECK(cli({"frobnicate"}, &out, &err) == 2 && err.find("unknown command") != std::string::npos);
  CHECK(cli({"mods"}, &out, &err) == 0);
  CHECK(out.find("rathena.base") != std::string::npos && out.find("ruleset.prerenewal") != std::string::npos && out.find("legacy.npc") != std::string::npos);
  CHECK(cli({"run", scen("packet_gating")}, &out, &err) == 0 && out.find("PASS  packet_gating") != std::string::npos);
  CHECK(cli({"run", "--json", scen("packet_gating")}, &out, &err) == 0 && out.find("\"passed\": true") != std::string::npos);
  simtest::TempDir d("fail");
  d.write("f.luau", "--@rule TL-X-9\n--@mods rathena.base\nsim.fail('expected')\n");
  CHECK(cli({"run", (d.path / "f.luau").string()}, &out, &err) == 1 && out.find("FAIL") != std::string::npos);
  CHECK(cli({"load", "--players", "10", "--seconds", "2"}, &out, &err) == 0 && out.find("10 players") != std::string::npos);
}
