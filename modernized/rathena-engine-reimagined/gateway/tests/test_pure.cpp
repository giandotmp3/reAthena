// Active acceptance tests: pure rules and in-process seam implementations.
#include "fixture.hpp"

using namespace gt;

TEST("BR-LOGIN-021", population_colour_thresholds) {
  using rules::PopulationColour;
  CHECK(rules::population_colour(200, true) == PopulationColour::Green);
  CHECK(rules::population_colour(201, true) == PopulationColour::Yellow);
  CHECK(rules::population_colour(500, true) == PopulationColour::Yellow);
  CHECK(rules::population_colour(1000, true) == PopulationColour::Red);
  CHECK(rules::population_colour(1001, true) == PopulationColour::Purple);
  CHECK(rules::population_colour(5, false) == PopulationColour::Hidden);
}

TEST("BR-LOGIN-031", slots_non_vip_9_vip_15_with_max_15_vip_6) {
  CHECK(rules::account_char_slots(0, 9, false, 6, 15) == 9);
  CHECK(rules::account_char_slots(0, 9, true, 6, 15) == 15);
  CHECK(rules::account_char_slots(5, 9, false, 6, 15) == 5);
}

TEST("BR-CHAR-024", slots_clamped_to_max_and_zero_becomes_min) {
  CHECK(rules::clamp_slots(40, 3, 15) == 15);
  CHECK(rules::clamp_slots(0, 3, 15) == 3);
  CHECK(rules::clamp_slots(7, 3, 15) == 7);
}

TEST("BR-LOGIN-026", ban_extends_from_max_now_current_and_ignores_past_result) {
  CHECK(*rules::extend_ban(1000, 0, 500) == 1500);
  CHECK(*rules::extend_ban(1000, 5000, 500) == 5500);
  CHECK(!rules::extend_ban(1000, 0, -2000).has_value());
}

TEST("BR-CHAR-016", char_ban_adjust_below_now_clears_to_zero) {
  CHECK(rules::adjust_char_ban(1000, 0, 500) == 1500);
  CHECK(rules::adjust_char_ban(1000, 5000, -10000) == 0);
}

TEST("BR-CHAR-020", pin_must_be_four_digits_and_not_trivial) {
  CHECK(!rules::pin_is_legal("1234"));
  CHECK(!rules::pin_is_legal("8901"));
  CHECK(!rules::pin_is_legal("3210"));
  CHECK(!rules::pin_is_legal("1111"));
  CHECK(rules::pin_is_legal("1357"));
  CHECK(!rules::pin_is_legal("123"));
  CHECK(!rules::pin_is_legal("12a4"));
}

TEST("BR-CHAR-005", starting_stats_old_and_new_clients) {
  CHECK(rules::starting_stats_valid({5, 5, 5, 5, 5, 5}, false));
  CHECK(rules::starting_stats_valid({9, 5, 5, 1, 5, 5}, false));    // pairs 9+1, 5+5, 5+5
  CHECK(!rules::starting_stats_valid({6, 5, 5, 5, 5, 5}, false));   // STR+INT != 10
  CHECK(!rules::starting_stats_valid({10, 5, 5, 0, 5, 5}, false));  // out of 1..9
  CHECK(rules::starting_stats_valid({1, 1, 1, 1, 1, 1}, true));
  CHECK(!rules::starting_stats_valid({2, 1, 1, 1, 1, 1}, true));
}

TEST("BR-CHAR-007", initial_hp_sp) {
  auto r = rules::initial_hp_sp(1, 1);
  CHECK(r.hp == 40 && r.sp == 11);
  auto r2 = rules::initial_hp_sp(9, 9);
  CHECK(r2.hp == 40 * 109 / 100 && r2.sp == 11 * 109 / 100);
}

TEST("BR-CHAR-008", create_error_code_mapping) {
  CHECK(rules::create_error_wire_code(-1) == 0x00);
  CHECK(rules::create_error_wire_code(-2) == 0xFF);
  CHECK(rules::create_error_wire_code(-3) == 0x01);
  CHECK(rules::create_error_wire_code(-4) == 0x03);
}

TEST("BR-CHAR-001", default_name_policy_verdicts) {
  Env e;
  CHECK(e.policy.check_name("Bobby") == NameVerdict::Ok);
  CHECK(e.policy.check_name("Bob") == NameVerdict::Rejected);       // < 4
  CHECK(e.policy.check_name("#Bobby") == NameVerdict::Rejected);
  CHECK(e.policy.check_name("Server") == NameVerdict::Rejected);
  CHECK(e.policy.check_name(std::string("Bo\x01by")) == NameVerdict::Rejected);
  CHECK(e.policy.check_name("Bob!") == NameVerdict::BadLetters);    // allow-list policy
  CHECK(e.policy.check_name("  Bobby  ") == NameVerdict::Ok);       // trimmed
}

TEST("BR-LOGIN-019", ticket_30s_single_use_and_wrong_id2_refused) {
  Env e;
  AuthTicket t{2000001, 11, 22, Sex::Male, e.clock.now()};
  e.tickets.issue(t);
  CHECK(!e.tickets.consume(2000001, 11, 99, e.clock.now()).has_value());  // wrong id2
  CHECK(e.tickets.consume(2000001, 11, 22, e.clock.now() + 29).has_value());
  CHECK(!e.tickets.consume(2000001, 11, 22, e.clock.now() + 29).has_value());  // single use
  e.tickets.issue(t);
  CHECK(!e.tickets.consume(2000001, 11, 22, e.clock.now() + 31).has_value());  // expired
}

TEST("BR-LOGIN-022", ip_ban_masks_and_fail_closed) {
  Env e;
  Timestamp now = e.clock.now();
  e.bans.add("203.0.113.*", now + 60);
  CHECK(e.bans.is_banned("203.0.113.9", now));
  CHECK(!e.bans.is_banned("203.0.114.9", now));
  CHECK(!e.bans.is_banned("203.0.113.9", now + 61));  // expired
  e.bans.backend_failing = true;
  CHECK(e.bans.is_banned("198.51.100.1", now));        // fails closed
}

TEST("BR-LOGIN-023", seven_failures_in_five_minutes_ban_class_c_independent_of_log) {
  Env e;
  Timestamp now = e.clock.now();
  bool banned = false;
  for (int i = 0; i < 6; ++i) banned = e.fails.record_failure("198.51.100.7", now + i) || banned;
  CHECK(!banned);
  CHECK(e.fails.record_failure("198.51.100.7", now + 6));
  CHECK(e.bans.is_banned("198.51.100.200", now + 7));       // whole a.b.c.*
  CHECK(!e.bans.is_banned("198.51.100.200", now + 7 + 301));
}

TEST("BR-LOGIN-023", failures_outside_window_do_not_accumulate) {
  Env e;
  Timestamp now = e.clock.now();
  for (int i = 0; i < 6; ++i) e.fails.record_failure("198.51.100.8", now + i * 100);  // spans > 300 s
  CHECK(!e.bans.is_banned("198.51.100.8", now + 600));
}

TEST("BR-LOGIN-024", expired_ip_bans_purged) {
  Env e;
  Timestamp now = e.clock.now();
  e.bans.add("1.2.3.*", now + 10);
  e.bans.add("4.5.6.*", now + 1000);
  e.bans.purge_expired(now + 60);
  CHECK(e.bans.size() == 1);
}

TEST("BR-CHAR-027", user_count_never_negative) {
  Env e;
  CHECK(e.dir.user_count() == 0);
  e.dir.mark({1, kOnlineLoginPhase, 0, 0});
  e.dir.mark({2, kOnlineLost, 0, 0});
  CHECK(e.dir.user_count() == 0);
  e.dir.mark({3, 0, 5, 0});
  CHECK(e.dir.user_count() == 1);
}

TEST("BR-LOGIN-008", account_id_range_enforced_by_store) {
  Env e;
  Account a; a.id = 0; CHECK(!e.accounts.create(a).has_value());
  a.id = 100000001; CHECK(!e.accounts.create(a).has_value());
  a.id = 2000000; CHECK(e.accounts.create(a).has_value());
}

TEST("BR-LOGIN-038", userid_lookup_case_sensitivity) {
  Env e;
  e.add_account("FakeUser", 2000001);
  CHECK(e.accounts.find_by_userid("fakeuser", true).empty());
  CHECK(e.accounts.find_by_userid("fakeuser", false).size() == 1);
}

TEST("ARCH-ROLE", role_flag_parsing) {
  CHECK(parse_role("all") == Role::All);
  CHECK(parse_role("gateway") == Role::Gateway);
  CHECK(parse_role("world") == Role::World);
  CHECK(!parse_role("map").has_value());
  CHECK(role_runs_gateway(Role::All) && role_runs_world(Role::All));
  CHECK(role_runs_gateway(Role::Gateway) && !role_runs_world(Role::Gateway));
  CHECK(!role_runs_gateway(Role::World) && role_runs_world(Role::World));
}
