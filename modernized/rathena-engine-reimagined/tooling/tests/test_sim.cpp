// rathena-sim acceptance tests. Rule ids: BR-RULESET-115, 119 (spec); TL-SIM-* are
// tooling-local ids derived from architecture section 8 (no spec rule id exists).
#include "harness.hpp"
#include "sim_fixture.hpp"
#include "tooling/sim_scenario.hpp"

using namespace tooling;
using namespace tooling::sim;

TEST("BR-RULESET-115", seeded_rng_is_reproducible) {
  DeterministicRng a(42), b(42);
  for (int i = 0; i < 100; ++i) CHECK(a.next() == b.next());
}

TEST("BR-RULESET-115", chance_draws_even_when_rate_not_positive) {
  // Legacy BR-MAP-010/011: the draw happens even when the chance is <= 0,
  // so seeded replays stay aligned.
  DeterministicRng r(7);
  CHECK(!r.chance(0, 100));
  CHECK(!r.chance(-5, 100));
  CHECK(r.draws() == 2);
}

TEST("BR-RULESET-115", hook_streams_do_not_shift_main_stream) {
  DeterministicRng with_hook(9), without_hook(9);
  auto h = with_hook.derive("mymod:fire_brand");
  for (int i = 0; i < 5; ++i) h.next();
  CHECK(with_hook.draws() == 0);
  CHECK(with_hook.next() == without_hook.next());
}

TEST("TL-SIM-001", virtual_clock_moves_only_when_advanced_and_fires_in_order) {
  VirtualClock c;
  std::string order;
  c.schedule(30, [&] { order += "b"; });
  c.schedule(10, [&] { order += "a"; });
  c.schedule(30, [&] { order += "c"; });  // same due: registration order
  CHECK(c.now_ms() == 0 && order.empty());
  c.advance(29);
  CHECK(order == "a");
  c.advance(1);
  CHECK(order == "abc" && c.now_ms() == 30);
}

TEST("TL-SIM-002", in_memory_store_roundtrip) {
  InMemoryStore s;
  s.put("char:1", "inventory", "x");
  std::string v;
  CHECK(s.get("char:1", "inventory", &v) && v == "x");
  CHECK(!s.get("char:2", "inventory", &v));
}

TEST("BR-RULESET-119", coverage_report_is_keyed_by_rule_id) {
  CoverageReport r;
  r.record("BR-RULESET-020", RuleStatus::Passing);
  r.record("BR-RULESET-019", RuleStatus::Pending);
  CHECK(r.status("BR-RULESET-020") == RuleStatus::Passing);
  CHECK(r.status("BR-RULESET-999") == RuleStatus::Uncovered);
  CHECK(r.render() == "BR-RULESET-019\tpending\nBR-RULESET-020\tpassing\n");
}


// ---- the former scaffold stubs, now implemented on the real stack --------------------------------
// TL-SIM-003 keeps its pending marker with a STRONGER body: a cast of Bash must end in a damage frame
// (ZC_NOTIFY_ACT with damage > 0). The ruleset packages implement the basic weapon attack only (their
// skill formulas raise NOT_IMPLEMENTED, ruleset-prerenewal scripts/rules/skill.luau), so no cast can
// produce damage yet; the sim reports that instead of inventing numbers. The same shape with the
// attack that exists is scenarios/example_bash.luau (TL-SIM-004).
PENDING_TEST("TL-SIM-003", simplayer_casts_skill_and_observes_damage_packet) {
  auto sim = simtest::make_sim({"rathena.base", "ruleset.prerenewal"}, 1, [](SimConfig& c) { c.stage_test_support = true; });
  auto& p = simtest::add(*sim, "knight", 100, 100);
  uint32_t mob = sim->spawn_dummy("poring", 101, 100, "{kind='mob',level=1}");
  p.drain_received();
  p.cast("ruleset.prerenewal:skill/sm_bash", mob, 5);
  sim->run_for(200);
  bool damage = false;
  for (auto& e : p.drain_received())
    if (e.name == "ZC_NOTIFY_ACT" && e.num("damage") > 0) damage = true;
  CHECK(damage);
}

TEST("TL-SIM-004", luau_scenario_declares_br_tags_and_passes) {
  simtest::need_luau();
  auto r = run_scenario(std::string(TOOLING_SOURCE_DIR) + "/scenarios/example_bash.luau");
  if (!r.passed) throw CheckFailure("scenario failed: " + r.message);
  CHECK(!r.rule_tags.empty() && r.rule_tags[0] == "BR-RULESET-020");
  CHECK(r.assertions >= 5);
}

TEST("BR-RULESET-119", golden_trace_replay_matches_for_both_rulesets) {
  simtest::need_luau();
  auto recs = load_golden_trace(std::string(TOOLING_SOURCE_DIR) + "/scenarios/golden_example.trace");
  CHECK(recs.size() == 1 && recs[0].rule_id == "BR-RULESET-020" && recs[0].expected == "1380");
  CHECK(recs[0].inputs == "seed=1 damage=1000 race=+20 size=+15");
  CHECK(replay_golden_trace(recs, DbSet::PreRenewal));
  CHECK(replay_golden_trace(recs, DbSet::Renewal));
  // the comparison bites: the 1350 an ADDITIVE card fix would give is not what the packages compute
  auto wrong = recs;
  wrong[0].expected = "1350";
  CHECK(!replay_golden_trace(wrong, DbSet::PreRenewal));
  CHECK(!replay_golden_trace(wrong, DbSet::Renewal));
}

TEST("TL-SIM-005", reload_during_select_finishes_dialogue_in_old_generation) {
  auto sim = simtest::make_sim({"rathena.base", "legacy.npc"});
  sim->load_npc_file(repo_root() / "npc/pre-re/other/resetskill.txt");
  sim->place_npc("Hypnotist#2", 146, 232);
  auto& p = simtest::add(*sim, "neo", 146, 228);
  p.click_npc("Hypnotist#2");
  for (int i = 0; i < 3; ++i) {
    p.expect("say", 500);
    p.expect("next", 100);
    p.next();
  }
  p.expect("menu", 500);  // parked on select()
  CHECK(sim->dialogue_of(p.gid()).active && sim->dialogue_of(p.gid()).generation == 1);
  auto r = sim->reload("legacy.npc");
  CHECK(r.ok && r.generation == 2);
  CHECK(sim->dialogue_of(p.gid()).generation == 1);  // the dialogue completes under generation N ...
  p.answer(3);
  p.expect("say", 500);
  p.expect("next", 100);
  p.next();
  p.expect("close", 500);
  CHECK(!sim->dialogue_of(p.gid()).active);
  // ... and what starts afterwards uses N+1
  auto& q = simtest::add(*sim, "trinity", 147, 228);
  q.click_npc("Hypnotist#2");
  q.expect("say", 500);
  CHECK(sim->dialogue_of(q.gid()).generation == 2);
}

TEST("TL-SIM-006", reload_with_pending_status_tick_does_not_fire_twice) {
  auto sim = simtest::make_sim({"sim.status.tick"});
  auto ticks = [&] {
    auto v = sim->storage().get(script_host::Scope::World, "world", "sim.status.tick", "sim.status.tick", "ticks");
    return v ? std::get<int64_t>(*v) : int64_t(0);
  };
  sim->run_for(400);
  CHECK(ticks() == 0);
  auto r = sim->reload("sim.status.tick");  // same code, generation 2; the old tick (due at 1000) is cancelled
  CHECK(r.ok && r.generation == 2);
  sim->run_for(3100);                       // t = 3500: generation 2 ticks at 1400, 2400, 3400
  CHECK(ticks() == 3);                      // without the cancel the old closure would add 1000, 2000 and 3000
}
