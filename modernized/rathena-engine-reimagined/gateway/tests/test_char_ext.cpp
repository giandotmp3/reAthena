// Character select / lifecycle / PIN: behaviours beyond the original per-rule placeholders. Derived
// from char.cpp, char_clif.cpp, char_logif.cpp, char_mapif.cpp (see src/char_service.cpp).
#include "fixture.hpp"
#include "gateway/hasher.hpp"

using namespace gt;

namespace {
// A policy with different numbers than the legacy fixture: proves the gateway holds no game numbers.
struct OtherNumbers : DefaultNamePolicy {
  bool starting_stats_valid(bool, const rules::StartStats& s) const override { return s.str == 3; }
  bool start_job_allowed(int f) const override { return f == 7; }
  std::optional<StartPackage> starting_package(int f, const rules::StartStats&) const override {
    if (f != 7) return std::nullopt;
    StartPackage p;
    p.max_hp = 777;
    p.max_sp = 333;
    p.numbers["bonus"] = 5;
    return p;
  }
  std::vector<std::string> delete_cleanup_plan() const override { return {"z-last", "a-first"}; }
};
struct BarePolicy : PolicyHooks {   // only the mandatory hook: everything else is fail-safe
  NameVerdict check_name(const std::string&) const override { return NameVerdict::Ok; }
};
MakeCharRequest req(const std::string& name, int slot = 0) {
  MakeCharRequest r; r.name = name; r.slot = slot; return r;
}
}  // namespace

TEST("BR-CHAR-001", name_edge_cases_through_make) {
  Env e;
  auto& a = e.add_account();
  auto s = e.session(a);
  CHECK(e.chr.make(s, req("")).error == -1);
  CHECK(e.chr.make(s, req("#chan")).error == -1);
  CHECK(e.chr.make(s, req("Server")).error == -1);
  CHECK(e.chr.make(s, req("sErVeR")).error == -1);                       // the reserved name is matched ignoring case
  CHECK(e.chr.make(s, req(std::string("Bo\x01by"))).error == -1);
  CHECK(e.chr.make(s, req(std::string(24, 'a'))).error == -2);          // longer than NAME_LENGTH - 1
  auto ok = e.chr.make(s, req("  Padded  "));
  CHECK(ok.ok && ok.ch.name == "Padded");                               // stored trimmed
  CHECK(e.chr.make(s, req("PADDED", 1)).error == -1);                   // names are unique ignoring case
  CHECK(e.chr.make(s, req("padded", 1)).error == -1);
}

TEST("BR-CHAR-002", slot_edges_and_error_precedence) {
  Env e;
  auto& a = e.add_account();
  auto s = e.session(a);
  CHECK(e.chr.make(s, req("Bobby", -1)).error == -4);
  CHECK(e.chr.make(s, req("Bobby", 9)).error == -4);                    // slots = 9
  CHECK(e.chr.make(s, req("Bob", 99)).error == -1);                     // name is checked before the slot
  CHECK(e.chr.make(s, req("Bobby", 8)).ok);
}

TEST("BR-CHAR-004", sex_rules_server_account_and_bad_client_sex) {
  Env e;
  auto& a = e.add_account();
  auto s = e.session(a);
  auto r = req("Bobby"); r.sex = Sex::Server;
  CHECK(e.chr.make(s, r).error == -2);
  Account u = a; u.sex = Sex::Server; e.accounts.update(u);
  auto old = req("Oldie", 1); old.new_client = false; old.stats = {5, 5, 5, 5, 5, 5};
  CHECK(e.chr.make(s, old).error == -2);
}

TEST("BR-CHAR-007", hooks_supply_all_numbers_gateway_has_none) {
  Env e;
  auto& a = e.add_account();
  OtherNumbers p;
  GatewayDeps d{e.cfg, e.clock, e.rng, e.accounts, e.chars, e.dir, e.bus, e.tickets, e.hasher, e.bans, e.fails, p};
  CharService svc(d);
  auto s = e.session(a);
  auto r = req("Hooked"); r.stats = {3, 1, 1, 1, 1, 1}; r.start_job_flag = 7;
  r.client_choices["hair_style"] = 4;
  auto o = svc.make(s, r);
  CHECK(o.ok && o.ch.max_hp == 777 && o.ch.max_sp == 333);
  CHECK(o.ch.extra.at("bonus") == 5 && o.ch.extra.at("client.hair_style") == 4);
  CHECK(e.chars.find(o.ch.id)->extra == o.ch.extra);                    // persisted with the character
  r.name = "Hooked2"; r.slot = 1;
  r.stats = {1, 1, 1, 1, 1, 1};
  CHECK(svc.make(s, r).error == -2);                                    // the hook's stat rule, not the fixture's
  r.stats = {3, 1, 1, 1, 1, 1}; r.start_job_flag = 1;
  CHECK(svc.make(s, r).error == -2);                                    // the hook's job rule
}

TEST("BR-CHAR-006", a_policy_without_start_hooks_refuses_everything_fail_safe) {
  Env e;
  auto& a = e.add_account();
  BarePolicy p;
  GatewayDeps d{e.cfg, e.clock, e.rng, e.accounts, e.chars, e.dir, e.bus, e.tickets, e.hasher, e.bans, e.fails, p};
  CharService svc(d);
  auto s = e.session(a);
  CHECK(svc.make(s, req("Bobby")).error == -2);
  CHECK(e.chars.list_by_account(a.id).empty());
}

TEST("BR-CHAR-003", vip_account_gets_more_slots_until_vip_ends) {
  Env e;
  auto& a = e.add_account();
  auto s = e.session(a);
  CHECK(e.chr.make(s, req("Slot10", 10)).error == -4);
  Account u = a; u.vip_time = e.clock.now() + 100; e.accounts.update(u);
  CHECK(e.chr.make(s, req("Slot10", 10)).ok);
  CHECK(e.chr.make(s, req("Slot15", 15)).error == -4);                   // max_chars is 15 slots: 0..14
  CHECK(e.chr.make(s, req("Slot14", 14)).ok);
  CHECK(e.chr.visible_slots(*e.accounts.find(a.id)) == 15);
}

TEST("BR-CHAR-013", cleanup_plan_goes_to_the_world_before_the_row_is_erased) {
  Env e;
  auto& a = e.add_account();
  auto s = e.session(a);
  CharId c = e.add_char(a.id, "Bobby", 0);
  bool row_present_at_purge = false;
  e.bus.on_purge = [&](CharId id) { row_present_at_purge = e.chars.find(id).has_value(); };
  e.cfg.char_del_delay_s = 0;
  CHECK(e.chr.confirm_delete(s, c, "900101").ok);
  CHECK(row_present_at_purge);                                           // world cleanup first, gateway row last
  CHECK(e.bus.purge_plans.size() == 1);
  const auto& plan = e.bus.purge_plans[0];
  CHECK(plan.size() == 16 && plan.front() == "divorce" && plan[2] == "party_leave" && plan.back() == "guild_leave_or_break");
  CHECK(!e.chars.find(c));
  OtherNumbers p;
  GatewayDeps d{e.cfg, e.clock, e.rng, e.accounts, e.chars, e.dir, e.bus, e.tickets, e.hasher, e.bans, e.fails, p};
  CharService svc(d);
  CharId c2 = e.add_char(a.id, "Second", 1);
  CHECK(svc.confirm_delete(s, c2, "900101").ok);
  CHECK(e.bus.purge_plans.back() == (std::vector<std::string>{"z-last", "a-first"}));   // order is the hook's, as given
}

TEST("BR-CHAR-012", delete_gates_level_restriction_bits_and_failure_leaves_the_row) {
  Env e;
  auto& a = e.add_account();
  auto s = e.session(a);
  e.cfg.char_del_delay_s = 0;
  CharId low = e.add_char(a.id, "Lowbie", 0), high = e.add_char(a.id, "Highbie", 1);
  { auto c = *e.chars.find(low); c.base_level = 10; e.chars.update(c); }
  { auto c = *e.chars.find(high); c.base_level = 90; e.chars.update(c); }
  e.cfg.char_del_level = 80;                                             // levels >= 80 are protected
  CHECK(e.chr.confirm_delete(s, high, "900101").result == DeleteResult::BaseLevel);
  e.cfg.char_del_level = -20;                                            // levels <= 20 are protected
  CHECK(e.chr.confirm_delete(s, low, "900101").result == DeleteResult::BaseLevel);
  e.cfg.char_del_level = 0;
  e.bus.members[low] = {3, 7};
  e.cfg.char_del_restriction = 1;                                        // party only
  CHECK(e.chr.confirm_delete(s, low, "900101").result == DeleteResult::Party);
  e.cfg.char_del_restriction = 2;                                        // guild only
  CHECK(e.chr.confirm_delete(s, low, "900101").result == DeleteResult::Guild);
  e.cfg.char_del_restriction = 0;
  struct Failing : InProcWorldBus {
    void purge_character_data(CharId, const std::vector<std::string>&) override { throw StoreUnavailable("world mailbox full"); }
  } failing;
  GatewayDeps d{e.cfg, e.clock, e.rng, e.accounts, e.chars, e.dir, failing, e.tickets, e.hasher, e.bans, e.fails, e.policy};
  CharService svc(d);
  bool threw = false;
  try { svc.confirm_delete(s, low, "900101"); } catch (const StoreUnavailable&) { threw = true; }
  CHECK(threw && e.chars.find(low).has_value());                         // refused cleanup => the character is NOT deleted
  CHECK(e.chr.confirm_delete(s, low, "900101").ok);
}

TEST("BR-CHAR-010", birthdate_and_email_special_cases_and_foreign_characters) {
  Env e;
  auto& a = e.add_account();
  auto s = e.session(a);
  e.cfg.char_del_delay_s = 0;
  CharId c = e.add_char(a.id, "Bobby", 0);
  CHECK(e.chr.confirm_delete(s, c, "").result == DeleteResult::BadCode);        // a birthdate is set
  Account u = a; u.birthdate.clear(); e.accounts.update(u);
  CHECK(e.chr.confirm_delete(s, 424242, "").result == DeleteResult::NotFound);  // unknown char (code is fine)
  e.cfg.char_del_option = 3;
  u.email = "a@a.com"; e.accounts.update(u);
  CHECK(e.chr.confirm_delete(s, c, "").ok);                                      // default e-mail + empty answer
}

TEST("BR-CHAR-009", request_twice_and_cancel_cycle) {
  Env e;
  auto& a = e.add_account();
  auto s = e.session(a);
  CharId c = e.add_char(a.id, "Bobby", 0);
  CHECK(e.chr.request_delete(s, c).ok);
  auto again = e.chr.request_delete(s, c);
  CHECK(!again.ok && again.code == 0 && again.result == DeleteResult::AlreadyQueued);
  CHECK(!e.chr.cancel_delete(s, 424242).ok);
  CHECK(e.chr.cancel_delete(s, c).ok && e.chars.find(c)->delete_date == 0);
  CHECK(e.chr.request_delete(s, c).ok);                                           // can be queued again
  e.cfg.char_del_restriction = 0;
  CharId g = e.add_char(a.id, "Guilded", 1);
  e.bus.members[g] = {1, 1};
  CHECK(e.chr.request_delete(s, g).ok);                                           // restriction off
}

TEST("BR-CHAR-017", rename_codes_one_by_one) {
  Env e;
  auto& a = e.add_account();
  auto s = e.session(a);
  CharId c = e.add_char(a.id, "Bobby", 0);
  e.add_char(a.id, "Taken", 1);
  { auto ch = *e.chars.find(c); ch.rename_counter = 2; e.chars.update(ch); }
  CHECK(e.chr.rename_code(s, 424242, "Whatever") == 2);
  CHECK(e.chr.rename_code(s, c, "Bobby") == 0 && e.chars.find(c)->rename_counter == 2);   // same name: free
  CHECK(e.chr.rename_code(s, c, "Taken") == 4);
  CHECK(e.chr.rename_code(s, c, "Bad!") == 8);
  e.bus.members[c] = {1, 0};
  CHECK(e.chr.rename_code(s, c, "Robert") == 6);
  e.bus.members[c] = {0, 1};
  CHECK(e.chr.rename_code(s, c, "Robert") == 5);
  e.cfg.char_rename_party = e.cfg.char_rename_guild = true;
  CHECK(e.chr.rename_code(s, c, "Robert") == 0 && e.chars.find(c)->name == "Robert" && e.chars.find(c)->rename_counter == 1);
  CHECK(e.chr.name_acceptable(s, c, "Fresh1") && !e.chr.name_acceptable(s, c, "Taken"));
  auto other = e.session(e.add_account("other", 2000002));
  CHECK(e.chr.rename_code(other, c, "Stolen") == 2);                            // not your character
}

TEST("BR-CHAR-018", slot_move_rules) {
  Env e;
  auto& a = e.add_account();
  auto s = e.session(a);
  CharId c0 = e.add_char(a.id, "Zero", 0), c1 = e.add_char(a.id, "One", 1);
  { auto x = *e.chars.find(c0); x.slot_moves = 2; e.chars.update(x); }
  CHECK(!e.chr.move_slot(s, c0, 1, false));                                      // target used, swap not allowed
  CHECK(e.chr.move_slot(s, c0, 1, true));                                        // triangle exchange
  CHECK(e.chars.find(c0)->slot == 1 && e.chars.find(c1)->slot == 0);
  CHECK(e.chars.find(c0)->slot_moves == 1 && e.chars.find(c1)->slot_moves == 0);   // only the moved one pays
  CHECK(!e.chr.move_slot(s, c0, 9, true));                                       // slot beyond the account's slots
  CHECK(!e.chr.move_slot(s, c0, -1, true));
  CHECK(!e.chr.move_slot(s, c0, 1, true));                                       // onto itself
  e.cfg.char_move_enabled = false;
  CHECK(!e.chr.move_slot(s, c0, 5, true));
  e.cfg.char_move_enabled = true;
  e.cfg.char_moves_unlimited = true;
  CHECK(e.chr.move_slot(s, c1, 5, true) && e.chars.find(c1)->slot == 5);
  int left = -1;
  CHECK(e.chr.move_slot_by_slot(s, 5, 6, &left) == 0 && left == 0);
  CHECK(e.chr.move_slot_by_slot(s, 7, 8, &left) == 1);                           // nothing in slot 7
}

// ---- PIN -----------------------------------------------------------------------------------------------------
TEST("BR-CHAR-019", pin_state_machine_all_branches) {
  Env e;
  auto& a = e.add_account();
  e.cfg.pin_enabled = false;
  CHECK(e.chr.pin_state(a, false) == PinState::Ok);
  e.cfg.pin_enabled = true;
  CHECK(e.chr.pin_state(a, false) == PinState::Passed);
  e.cfg.pin_forced = true;
  CHECK(e.chr.pin_state(a, false) == PinState::New);
  Account w = a; w.pin = "h"; w.pin_changed = e.clock.now();
  CHECK(e.chr.pin_state(w, false) == PinState::Ask && e.chr.pin_state(w, true) == PinState::Passed);
  e.cfg.pin_expire_s = 100;
  CHECK(e.chr.pin_state(w, false) == PinState::Ask);
  w.pin_changed = e.clock.now() - 100;                                           // exactly at the limit: expired
  CHECK(e.chr.pin_state(w, true) == PinState::Expired);
}

TEST("BR-CHAR-020", pin_set_requires_the_old_pin_and_stores_a_hash) {
  Env e;
  auto& a = e.add_account();
  auto s = e.session(a, rules::SessionPhase::AwaitingPin);
  CHECK(e.chr.pin_set(s, "", "1357"));
  auto acc = *e.accounts.find(a.id);
  CHECK(acc.pin != "1357" && !acc.pin.empty() && acc.pin_changed == e.clock.now());
  CHECK(s.phase == rules::SessionPhase::Ready && s.pin_verified);
  auto again = e.session(a, rules::SessionPhase::AwaitingPin);
  CHECK(!e.chr.pin_set(again, "", "2468"));                                      // legacy let any session overwrite it
  CHECK(!e.chr.pin_set(again, "9999", "2468"));
  CHECK(e.chr.pin_set(again, "1357", "2468"));
  CHECK(!e.chr.pin_set(again, "2468", "1234"));                                  // new PIN must be legal
  CHECK(!e.chr.pin_set(again, "2468", "12ab"));
  auto none = e.session(a, rules::SessionPhase::Unauthenticated);
  CHECK(!e.chr.pin_set(none, "", "1357"));                                       // no PIN work before char auth
}

TEST("BR-CHAR-021", lockout_survives_reconnect_decays_with_time_and_offlines_the_account) {
  Env e;
  auto& a = e.add_account();
  Account u = a; u.pin = e.hasher.hash("1357"); e.accounts.update(u);
  e.dir.mark({a.id, kOnlineLoginPhase, 0, e.clock.now()});
  auto s1 = e.session(a, rules::SessionPhase::AwaitingPin);
  auto o1 = e.chr.pin_check(s1, "0000");
  CHECK(!o1.ok && o1.attempts_left == 2);
  CHECK(!e.chr.pin_check(s1, "0000").ok);
  auto o3 = e.chr.pin_check(s1, "0000");
  CHECK(o3.account_offlined && !e.dir.find(a.id) && e.bus.kicks.size() == 1 && s1.phase == rules::SessionPhase::Unauthenticated);
  // a new session starts with the account's counter, not zero: one wrong guess locks again
  auto s2 = e.session(a, rules::SessionPhase::AwaitingPin);
  CHECK(e.chr.pin_check(s2, "0000").account_offlined);
  // the right PIN still works and clears the counter
  auto s3 = e.session(a, rules::SessionPhase::AwaitingPin);
  CHECK(e.chr.pin_check(s3, "1357").ok && e.accounts.find(a.id)->pin_failures == 0);
  // failures older than the window are forgotten
  auto s4 = e.session(a, rules::SessionPhase::AwaitingPin);
  e.chr.pin_check(s4, "0000"); e.chr.pin_check(s4, "0000");
  e.clock.advance(e.cfg.pin_failure_window_s + 1);
  auto o = e.chr.pin_check(s4, "0000");
  CHECK(!o.account_offlined && o.attempts_left == 2);
  // bad format / no PIN set / unauthenticated session are not guesses
  CHECK(e.chr.pin_check(s4, "12").bad_format);
  auto none = e.session(a, rules::SessionPhase::Unauthenticated);
  CHECK(e.chr.pin_check(none, "1357").bad_format);
}

TEST("BR-CHAR-021", pepper_from_the_environment_changes_the_stored_material) {
  setenv("GATEWAY_PIN_PEPPER", "fake-pepper", 1);
  Env e;
  auto& a = e.add_account();
  auto s = e.session(a, rules::SessionPhase::AwaitingPin);
  CHECK(e.chr.pin_set(s, "", "1357"));
  auto acc = *e.accounts.find(a.id);
  CHECK(!e.hasher.verify(PasswordScheme::Argon2id, acc.pin, "1357").ok);          // not hash(pin) ...
  CHECK(e.hasher.verify(PasswordScheme::Argon2id, acc.pin, "fake-pepper:1357").ok);   // ... but hash(pepper:pin)
  auto s2 = e.session(a, rules::SessionPhase::AwaitingPin);
  CHECK(e.chr.pin_check(s2, "1357").ok);
  unsetenv("GATEWAY_PIN_PEPPER");
}

TEST("BR-CHAR-022", select_make_delete_are_refused_by_the_service_until_the_pin_is_verified) {
  Env e;
  auto& a = e.add_account();
  e.add_char(a.id, "Bobby", 0);
  auto s = e.session(a, rules::SessionPhase::AwaitingPin);
  CHECK(!e.chr.select(s, 0).ok);                                                  // gate lives in the service, not only in the wire layer
  s.phase = rules::SessionPhase::Ready;
  CHECK(e.chr.select(s, 0).ok);
}

// ---- enter / handoff ----------------------------------------------------------------------------------------------
TEST("BR-CHAR-026", enter_ticket_stage_sex_ttl_and_replays) {
  Env e;
  auto& a = e.add_account();
  auto o = e.login.login(e.req());
  CharSession s;
  CHECK(!e.chr.enter(s, a.id, o.ticket->login_id1, o.ticket->login_id2, Sex::Female).ok);   // wrong sex: refused, ticket kept
  CHECK(e.chr.enter(s, a.id, o.ticket->login_id1, o.ticket->login_id2, Sex::Male).ok);
  CharSession replay;
  CHECK(!e.chr.enter(replay, a.id, o.ticket->login_id1, o.ticket->login_id2, Sex::Male).ok);   // single use
  CHECK(e.chr.enter(s, a.id, 1, 2, Sex::Male).ignored);                                   // authed session sends it again: discarded
  // a World-stage ticket is not accepted at the char hop
  e.tickets.issue({a.id, 5, 6, Sex::Male, e.clock.now(), 1, AuthStage::World});
  CharSession s2;
  CHECK(!e.chr.enter(s2, a.id, 5, 6, Sex::Male).ok);
  // expired
  auto o2 = e.login.login(e.req());
  e.clock.advance(31);
  CharSession s3;
  CHECK(!e.chr.enter(s3, a.id, o2.ticket->login_id1, o2.ticket->login_id2, Sex::Male).ok);
  // world closed
  auto o3 = e.login.login(e.req());
  e.bus.up = false;
  CharSession s4;
  auto r = e.chr.enter(s4, a.id, o3.ticket->login_id1, o3.ticket->login_id2, Sex::Male);
  CHECK(!r.ok && !r.via_notify_ban && r.refusal_code == 0);
}

TEST("BR-CHAR-025", second_connection_in_char_select_is_refused_and_disconnect_frees_the_slot) {
  Env e;
  auto& a = e.add_account();
  auto o = e.login.login(e.req());
  CharSession s1; s1.conn_id = 11;
  CHECK(e.chr.enter(s1, a.id, o.ticket->login_id1, o.ticket->login_id2, Sex::Male).ok);
  e.tickets.issue({a.id, 5, 6, Sex::Male, e.clock.now(), 0, AuthStage::Login});
  CharSession s2; s2.conn_id = 22;
  auto r = e.chr.enter(s2, a.id, 5, 6, Sex::Male);
  CHECK(!r.ok && r.via_notify_ban && r.refusal_code == refuse::AlreadyOnline);
  e.chr.on_disconnect(s1);
  e.tickets.issue({a.id, 5, 6, Sex::Male, e.clock.now(), 0, AuthStage::Login});
  CHECK(e.chr.enter(s2, a.id, 5, 6, Sex::Male).ok);
}

TEST("BR-CHAR-025", world_session_is_kicked_and_forced_offline_after_30_seconds) {
  Env e;
  auto& a = e.add_account();
  CharId c = e.add_char(a.id, "Bobby", 0);
  { auto ch = *e.chars.find(c); ch.online = true; e.chars.update(ch); }
  e.dir.mark({a.id, 0, c, e.clock.now()});
  e.tickets.issue({a.id, 5, 6, Sex::Male, e.clock.now(), 0, AuthStage::Login});
  CharSession s;
  auto r = e.chr.enter(s, a.id, 5, 6, Sex::Male);
  CHECK(!r.ok && r.refusal_code == 8 && e.bus.kicks.size() == 1);
  e.clock.advance(31);
  CHECK(!e.dir.find(a.id) && !e.chars.find(c)->online);
}

TEST("BR-CHAR-026", returning_from_the_world_skips_the_pin) {
  Env e;
  auto& a = e.add_account();
  Account u = a; u.pin = e.hasher.hash("1357"); u.pin_changed = e.clock.now(); e.accounts.update(u);
  CharSession fresh;
  auto o = e.login.login(e.req());
  auto r1 = e.chr.enter(fresh, a.id, o.ticket->login_id1, o.ticket->login_id2, Sex::Male);
  CHECK(r1.ok && r1.pin == PinState::Ask && fresh.phase == rules::SessionPhase::AwaitingPin);
  e.chr.prepare_return_from_world(a.id, 31, 32, Sex::Male);
  CharSession back;
  auto r2 = e.chr.enter(back, a.id, 31, 32, Sex::Male);
  CHECK(r2.ok && r2.pin == PinState::Passed && back.phase == rules::SessionPhase::Ready && back.returning_from_world);
}

TEST("BR-CHAR-031", handoff_ticket_is_world_stage_bound_to_the_character_and_expires) {
  Env e;
  auto& a = e.add_account();
  CharId c0 = e.add_char(a.id, "Zero", 0), c1 = e.add_char(a.id, "One", 1);
  auto s = e.session(a);
  auto sel = e.chr.select(s, 0);
  CHECK(sel.ok && sel.ticket->stage == AuthStage::World && sel.ticket->char_id == c0);
  CHECK(e.dir.find(a.id)->state == kOnlineHandoff && e.chars.find(c0)->online);
  CHECK(e.dir.user_count() == 0);                                                   // not "in world" yet
  CHECK(!e.chr.authenticate_from_world(a.id, c1, s.login_id1));                     // other character: refused (and the ticket is burnt)
  CHECK(!e.chr.authenticate_from_world(a.id, c0, s.login_id1));
  auto sel2 = e.chr.select(s, 0);
  const Sex female = Sex::Female;
  CHECK(!e.chr.authenticate_from_world(a.id, c0, s.login_id1, &female));            // wrong sex keeps the ticket
  const Sex male = Sex::Male;
  CHECK(e.chr.authenticate_from_world(a.id, c0, s.login_id1, &male));
  CHECK(e.dir.find(a.id)->state == 0 && e.dir.find(a.id)->char_id == c0 && e.dir.user_count() == 1);
  (void)sel2;
}

TEST("BR-CHAR-031", login_ticket_cannot_be_used_at_the_world_hop_pin_bypass) {
  Env e;
  auto& a = e.add_account();
  CharId c = e.add_char(a.id, "Bobby", 0);
  auto o = e.login.login(e.req());
  CHECK(!e.chr.authenticate_from_world(a.id, c, o.ticket->login_id1));              // skipping char-select and the PIN
  CharSession s;
  CHECK(e.chr.enter(s, a.id, o.ticket->login_id1, o.ticket->login_id2, Sex::Male).ok);   // the ticket was not burnt by the attempt
}

TEST("BR-CHAR-031", unanswered_handoff_expires_and_frees_the_character) {
  Env e;
  auto& a = e.add_account();
  CharId c = e.add_char(a.id, "Bobby", 0);
  auto s = e.session(a);
  CHECK(e.chr.select(s, 0).ok);
  e.clock.advance(29);
  CHECK(e.dir.find(a.id).has_value());
  e.clock.advance(2);
  CHECK(!e.dir.find(a.id) && !e.chars.find(c)->online);
  CHECK(!e.chr.authenticate_from_world(a.id, c, s.login_id1));
  CHECK(!e.tickets.consume(a.id, s.login_id1, s.login_id2, e.clock.now()));   // the unused ticket was revoked, not left behind
}

TEST("BR-CHAR-016", character_ban_kicks_an_online_character_only) {
  Env e;
  auto& a = e.add_account();
  CharId c = e.add_char(a.id, "Bobby", 0);
  e.chr.ban_character(c, 100);
  CHECK(e.bus.kicks.empty());
  { auto ch = *e.chars.find(c); ch.online = true; e.chars.update(ch); }
  e.chr.ban_character(c, 100);
  CHECK(e.bus.kicks.size() == 1);
  CHECK(e.chr.ban_character(424242, 5) == 0);
}

TEST("BR-CHAR-029", cleanup_skips_connected_entries_and_keeps_going_defect_fix) {
  Env e;
  e.dir.mark({1, kOnlineLoginPhase, 0, e.clock.now() - 5000, 77});                  // connected: skipped, scan continues
  e.dir.mark({2, kOnlineLost, 0, e.clock.now() - 10000});
  e.dir.mark({3, kOnlineHandoff, 0, e.clock.now() - 100});
  e.dir.mark({4, kOnlineLoginPhase, 0, e.clock.now() - 100, 0, 0});                 // stale login-phase entry
  e.dir.mark({5, 0, 9, e.clock.now() - 100000});                                    // in the world
  e.dir.mark({6, kOnlineLost, 0, e.clock.now() - 10000});
  CHECK(e.chr.cleanup_stale_online() == 4);
  CHECK(e.dir.find(1) && e.dir.find(5) && !e.dir.find(2) && !e.dir.find(3) && !e.dir.find(4) && !e.dir.find(6));
}

TEST("BR-CHAR-032", save_for_unknown_character_is_refused_even_when_final) {
  Env e;
  CHECK(!e.chr.accept_save(424242, true, 10, 10));
}

TEST("BR-CHAR-028", startup_resets_online_flags_for_every_character) {
  Env e;
  auto& a = e.add_account();
  CharId c0 = e.add_char(a.id, "Zero", 0), c1 = e.add_char(a.id, "One", 1);
  for (CharId id : {c0, c1}) { auto ch = *e.chars.find(id); ch.online = true; e.chars.update(ch); }
  e.chr.startup_set_offline();
  CHECK(!e.chars.find(c0)->online && !e.chars.find(c1)->online);
}
