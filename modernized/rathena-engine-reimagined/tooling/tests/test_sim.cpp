// rathena-sim acceptance tests. Rule ids: BR-RULESET-115, 119 (spec); TL-SIM-* are
// tooling-local ids derived from architecture section 8 (no spec rule id exists).
#include "harness.hpp"
#include "tooling/sim.hpp"

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

PENDING_TEST("TL-SIM-003", simplayer_casts_skill_and_observes_damage_packet) {
  SimPlayer p({"knight", 99});
  p.cast("ruleset.prerenewal:skill/sm_bash", "mob:poring");
  CHECK(!p.drain_received().empty());
}

PENDING_TEST("TL-SIM-004", luau_scenario_declares_br_tags_and_passes) {
  auto r = run_scenario(std::string(TOOLING_SOURCE_DIR) + "/scenarios/example_bash.luau");
  CHECK(r.passed);
  CHECK(!r.rule_tags.empty() && r.rule_tags[0] == "BR-RULESET-020");
}

PENDING_TEST("BR-RULESET-119", golden_trace_replay_matches_for_both_rulesets) {
  auto recs = load_golden_trace(std::string(TOOLING_SOURCE_DIR) + "/scenarios/golden_example.trace");
  CHECK(replay_golden_trace(recs, DbSet::PreRenewal));
  CHECK(replay_golden_trace(recs, DbSet::Renewal));
}

PENDING_TEST("TL-SIM-005", reload_during_select_finishes_dialogue_in_old_generation) {
  SimPlayer p({"novice", 1});
  p.select(1);
  reload_ruleset_generation("ruleset.prerenewal");
  CHECK(false);  // real assertion: dialogue completes under generation N, new casts use N+1
}

PENDING_TEST("TL-SIM-006", reload_with_pending_status_tick_does_not_fire_twice) {
  reload_ruleset_generation("ruleset.renewal");
  CHECK(false);  // real assertion: old-generation closure cancelled, tick fires once
}
