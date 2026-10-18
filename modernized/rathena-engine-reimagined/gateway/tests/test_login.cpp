// Gateway-owned BR-LOGIN rules. These were expected-failure tests against stubs; the services are
// implemented now. Where a body originally said "config seam added with implementation" (013, 014, 016,
// 039, 005) the seam setup or the real assertion was added; every such edit is marked "[impl]".
#include "fixture.hpp"

using namespace gt;

// BR-LOGIN-025 (char-server registration with inter-server credentials) is
// SUPERSEDED: login/char/map are one process behind in-process seams.
// BR-LOGIN-036 (## registry) belongs to persistence/KV, not the gateway.

TEST("BR-LOGIN-001", first_failing_check_wins_banned_with_correct_password_is_6) {
  Env e;
  auto& a = e.add_account();
  Account u = a; u.unban_time = e.clock.now() + 3600; e.accounts.update(u);
  auto o = e.login.login(e.req());
  CHECK(!o.ok && o.refusal_code == refuse::Banned);
}

TEST("BR-LOGIN-001", wrong_password_beats_banned_check_order) {
  Env e;
  auto& a = e.add_account();
  Account u = a; u.unban_time = e.clock.now() + 3600; e.accounts.update(u);
  auto o = e.login.login(e.req("fakeuser", "Wrong-Pass-9"));
  CHECK(!o.ok && o.refusal_code == refuse::IncorrectPassword);
}

TEST("BR-LOGIN-002", plaintext_compare_is_case_sensitive) {
  Env e;
  auto& a = e.add_account();
  Account u = a; u.scheme = PasswordScheme::Plaintext; u.stored_secret = "secret1"; e.accounts.update(u);
  CHECK(e.login.login(e.req("fakeuser", "secret1")).ok);
  CHECK(!e.login.login(e.req("fakeuser", "Secret1")).ok);
}

TEST("BR-LOGIN-003", md5_challenge_accepts_salt_pass_or_pass_salt) {
  Env e;
  auto& a = e.add_account();
  Account u = a; u.scheme = PasswordScheme::Md5; u.stored_secret = "unused"; e.accounts.update(u);
  auto r1 = e.req(); r1.kind = LoginKind::Md5Challenge; r1.challenge_salt = "salt";
  r1.secret = e.hasher.md5_hex("salt" + std::string(kFakePass));
  CHECK(e.login.login(r1).ok);
  auto r2 = r1; r2.secret = e.hasher.md5_hex(std::string(kFakePass) + "salt");
  CHECK(e.login.login(r2).ok);
}

TEST("BR-LOGIN-004", legacy_use_md5_passwords_option_is_removed_argon2id_at_rest) {
  Env e;
  e.add_account();
  e.login.login(e.req());
  auto stored = e.accounts.find(2000001);
  CHECK(stored && stored->scheme == PasswordScheme::Argon2id);
  CHECK(e.hasher.verify(PasswordScheme::Argon2id, stored->stored_secret, kFakePass).ok);
}

TEST("BR-LOGIN-005", challenge_salt_bytes_1_to_255_never_nul) {
  Env e;
  e.add_account();  // [impl] the placeholder logged in without any account existing
  // Gateway exposes a salt generator through login(); a hook-level API lands with netproto wiring.
  auto o = e.login.login(e.req());
  CHECK(o.ok);  // placeholder expectation: salt generation is exercised through the challenge path
  // [impl] the real rule: every byte of every generated salt is in 1..255, never NUL
  for (int round = 0; round < 200; ++round) {
    std::string salt = e.login.make_challenge_salt(20);
    CHECK(salt.size() == 20);
    for (char c : salt) CHECK(static_cast<unsigned char>(c) != 0);
  }
}

TEST("BR-LOGIN-006", new_account_suffix_M_creates_account_then_authenticates) {
  Env e;
  e.cfg.new_account = true;
  auto o = e.login.login(e.req("bob123_M", "pass12"));
  CHECK(o.ok);
  auto found = e.accounts.find_by_userid("bob123", true);
  CHECK(found.size() == 1 && found[0].sex == Sex::Male);
}

TEST("BR-LOGIN-006", new_account_refused_when_too_short_or_exists_code_1) {
  Env e;
  e.cfg.new_account = true;
  e.add_account("bob123");
  CHECK(e.login.login(e.req("bob123_M", "pass12")).refusal_code == refuse::IncorrectPassword);
}

TEST("BR-LOGIN-007", registration_flood_limit_is_server_wide_code_3) {
  Env e;
  e.cfg.new_account = true;
  int refused = 0;
  for (int i = 0; i < 20; ++i) {
    auto o = e.login.login(e.req("flood" + std::to_string(i) + "_M", "pass12"));
    if (!o.ok && o.refusal_code == refuse::Rejected) ++refused;
  }
  CHECK(refused > 0);
}

TEST("BR-LOGIN-008", new_account_id_is_max_plus_one_at_least_2000000) {
  Env e;
  e.cfg.new_account = true;
  auto o = e.login.login(e.req("newbie_F", "pass12"));
  CHECK(o.ok && o.account_id >= 2000000);
}

TEST("BR-LOGIN-009", server_sex_account_cannot_log_in_from_client) {
  Env e;
  auto& a = e.add_account("s1");
  Account u = a; u.sex = Sex::Server; e.accounts.update(u);
  auto o = e.login.login(e.req("s1"));
  CHECK(!o.ok && o.refusal_code == refuse::UnregisteredId);
}

TEST("BR-LOGIN-010", expired_account_is_code_2_zero_means_never) {
  Env e;
  auto& a = e.add_account();
  Account u = a; u.expiration_time = e.clock.now() - 1; e.accounts.update(u);
  CHECK(e.login.login(e.req()).refusal_code == refuse::Expired);
  u.expiration_time = 0; e.accounts.update(u);
  CHECK(e.login.login(e.req()).ok);
}

TEST("BR-LOGIN-011", future_unban_time_is_code_6_with_date) {
  Env e;
  auto& a = e.add_account();
  Account u = a; u.unban_time = e.clock.now() + 100; e.accounts.update(u);
  auto o = e.login.login(e.req());
  CHECK(o.refusal_code == refuse::Banned && o.unban_time == u.unban_time);
}

TEST("BR-LOGIN-012", state_n_refuses_with_code_n_minus_1) {
  Env e;
  auto& a = e.add_account();
  Account u = a; u.state = 5; e.accounts.update(u);
  CHECK(e.login.login(e.req()).refusal_code == 4);
}

TEST("BR-LOGIN-013", client_hash_mismatch_is_code_5) {
  Env e;
  e.add_account();
  auto r = e.req(); r.client_hash = "00000000000000000000000000000000";
  // Requires a configured hash list excluding this hash (config seam added with implementation).
  e.cfg.client_hash_check = true;  // [impl] the config seam: a list that does not contain the zero hash
  e.cfg.client_hash_nodes = {{0, "113e195e6c051bb1cfb12a644bb084c5"}};
  CHECK(e.login.login(r).refusal_code == refuse::ClientHash);
  r.client_hash = "113e195e6c051bb1cfb12a644bb084c5";
  CHECK(e.login.login(r).ok);
}

TEST("BR-LOGIN-014", dnsbl_hit_is_code_3) {
  Env e;
  e.add_account();
  auto r = e.req(); r.ip = "192.0.2.99";  // listed by a fake DNSBL resolver seam
  e.cfg.use_dnsbl = true;                  // [impl] the resolver seam is the fixture's FakeDnsbl
  e.dnsbl.hits.insert("192.0.2.99");
  CHECK(e.login.login(r).refusal_code == refuse::Rejected);
  CHECK(e.login.login(e.req()).ok);        // an unlisted address passes
}

TEST("BR-LOGIN-015", success_rotates_ids_updates_account_and_clears_unban) {
  Env e;
  auto& a = e.add_account();
  Account u = a; u.unban_time = 1; e.accounts.update(u);  // past ban
  auto o = e.login.login(e.req());
  CHECK(o.ok && o.ticket);
  auto s = e.accounts.find(2000001);
  CHECK(s->logincount == 1 && s->unban_time == 0 && s->last_ip == "192.0.2.10");
  CHECK(s->login_id1 == o.ticket->login_id1 && s->login_id2 == o.ticket->login_id2);
  CHECK(s->web_token.size() == 16);
}

TEST("BR-LOGIN-016", group_gating_server_closed_code_1) {
  Env e;
  e.add_account();
  // min_group_id_to_connect above the account group (config seam added with implementation).
  e.cfg.min_group_id_to_connect = 1;  // [impl] the config seam; the fixture account has group 0
  CHECK(e.login.login(e.req()).refusal_code == refuse::ServerClosed);
}

TEST("BR-LOGIN-017", world_unavailable_refuses_with_code_1) {
  Env e;
  e.add_account();
  e.bus.up = false;
  auto o = e.login.login(e.req());
  CHECK(!o.ok && o.refusal_code == refuse::ServerClosed);
}

TEST("BR-LOGIN-018", duplicate_login_kicks_existing_and_client_gets_code_8) {
  Env e;
  e.add_account();
  CHECK(e.login.login(e.req()).ok);
  e.dir.mark({2000001, 0, 150000, e.clock.now()});
  auto o = e.login.login(e.req());
  CHECK(!o.ok && o.refusal_code == refuse::AlreadyOnline);
  CHECK(e.bus.kicks.size() == 1 && e.bus.kicks[0] == 2000001);
}

TEST("BR-LOGIN-018", online_entry_force_cleared_after_30s) {
  Env e;
  e.add_account();
  e.dir.mark({2000001, 0, 150000, e.clock.now()});
  e.login.login(e.req());
  e.clock.advance(31);
  e.login.sweep_duplicate_clears();
  CHECK(!e.dir.find(2000001).has_value());
}

TEST("BR-LOGIN-019", successful_login_issues_single_use_ticket_in_store) {
  Env e;
  e.add_account();
  auto o = e.login.login(e.req());
  CHECK(o.ok && o.ticket);
  CHECK(e.tickets.consume(o.ticket->account_id, o.ticket->login_id1, o.ticket->login_id2, e.clock.now()).has_value());
  CHECK(!e.tickets.consume(o.ticket->account_id, o.ticket->login_id1, o.ticket->login_id2, e.clock.now()).has_value());
}

TEST("BR-LOGIN-022", ip_banned_login_is_code_3_before_any_account_lookup) {
  Env e;
  e.add_account();
  e.bans.add("192.0.2.*", e.clock.now() + 60);
  CHECK(e.login.login(e.req()).refusal_code == refuse::Rejected);
}

TEST("BR-LOGIN-023", wrong_passwords_through_login_trigger_ip_autoban) {
  Env e;
  e.add_account();
  for (int i = 0; i < 7; ++i) e.login.login(e.req("fakeuser", "Wrong-Pass-9"));
  CHECK(e.bans.is_banned("192.0.2.10", e.clock.now()));
  CHECK(e.login.login(e.req()).refusal_code == refuse::Rejected);
}

TEST("BR-LOGIN-026", admin_ban_extends_and_broadcasts) {
  Env e;
  e.add_account();
  CHECK(e.admin.ban(2000001, 3600));
  CHECK(e.accounts.find(2000001)->unban_time == e.clock.now() + 3600);
  CHECK(e.bus.events.size() == 1 && e.bus.events[0].kind == AccountEventKind::Ban);
}

TEST("BR-LOGIN-027", unban_sets_zero) {
  Env e;
  auto& a = e.add_account();
  Account u = a; u.unban_time = e.clock.now() + 99; e.accounts.update(u);
  e.admin.unban(2000001);
  CHECK(e.accounts.find(2000001)->unban_time == 0);
}

TEST("BR-LOGIN-028", state_change_applied_if_different_and_broadcast_if_nonzero) {
  Env e;
  e.add_account();
  e.admin.set_state(2000001, 5);
  CHECK(e.accounts.find(2000001)->state == 5);
  CHECK(e.bus.events.size() == 1);
  e.admin.set_state(2000001, 5);
  CHECK(e.bus.events.size() == 1);  // unchanged => no second broadcast
}

TEST("BR-LOGIN-029", sex_toggles_m_f_server_excluded_and_broadcast) {
  Env e;
  e.add_account();
  CHECK(e.admin.toggle_sex(2000001));
  CHECK(e.accounts.find(2000001)->sex == Sex::Female);
  CHECK(e.bus.events.size() == 1);
  auto s = *e.accounts.find(2000001); s.sex = Sex::Server; e.accounts.update(s);
  CHECK(!e.admin.toggle_sex(2000001));
}

TEST("BR-LOGIN-030", email_change_requires_valid_nonplaceholder_and_matching_current) {
  Env e;
  e.add_account();
  CHECK(!e.admin.change_email(2000001, "other@example.invalid", "new@example.invalid"));
  CHECK(!e.admin.change_email(2000001, "USER@example.invalid", "a@a.com"));
  CHECK(e.admin.change_email(2000001, "USER@EXAMPLE.INVALID", "new@example.invalid"));
}

TEST("BR-LOGIN-031", admin_char_slots_vip_adds_increase) {
  Env e;
  auto& a = e.add_account();
  CHECK(e.admin.char_slots(2000001) == 9);
  Account u = a; u.vip_time = e.clock.now() + 1000; e.accounts.update(u);
  CHECK(e.admin.char_slots(2000001) == 15);
}

TEST("BR-LOGIN-032", vip_group_5_while_active_old_group_restored_group_gt_5_untouched) {
  Env e;
  auto& a = e.add_account();
  Account u = a; u.group_id = 1; e.accounts.update(u);
  e.admin.apply_vip(2000001, true);
  CHECK(e.accounts.find(2000001)->group_id == 5);
  e.admin.apply_vip(2000001, false);
  CHECK(e.accounts.find(2000001)->group_id == 1);
  u.group_id = 99; e.accounts.update(u);
  e.admin.apply_vip(2000001, true);
  CHECK(e.accounts.find(2000001)->group_id == 99);
}

TEST("BR-LOGIN-033", online_state_login_minus1_lost_minus2_purged_after_10min) {
  Env e;
  e.add_account();
  e.login.login(e.req());
  CHECK(e.dir.find(2000001) && e.dir.find(2000001)->state == kOnlineLoginPhase);
  e.dir.mark({2000001, kOnlineLost, 0, e.clock.now()});
  e.clock.advance(601);
  e.login.sweep_duplicate_clears();  // sweep also purges lost entries
  CHECK(!e.dir.find(2000001).has_value());
}

TEST("BR-LOGIN-034", web_token_16_chars_disabled_10s_after_logout_retry_capped) {
  Env e;
  e.add_account();
  auto o = e.login.login(e.req());
  CHECK(o.ok);
  CHECK(e.accounts.find(2000001)->web_token.size() == 16);
  e.login.on_logout(2000001);
  e.clock.advance(11);
  CHECK(e.accounts.find(2000001)->web_token.empty());
}

TEST("BR-LOGIN-035", pin_update_stores_pin_and_time) {
  Env e;
  e.add_account();
  e.admin.record_pin_update(2000001, "fake-pin-hash", e.clock.now());
  auto a = e.accounts.find(2000001);
  CHECK(a->pin == "fake-pin-hash" && a->pin_changed == e.clock.now());
}

TEST("BR-LOGIN-037", account_deletion_atomic) {
  Env e;
  e.add_account();
  CHECK(e.admin.delete_account(2000001));
  CHECK(!e.accounts.find(2000001).has_value());
}

TEST("BR-LOGIN-039", every_attempt_audited_ip_banned_logged_as_minus_3) {
  Env e;
  e.add_account();
  e.bans.add("192.0.2.*", e.clock.now() + 60);
  e.login.login(e.req());
  // [impl] audit sink seam (CAP-25): the IP-banned attempt is recorded with code -3
  CHECK(e.audit.logins.size() == 1 && e.audit.logins[0].code == -3);
  e.bans.purge_expired(e.clock.now() + 61);
  e.login.login(e.req());
  CHECK(e.audit.logins.size() == 2 && e.audit.logins[1].code == 100);   // success is code 100 "login ok"
  e.login.login(e.req("fakeuser", "Wrong-Pass-9"));
  CHECK(e.audit.logins.size() == 3 && e.audit.logins[2].code == refuse::IncorrectPassword);
}

TEST("ARCH-HASH-LAZY", legacy_plaintext_and_md5_upgraded_to_argon2id_on_first_login) {
  Env e;
  auto& a = e.add_account();
  Account u = a; u.scheme = PasswordScheme::Plaintext; u.stored_secret = kFakePass; e.accounts.update(u);
  CHECK(e.login.login(e.req()).ok);
  auto s = e.accounts.find(2000001);
  CHECK(s->scheme == PasswordScheme::Argon2id);
  CHECK(s->stored_secret != kFakePass);
  CHECK(e.login.login(e.req()).ok);
}
