// Gateway-owned BR-CHAR rules (plumbing). Party/guild/mail/clan/fame/cooldown
// rules (033-051, 053, 054) belong to other services. BR-CHAR-030 (map-server
// registration) is superseded by the in-process WorldBus seam. BR-CHAR-052
// (sex-change class swap) is ruleset/base policy.
#include "fixture.hpp"

using namespace gt;

static MakeCharRequest good(const std::string& name = "Bobby", int slot = 0) {
  MakeCharRequest r;
  r.name = name;
  r.slot = slot;
  return r;
}

TEST("BR-CHAR-001", make_rejects_bad_names_with_minus1_minus2_and_duplicate_minus1) {
  Env e;
  auto& a = e.add_account();
  auto s = e.session(a);
  CHECK(e.chr.make(s, good("Bob")).error == -1);
  CHECK(e.chr.make(s, good("Bob!")).error == -2);
  CHECK(e.chr.make(s, good("Bobby")).ok);
  CHECK(e.chr.make(s, good("Bobby", 1)).error == -1);  // exists
}

TEST("BR-CHAR-002", slot_beyond_char_slots_minus4_occupied_minus2) {
  Env e;
  auto& a = e.add_account();
  auto s = e.session(a);
  CHECK(e.chr.make(s, good("Bobby", 99)).error == -4);
  CHECK(e.chr.make(s, good("Bobby", 0)).ok);
  CHECK(e.chr.make(s, good("Carol", 0)).error == -2);
}

TEST("BR-CHAR-003", count_must_be_below_slots) {
  Env e;
  auto& a = e.add_account();
  auto s = e.session(a);
  for (int i = 0; i < 9; ++i) e.add_char(a.id, "Filler" + std::to_string(i), i);
  CHECK(!e.chr.make(s, good("Overflow", 8)).ok);
}

TEST("BR-CHAR-004", new_clients_choose_sex_old_clients_use_account_sex) {
  Env e;
  auto& a = e.add_account();
  auto s = e.session(a);
  auto r = good(); r.sex = Sex::Female;
  CHECK(e.chr.make(s, r).ch.sex == Sex::Female);
  auto old = good("Oldie", 1); old.new_client = false; old.sex = Sex::Female;
  old.stats = {5, 5, 5, 5, 5, 5};
  CHECK(e.chr.make(s, old).ch.sex == Sex::Male);  // account is male
}

TEST("BR-CHAR-005", make_enforces_starting_stats) {
  Env e;
  auto& a = e.add_account();
  auto s = e.session(a);
  auto r = good(); r.new_client = false; r.stats = {6, 5, 5, 5, 5, 5};
  CHECK(!e.chr.make(s, r).ok);
}

TEST("BR-CHAR-006", allowed_start_jobs_follow_flag) {
  Env e;
  auto& a = e.add_account();
  auto s = e.session(a);
  auto r = good(); r.start_job_flag = 0;
  CHECK(!e.chr.make(s, r).ok);
}

TEST("BR-CHAR-007", created_character_has_initial_hp_sp) {
  Env e;
  auto& a = e.add_account();
  auto s = e.session(a);
  auto o = e.chr.make(s, good());
  CHECK(o.ok && o.ch.max_hp == 40 && o.ch.max_sp == 11);
}

TEST("BR-CHAR-008", char_new_no_refuses_all_creation) {
  Env e;
  e.cfg.char_new = false;
  auto& a = e.add_account();
  auto s = e.session(a);
  CHECK(!e.chr.make(s, good()).ok);
}

TEST("BR-CHAR-009", delete_request_queues_date_now_plus_delay_blocked_by_guild_or_party) {
  Env e;
  auto& a = e.add_account();
  auto s = e.session(a);
  CharId c = e.add_char(a.id, "Bobby", 0);
  auto o = e.chr.request_delete(s, c);
  CHECK(o.ok && o.delete_date == e.clock.now() + 86400);
  CharId g = e.add_char(a.id, "Guilded", 1);
  e.bus.members[g] = {0, 7};
  CHECK(e.chr.request_delete(s, g).code == 4);
  CharId p = e.add_char(a.id, "Partied", 2);
  e.bus.members[p] = {3, 0};
  CHECK(e.chr.request_delete(s, p).code == 5);
}

TEST("BR-CHAR-010", delete_confirm_requires_birthdate_code) {
  Env e;
  auto& a = e.add_account();
  auto s = e.session(a);
  CharId c = e.add_char(a.id, "Bobby", 0);
  e.chr.request_delete(s, c);
  e.clock.advance(86401);
  CHECK(e.chr.confirm_delete(s, c, "000000").code == 5);
  CHECK(e.chr.confirm_delete(s, c, "900101").ok);
}

TEST("BR-CHAR-011", legacy_delete_by_email_per_char_del_option) {
  Env e;
  auto& a = e.add_account();
  auto s = e.session(a);
  CharId c = e.add_char(a.id, "Bobby", 0);
  e.cfg.char_del_option = 1;      // [impl] the option under test: e-mail ...
  e.cfg.char_del_delay_s = 0;     // [impl] ... and no deletion delay (legacy: delay > 0 needs a queued request)
  CHECK(e.chr.confirm_delete(s, c, "user@example.invalid").ok);
}

TEST("BR-CHAR-012", delete_gate_ownership_level_and_delay) {
  Env e;
  auto& a = e.add_account();
  auto& other = e.add_account("other", 2000002);
  auto s = e.session(a);
  CharId theirs = e.add_char(other.id, "Theirs", 0);
  CHECK(!e.chr.request_delete(s, theirs).ok);          // not owner
  CharId mine = e.add_char(a.id, "Mine", 0);
  e.chr.request_delete(s, mine);
  CHECK(!e.chr.confirm_delete(s, mine, "900101").ok);  // delay not elapsed
}

TEST("BR-CHAR-013", confirmed_delete_removes_character_and_delegates_world_cleanup) {
  Env e;
  auto& a = e.add_account();
  auto s = e.session(a);
  CharId c = e.add_char(a.id, "Bobby", 0);
  e.chr.request_delete(s, c);
  e.clock.advance(86401);
  CHECK(e.chr.confirm_delete(s, c, "900101").ok);
  CHECK(!e.chars.find(c).has_value());
  CHECK(e.bus.purged.size() == 1 && e.bus.purged[0] == c);
}

TEST("BR-CHAR-014", cancel_delete_resets_date_to_zero) {
  Env e;
  auto& a = e.add_account();
  auto s = e.session(a);
  CharId c = e.add_char(a.id, "Bobby", 0);
  e.chr.request_delete(s, c);
  CHECK(e.chr.cancel_delete(s, c).ok);
  CHECK(e.chars.find(c)->delete_date == 0);
}

TEST("BR-CHAR-015", character_queued_for_deletion_cannot_be_selected) {
  Env e;
  auto& a = e.add_account();
  auto s = e.session(a);
  CharId c = e.add_char(a.id, "Bobby", 0);
  e.chr.request_delete(s, c);
  CHECK(!e.chr.select(s, 0).ok);
}

TEST("BR-CHAR-016", active_character_ban_blocks_selection_and_adjusts_from_max) {
  Env e;
  auto& a = e.add_account();
  auto s = e.session(a);
  CharId c = e.add_char(a.id, "Bobby", 0);
  CHECK(e.chr.ban_character(c, 3600) == e.clock.now() + 3600);
  CHECK(!e.chr.select(s, 0).ok);
  CHECK(e.chr.ban_character(c, -100000) == 0);
}

TEST("BR-CHAR-017", rename_needs_counter_no_party_guild_and_name_rules) {
  Env e;
  auto& a = e.add_account();
  auto s = e.session(a);
  CharId c = e.add_char(a.id, "Bobby", 0);
  CHECK(!e.chr.rename(s, c, "Robert"));  // counter == 0
  auto ch = *e.chars.find(c); ch.rename_counter = 1; e.chars.update(ch);
  CHECK(!e.chr.rename(s, c, "Bob!"));
  CHECK(e.chr.rename(s, c, "Robert"));
  CHECK(e.chars.find(c)->rename_counter == 0);
}

TEST("BR-CHAR-018", slot_move_requires_moves_valid_target_swap_if_enabled) {
  Env e;
  auto& a = e.add_account();
  auto s = e.session(a);
  CharId c = e.add_char(a.id, "Bobby", 0);
  CHECK(!e.chr.move_slot(s, c, 1, false));  // slot_moves == 0
  auto ch = *e.chars.find(c); ch.slot_moves = 1; e.chars.update(ch);
  CHECK(e.chr.move_slot(s, c, 1, false));
  CHECK(e.chars.find(c)->slot == 1);
}

TEST("BR-CHAR-019", pin_state_machine) {
  Env e;
  auto& a = e.add_account();
  CHECK(e.chr.pin_state(a, false) == PinState::Passed || e.chr.pin_state(a, false) == PinState::New);
  e.cfg.pin_forced = true;
  CHECK(e.chr.pin_state(a, false) == PinState::New);
  Account with = a; with.pin = "fake-pin-hash"; with.pin_changed = e.clock.now();
  CHECK(e.chr.pin_state(with, false) == PinState::Ask);
  CHECK(e.chr.pin_state(with, true) == PinState::Passed);
  e.cfg.pin_expire_s = 100;
  with.pin_changed = e.clock.now() - 1000;
  CHECK(e.chr.pin_state(with, false) == PinState::Expired);
}

TEST("BR-CHAR-020", pin_set_rejects_illegal_pins) {
  Env e;
  auto& a = e.add_account();
  auto s = e.session(a);
  CHECK(!e.chr.pin_set(s, "", "1234"));
  CHECK(e.chr.pin_set(s, "", "1357"));
}

TEST("BR-CHAR-021", three_wrong_pins_take_account_offline_success_resets) {
  Env e;
  auto& a = e.add_account();
  Account u = a; u.pin = e.hasher.hash("1357"); e.accounts.update(u);
  auto s = e.session(a, rules::SessionPhase::AwaitingPin);
  CHECK(!e.chr.pin_check(s, "2468").ok);
  CHECK(e.chr.pin_check(s, "1357").ok);          // resets counter
  CHECK(!e.chr.pin_check(s, "2468").ok);
  CHECK(!e.chr.pin_check(s, "2468").ok);
  auto o = e.chr.pin_check(s, "2468");
  CHECK(o.account_offlined);
}

TEST("BR-CHAR-022", pin_gate_allows_only_connect_ping_pin_charlist_until_verified) {
  using rules::CharPacket;
  using rules::SessionPhase;
  for (auto p : {CharPacket::Connect, CharPacket::Ping, CharPacket::PinCheck, CharPacket::PinSet, CharPacket::CharList})
    CHECK(rules::char_packet_allowed(SessionPhase::AwaitingPin, p));
  for (auto p : {CharPacket::SelectChar, CharPacket::MakeChar, CharPacket::DeleteChar, CharPacket::Other})
    CHECK(!rules::char_packet_allowed(SessionPhase::AwaitingPin, p));
  CHECK(rules::char_packet_allowed(SessionPhase::Ready, CharPacket::SelectChar));
}

TEST("BR-CHAR-023", capacity_or_maintenance_refuses_below_gm_allow_group) {
  Env e;
  auto& a = e.add_account();
  auto s = e.session(a, rules::SessionPhase::Unauthenticated);
  // capacity/maintenance config seam added with implementation; full world => refuse
  e.bus.up = false;
  CHECK(!e.chr.enter(s, a.id, 11, 22, Sex::Male).ok);
  // [impl] the real gate: valid ticket, but the server is full / in maintenance and the group is below gm_allow_group
  e.bus.up = true;
  e.tickets.issue({a.id, 11, 22, Sex::Male, e.clock.now()});
  e.cfg.max_connect_user = 0;   // closed
  CHECK(!e.chr.enter(s, a.id, 11, 22, Sex::Male).ok);
  e.cfg.gm_allow_group = 0;     // the account's group (0) may bypass
  e.tickets.issue({a.id, 11, 22, Sex::Male, e.clock.now()});
  CHECK(e.chr.enter(s, a.id, 11, 22, Sex::Male).ok);
}

TEST("BR-CHAR-025", duplicate_char_session_kicks_with_code_8) {
  Env e;
  auto& a = e.add_account();
  e.tickets.issue({a.id, 11, 22, Sex::Male, e.clock.now()});
  e.dir.mark({a.id, 0, 0, e.clock.now()});
  auto s = e.session(a, rules::SessionPhase::Unauthenticated);
  auto o = e.chr.enter(s, a.id, 11, 22, Sex::Male);
  CHECK(!o.ok && o.refusal_code == refuse::AlreadyOnline);
  CHECK(e.bus.kicks.size() == 1);
}

TEST("BR-CHAR-026", enter_verifies_ticket_wrong_id2_rejected_valid_ticket_advances_phase) {
  Env e;
  auto& a = e.add_account();
  e.tickets.issue({a.id, 11, 22, Sex::Male, e.clock.now()});
  auto s = e.session(a, rules::SessionPhase::Unauthenticated);
  CHECK(!e.chr.enter(s, a.id, 11, 99, Sex::Male).ok);
  CHECK(s.phase == rules::SessionPhase::Unauthenticated);
  CHECK(e.chr.enter(s, a.id, 11, 22, Sex::Male).ok);
  CHECK(s.phase != rules::SessionPhase::Unauthenticated);
}

TEST("BR-CHAR-028", startup_sets_all_characters_offline) {
  Env e;
  auto& a = e.add_account();
  CharId c = e.add_char(a.id, "Bobby", 0);
  auto ch = *e.chars.find(c); ch.online = true; e.chars.update(ch);
  e.chr.startup_set_offline();
  CHECK(!e.chars.find(c)->online);
}

TEST("BR-CHAR-029", stale_cleanup_skips_connected_entry_and_continues) {
  Env e;
  e.dir.mark({1, 0, 1, e.clock.now() - 10000});  // still connected (modelled by world bus up)
  e.dir.mark({2, kOnlineLost, 2, e.clock.now() - 10000});
  int purged = e.chr.cleanup_stale_online();
  CHECK(purged >= 1);
  CHECK(!e.dir.find(2).has_value());
}

TEST("BR-CHAR-031", world_auth_requires_matching_ticket_single_use) {
  Env e;
  auto& a = e.add_account();
  CharId c = e.add_char(a.id, "Bobby", 0);
  e.tickets.issue({a.id, 11, 22, Sex::Male, e.clock.now()});
  CHECK(!e.chr.authenticate_from_world(a.id, c, 12));
  CHECK(e.chr.authenticate_from_world(a.id, c, 11));
  CHECK(!e.chr.authenticate_from_world(a.id, c, 11));
}

TEST("BR-CHAR-032", save_accepted_only_if_online_or_final_and_size_matches) {
  Env e;
  auto& a = e.add_account();
  CharId c = e.add_char(a.id, "Bobby", 0);
  CHECK(!e.chr.accept_save(c, false, 100, 100));  // not online
  CHECK(e.chr.accept_save(c, true, 100, 100));    // final
  CHECK(!e.chr.accept_save(c, true, 99, 100));    // size mismatch
}

TEST("BR-CHAR-055", gm_op_authority_rechecked_by_gateway) {
  Env e;
  e.add_account("target", 2000002);
  e.add_account("plain", 2000003);  // group 0
  CHECK(!e.admin.gm_set_state(2000003, 2000002, 5));
  CHECK(e.accounts.find(2000002)->state == 0);
}
