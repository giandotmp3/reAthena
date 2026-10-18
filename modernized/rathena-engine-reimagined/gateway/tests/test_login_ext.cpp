// Login pipeline: behaviours beyond the original per-rule placeholders. Derived from login.cpp,
// loginclif.cpp, loginchrif.cpp, ipban.cpp, account.cpp (see src/login_service.cpp).
#include "fixture.hpp"
#include "gateway/crypto.hpp"

using namespace gt;

namespace {
struct ReorderedPolicy : DefaultNamePolicy {
  std::vector<LoginCheck> order;
  std::vector<LoginCheck> login_check_order() const override { return order; }
};
// Counts how many tokens are probed and always reports "in use": forces the retry cap.
struct CountingStore : InProcAccountStore {
  mutable int probes = 0;
  bool web_token_in_use(const std::string&) const override { ++probes; return true; }
};
struct ConstRng : Rng { std::uint32_t next_u32() override { return 0x12345678u; } };
}  // namespace

// ---- BR-LOGIN-001: the order ---------------------------------------------------------------------------
TEST("BR-LOGIN-001", each_check_returns_its_own_code_in_the_documented_order) {
  Env e;
  e.cfg.use_dnsbl = true;
  e.cfg.client_hash_check = true;
  e.cfg.client_hash_nodes = {{0, "113e195e6c051bb1cfb12a644bb084c5"}};
  e.dnsbl.hits.insert("203.0.113.5");
  auto base = [&] { auto r = e.req(); r.client_hash = "113e195e6c051bb1cfb12a644bb084c5"; return r; };
  // unknown account -> 0 ; DNSBL beats it with 3
  auto r = base(); r.userid = "nobody";
  CHECK(e.login.login(r).refusal_code == refuse::UnregisteredId);
  r.ip = "203.0.113.5";
  CHECK(e.login.login(r).refusal_code == refuse::Rejected);
  auto& a = e.add_account();
  // an account that fails EVERY later check reports them one after another as each is fixed
  Account u = a;
  u.sex = Sex::Server;
  u.expiration_time = e.clock.now() - 5;
  u.unban_time = e.clock.now() + 50;
  u.state = 7;
  e.accounts.update(u);
  r = base();
  CHECK(e.login.login(r).refusal_code == refuse::UnregisteredId);          // server account
  u.sex = Sex::Male; e.accounts.update(u);
  auto bad = base(); bad.secret = "Wrong-Pass-9";
  CHECK(e.login.login(bad).refusal_code == refuse::IncorrectPassword);     // wrong password beats the rest
  CHECK(e.login.login(r).refusal_code == refuse::Expired);                 // then expired
  u.expiration_time = 0; e.accounts.update(u);
  auto o = e.login.login(r);
  CHECK(o.refusal_code == refuse::Banned && o.unban_time == u.unban_time); // then banned
  u.unban_time = 0; e.accounts.update(u);
  CHECK(e.login.login(r).refusal_code == 6);                               // then state 7 -> code 6
  u.state = 0; e.accounts.update(u);
  auto wrong_hash = base(); wrong_hash.client_hash = "00000000000000000000000000000000";
  CHECK(e.login.login(wrong_hash).refusal_code == refuse::ClientHash);     // then the client hash
  CHECK(e.login.login(r).ok);
}

TEST("BR-LOGIN-001", check_order_is_configurable_but_must_be_a_permutation) {
  Env e;
  auto& a = e.add_account();
  Account u = a; u.unban_time = e.clock.now() + 100; e.accounts.update(u);
  ReorderedPolicy p;
  p.order = {LoginCheck::Dnsbl, LoginCheck::AutoCreate, LoginCheck::UnknownAccount, LoginCheck::ServerAccount,
             LoginCheck::Banned, LoginCheck::Password, LoginCheck::Expired, LoginCheck::State, LoginCheck::ClientHash};
  GatewayDeps d{e.cfg, e.clock, e.rng, e.accounts, e.chars, e.dir, e.bus, e.tickets, e.hasher, e.bans, e.fails, p};
  LoginService svc(d);
  CHECK(svc.login(e.req("fakeuser", "Wrong-Pass-9")).refusal_code == refuse::Banned);   // banned now precedes password
  p.order = {LoginCheck::Banned};                                                        // not a permutation => default order
  CHECK(svc.login(e.req("fakeuser", "Wrong-Pass-9")).refusal_code == refuse::IncorrectPassword);
  p.order = {LoginCheck::Banned, LoginCheck::Banned, LoginCheck::Password, LoginCheck::Dnsbl, LoginCheck::AutoCreate,
             LoginCheck::UnknownAccount, LoginCheck::ServerAccount, LoginCheck::State, LoginCheck::ClientHash};
  CHECK(svc.login(e.req("fakeuser", "Wrong-Pass-9")).refusal_code == refuse::IncorrectPassword);   // duplicate entry => default
  p.order = {LoginCheck::Password};                                                      // a pipeline can never skip authentication
  CHECK(!svc.login(e.req("fakeuser", "Wrong-Pass-9")).ok);
}

TEST("BR-LOGIN-001", ip_ban_is_checked_before_dnsbl_and_account_lookup) {
  Env e;
  e.cfg.use_dnsbl = true;
  e.add_account();
  e.bans.add("192.0.2.*", e.clock.now() + 60);
  auto o = e.login.login(e.req("does-not-exist"));
  CHECK(o.refusal_code == refuse::Rejected && o.reason == Reason::IpBanned);
  CHECK(e.dnsbl.calls == 0);
}

// ---- BR-LOGIN-002 / 003 / 004: secrets ---------------------------------------------------------------
TEST("BR-LOGIN-003", md5_challenge_edge_cases) {
  Env e;
  auto& a = e.add_account();
  auto challenge = [&](const std::string& secret, bool prefix_salt, const std::string& salt = "salt") {
    auto r = e.req(); r.kind = LoginKind::Md5Challenge; r.challenge_salt = salt;
    r.secret = e.hasher.md5_hex(prefix_salt ? salt + secret : secret + salt);
    return r;
  };
  CHECK(e.login.login(challenge(kFakePass, true)).ok);
  e.dir.erase(a.id);
  CHECK(!e.login.login(challenge("Fake-Pass-2", true)).ok);                                // wrong secret
  CHECK(!e.login.login(challenge(kFakePass, true, "other-salt")).ok == false);             // matches its own salt
  auto no_salt = challenge(kFakePass, true); no_salt.challenge_salt.clear();
  CHECK(!e.login.login(no_salt).ok);                                                        // no session salt => no challenge
  auto empty = e.req(); empty.kind = LoginKind::Md5Challenge; empty.challenge_salt = "salt"; empty.secret.clear();
  CHECK(!e.login.login(empty).ok);
  // an account without a challenge secret cannot use the shim (spec section 3)
  Account u = a; u.challenge_secret.clear(); e.accounts.update(u);
  CHECK(e.login.login(challenge(kFakePass, true)).refusal_code == refuse::IncorrectPassword);
  // legacy plaintext rows can act as the challenge secret until they are upgraded
  u.scheme = PasswordScheme::Plaintext; u.stored_secret = "Secret1"; e.accounts.update(u);
  CHECK(e.login.login(challenge("Secret1", false)).ok);
  // disabled by configuration
  e.dir.erase(a.id);
  e.cfg.md5_challenge_enabled = false;
  CHECK(!e.login.login(challenge("Secret1", false)).ok);
}

TEST("BR-LOGIN-002", sso_token_is_checked_like_a_case_sensitive_password) {
  Env e;
  e.add_account();
  auto r = e.req(); r.kind = LoginKind::Sso; r.secret = kFakePass;
  CHECK(e.login.login(r).ok);
  r.secret = "fake-pass-1";
  CHECK(!e.login.login(r).ok);
}

TEST("BR-LOGIN-004", challenge_secret_never_replaces_the_hashed_password_row) {
  Env e;
  auto& a = e.add_account();
  const std::string before = a.stored_secret;
  auto r = e.req(); r.kind = LoginKind::Md5Challenge; r.challenge_salt = "salt";
  r.secret = e.hasher.md5_hex("salt" + std::string(kFakePass));
  CHECK(e.login.login(r).ok);
  auto s = e.accounts.find(a.id);
  CHECK(s->scheme == PasswordScheme::Argon2id && s->stored_secret == before);
}

// ---- BR-LOGIN-006/007/008: registration -------------------------------------------------------------------
TEST("BR-LOGIN-006", registration_variants_and_refusal_codes) {
  Env e;
  e.cfg.new_account = true;
  e.cfg.allowed_regs = 100;
  auto reg = [&](const std::string& user, const std::string& pw, LoginKind k = LoginKind::Plaintext) {
    auto r = e.req(user, pw); r.kind = k; r.challenge_salt = "s"; return e.login.login(r);
  };
  CHECK(reg("alice1_f", "pass12").ok);                                   // lower-case suffix accepted
  CHECK(e.accounts.find_by_userid("alice1", true)[0].sex == Sex::Female);
  CHECK(e.accounts.find_by_userid("alice1", true)[0].stored_secret != "pass12");                         // hashed at rest
  CHECK(e.hasher.verify(PasswordScheme::Argon2id, e.accounts.find_by_userid("alice1", true)[0].stored_secret, "pass12").ok);
  CHECK(e.accounts.find_by_userid("alice1", true)[0].email == "a@a.com");
  CHECK(e.accounts.find_by_userid("alice1", true)[0].char_slots == e.cfg.min_chars);
  CHECK(reg("abc_M", "pass12").refusal_code == refuse::IncorrectPassword);       // name too short (min 4)
  CHECK(reg("bobby2_M", "abc").refusal_code == refuse::IncorrectPassword);       // password too short
  CHECK(reg("carol_X", "pass12").refusal_code == refuse::UnregisteredId);        // no valid suffix: plain lookup => unknown
  auto enc = reg("dave12_M", "0123456789abcdef0123456789abcdef", LoginKind::Md5Challenge);
  CHECK(!enc.ok && enc.refusal_code == refuse::UnregisteredId && enc.reason == Reason::RegistrationEncrypted);
  CHECK(e.accounts.find_by_userid("dave12", true).empty());
  e.cfg.new_account = false;
  CHECK(reg("erin12_M", "pass12").refusal_code == refuse::UnregisteredId);       // feature off
}

TEST("BR-LOGIN-007", registration_window_reopens_after_time_allowed) {
  Env e;
  e.cfg.new_account = true;
  e.cfg.allowed_regs = 2;
  e.cfg.reg_window_s = 10;
  CHECK(e.login.login(e.req("user01_M", "pass12")).ok);
  CHECK(e.login.login(e.req("user02_M", "pass12")).ok);
  CHECK(e.login.login(e.req("user03_M", "pass12")).refusal_code == refuse::Rejected);
  e.clock.advance(10);
  CHECK(e.login.login(e.req("user03_M", "pass12")).ok);
}

TEST("BR-LOGIN-008", new_account_id_is_max_plus_one_or_the_floor) {
  Env e;
  e.cfg.new_account = true;
  e.cfg.allowed_regs = 100;
  e.add_account("old", 2000500);
  CHECK(e.login.login(e.req("fresh1_M", "pass12")).account_id == 2000501);
  Env f;
  f.cfg.new_account = true;
  Account tiny; tiny.id = 17; tiny.userid = "tiny"; f.accounts.create(tiny);
  CHECK(f.login.login(f.req("fresh2_M", "pass12")).account_id == 2000000);
  Env g;
  g.cfg.new_account = true;
  Account top; top.id = 100000000; top.userid = "top"; g.accounts.create(top);
  CHECK(!g.login.login(g.req("fresh3_M", "pass12")).ok);                         // id would exceed 100,000,000
}

TEST("BR-LOGIN-006", registration_sets_expiry_from_start_limited_time) {
  Env e;
  e.cfg.new_account = true;
  e.cfg.start_limited_time = 3600;
  CHECK(e.login.login(e.req("limit1_M", "pass12")).ok);
  CHECK(e.accounts.find_by_userid("limit1", true)[0].expiration_time == e.clock.now() + 3600);
}

// ---- BR-LOGIN-038: lookup --------------------------------------------------------------------------------
TEST("BR-LOGIN-038", more_than_one_match_denies_and_case_policy_is_configurable) {
  Env e;
  e.add_account("Bobby", 2000001);
  e.add_account("bobby", 2000002);
  e.cfg.case_sensitive_userid = true;
  CHECK(e.login.login(e.req("Bobby")).ok);                               // exact match
  CHECK(e.login.login(e.req("BOBBY")).refusal_code == refuse::UnregisteredId);
  e.cfg.case_sensitive_userid = false;
  auto o = e.login.login(e.req("bobby"));                                // two accounts match case-insensitively
  CHECK(!o.ok && o.refusal_code == refuse::UnregisteredId);
}

// ---- BR-LOGIN-013/016: gates ---------------------------------------------------------------------------------
TEST("BR-LOGIN-013", client_hash_nodes_by_group_and_disabled_node) {
  Env e;
  auto& a = e.add_account();
  e.cfg.client_hash_check = true;
  e.cfg.client_hash_nodes = {{10, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"}, {0, "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"}};
  auto r = e.req();
  CHECK(e.login.login(r).refusal_code == refuse::ClientHash);            // no hash sent
  r.client_hash = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";
  CHECK(e.login.login(r).refusal_code == refuse::ClientHash);            // group 0 may not use the group-10 hash
  r.client_hash = "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB";
  CHECK(e.login.login(r).ok);                                            // hex compared case-insensitively
  Account u = a; u.group_id = 10; e.accounts.update(u);
  r.client_hash = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
  CHECK(e.login.login(r).ok);
  e.cfg.client_hash_nodes = {{0, ""}};                                   // "disabled": any client
  r.client_hash.clear();
  CHECK(e.login.login(r).ok);
}

TEST("BR-LOGIN-016", exact_group_wins_over_minimum_group) {
  Env e;
  e.add_account();
  e.cfg.group_id_to_connect = 0;
  e.cfg.min_group_id_to_connect = 50;   // ignored while group_id_to_connect is set
  CHECK(e.login.login(e.req()).ok);
  e.cfg.group_id_to_connect = 3;
  auto o = e.login.login(e.req());
  CHECK(!o.ok && o.refusal_code == refuse::ServerClosed && o.reason == Reason::GroupGate);
}

// ---- BR-LOGIN-015: success side effects ----------------------------------------------------------------------
TEST("BR-LOGIN-015", refused_logins_leave_the_account_untouched_and_success_rotates_ids) {
  Env e;
  auto& a = e.add_account();
  e.dir.mark({a.id, 0, 150000, e.clock.now()});                          // in the world: next login is refused with 8
  const Account before = *e.accounts.find(a.id);
  CHECK(e.login.login(e.req()).refusal_code == refuse::AlreadyOnline);
  const Account after = *e.accounts.find(a.id);
  CHECK(after.logincount == before.logincount && after.login_id1 == before.login_id1 && after.last_ip == before.last_ip);
  e.dir.erase(a.id);
  auto o1 = e.login.login(e.req());
  e.clock.advance(1);
  auto o2 = e.login.login(e.req());
  CHECK(o1.ok && o2.ok);
  CHECK(o1.ticket->login_id1 != 0 && o1.ticket->login_id2 != 0);
  CHECK(o1.ticket->login_id1 != o2.ticket->login_id1);                   // fresh random ids each time
  CHECK(e.accounts.find(a.id)->logincount == 2);
  CHECK(e.accounts.find(a.id)->lastlogin == e.clock.now());
}

// ---- BR-LOGIN-018/019: duplicates and tickets -------------------------------------------------------------------
TEST("BR-LOGIN-018", login_phase_session_is_wiped_by_a_newer_login_later_login_wins) {
  Env e;
  e.add_account();
  auto first = e.login.login(e.req());
  auto second = e.login.login(e.req());
  CHECK(first.ok && second.ok && !second.kicked_existing_session);
  CHECK(e.bus.kicks.empty());                                            // nobody was in the world
  CHECK(!e.tickets.consume(first.account_id, first.ticket->login_id1, first.ticket->login_id2, e.clock.now()));
  CHECK(e.tickets.consume(second.account_id, second.ticket->login_id1, second.ticket->login_id2, e.clock.now()));
}

TEST("BR-LOGIN-018", repeated_refusals_arm_exactly_one_forced_clear) {
  Env e;
  e.add_account();
  e.dir.mark({2000001, 0, 150000, e.clock.now()});
  for (int i = 0; i < 5; ++i) CHECK(e.login.login(e.req()).refusal_code == refuse::AlreadyOnline);
  CHECK(e.bus.kicks.size() == 5);                                         // every attempt kicks again
  CHECK(e.login.pending_timers() == 1);                                   // one waiting_disconnect per account
  e.clock.advance(29);
  CHECK(e.dir.find(2000001).has_value());
  e.clock.advance(2);
  CHECK(!e.dir.find(2000001).has_value());
  CHECK(e.login.pending_timers() == 0);
  CHECK(e.login.login(e.req()).ok);                                       // free again
}

TEST("BR-LOGIN-019", login_ticket_is_login_stage_30_seconds_and_single_use) {
  Env e;
  e.add_account();
  auto o = e.login.login(e.req());
  CHECK(o.ticket->stage == AuthStage::Login && o.ticket->issued_at == e.clock.now());
  CHECK(!e.tickets.consume_for(AuthStage::World, o.account_id, o.ticket->login_id1, nullptr, nullptr, e.clock.now()));   // not a world ticket
  CHECK(!e.tickets.consume_for(AuthStage::Login, o.account_id, o.ticket->login_id1, &o.ticket->login_id2, nullptr, e.clock.now() + 31));
  auto o2 = e.login.login(e.req());
  const Sex wrong = Sex::Female;
  CHECK(!e.tickets.consume_for(AuthStage::Login, o2.account_id, o2.ticket->login_id1, &o2.ticket->login_id2, &wrong, e.clock.now()));
  CHECK(e.tickets.consume_for(AuthStage::Login, o2.account_id, o2.ticket->login_id1, &o2.ticket->login_id2, nullptr, e.clock.now() + 30));
}

TEST("BR-LOGIN-033", login_phase_entry_expires_after_30_seconds_without_char_phase) {
  Env e;
  e.add_account();
  auto o = e.login.login(e.req());
  CHECK(e.dir.find(o.account_id)->state == kOnlineLoginPhase && e.dir.find(o.account_id)->gen == o.ticket->login_id1);
  e.clock.advance(29);
  CHECK(e.dir.find(o.account_id).has_value());
  e.clock.advance(2);
  CHECK(!e.dir.find(o.account_id).has_value());
  CHECK(!e.tickets.consume(o.account_id, o.ticket->login_id1, o.ticket->login_id2, e.clock.now()));
}

TEST("BR-LOGIN-033", sweep_purges_only_old_lost_entries_and_expired_ip_bans) {
  Env e;
  e.dir.mark({1, kOnlineLost, 0, e.clock.now()});
  e.dir.mark({2, kOnlineLost, 0, e.clock.now() - 700});
  e.dir.mark({3, 0, 7, e.clock.now() - 9000});                             // in the world: never purged here
  e.bans.add("9.9.9.*", e.clock.now() + 5);
  e.clock.advance(61);
  e.login.sweep_duplicate_clears();
  CHECK(e.dir.find(1).has_value() && !e.dir.find(2).has_value() && e.dir.find(3).has_value());
  CHECK(e.bans.size() == 0);                                               // BR-LOGIN-024
  e.clock.advance(600);
  e.login.sweep_duplicate_clears();
  CHECK(!e.dir.find(1).has_value());
}

// ---- BR-LOGIN-022/023: IP bans --------------------------------------------------------------------------------------
TEST("BR-LOGIN-023", failure_counter_works_with_no_audit_sink_at_all_defect_fix) {
  Env e;
  e.add_account();
  GatewayDeps d{e.cfg, e.clock, e.rng, e.accounts, e.chars, e.dir, e.bus, e.tickets, e.hasher, e.bans, e.fails, e.policy};
  LoginService svc(d);                                                     // no audit, no dnsbl: logging is off
  for (int i = 0; i < 6; ++i) CHECK(svc.login(e.req("fakeuser", "Wrong-Pass-9")).refusal_code == refuse::IncorrectPassword);
  CHECK(!e.bans.is_banned("192.0.2.10", e.clock.now()));
  CHECK(svc.login(e.req("fakeuser", "Wrong-Pass-9")).refusal_code == refuse::IncorrectPassword);   // 7th
  CHECK(e.bans.is_banned("192.0.2.77", e.clock.now()));                    // the whole /24
  CHECK(svc.login(e.req()).refusal_code == refuse::Rejected);              // even with the right password
  e.clock.advance(301);
  CHECK(svc.login(e.req()).ok);                                            // ban lasted ip_ban_duration_s
}

TEST("BR-LOGIN-023", unknown_accounts_count_but_other_refusals_do_not) {
  Env e;
  auto& a = e.add_account();
  Account u = a; u.state = 5; e.accounts.update(u);
  for (int i = 0; i < 10; ++i) CHECK(e.login.login(e.req()).refusal_code == 4);   // blocked by GM: not a password failure
  CHECK(!e.bans.is_banned("192.0.2.10", e.clock.now()));
  for (int i = 0; i < 7; ++i) e.login.login(e.req("ghost", "x"));                // result 0 counts like result 1
  CHECK(e.bans.is_banned("192.0.2.10", e.clock.now()));
}

TEST("BR-LOGIN-023", counter_can_be_switched_off_and_is_per_address) {
  Env e;
  e.add_account();
  e.cfg.dynamic_pass_failure_ban = false;
  for (int i = 0; i < 20; ++i) e.login.login(e.req("fakeuser", "nope"));
  CHECK(!e.bans.is_banned("192.0.2.10", e.clock.now()));
  e.cfg.dynamic_pass_failure_ban = true;
  auto other = e.req("fakeuser", "nope"); other.ip = "198.51.100.1";
  for (int i = 0; i < 6; ++i) e.login.login(other);
  CHECK(!e.bans.is_banned("198.51.100.1", e.clock.now()));
  CHECK(!e.bans.is_banned("192.0.2.10", e.clock.now()));
}

TEST("BR-LOGIN-022", ip_bans_disabled_by_config_and_fail_closed_on_backend_error) {
  Env e;
  e.add_account();
  e.bans.add("192.0.2.*", e.clock.now() + 60);
  e.cfg.ip_ban_enabled = false;
  CHECK(e.login.login(e.req()).ok);
  e.cfg.ip_ban_enabled = true;
  e.bans.purge_expired(e.clock.now() + 100);
  e.bans.backend_failing = true;
  auto o = e.login.login(e.req());
  CHECK(!o.ok && o.refusal_code == refuse::Rejected && o.reason == Reason::IpBanned);
}

// ---- BR-LOGIN-034: web token -------------------------------------------------------------------------------------
TEST("BR-LOGIN-034", token_is_16_hex_chars_unique_per_account_and_matches_the_outcome) {
  Env e;
  e.add_account("u1", 2000001);
  e.add_account("u2", 2000002);
  auto a = e.login.login(e.req("u1"));
  auto b = e.login.login(e.req("u2"));
  CHECK(a.web_token.size() == 16 && b.web_token.size() == 16 && a.web_token != b.web_token);
  for (char c : a.web_token) CHECK(std::string("0123456789abcdef").find(c) != std::string::npos);
  CHECK(e.accounts.find(2000001)->web_token == a.web_token && e.accounts.find(2000001)->web_token_enabled);
}

TEST("BR-LOGIN-034", retry_loop_is_capped_at_20_attempts_defect_fix) {
  GatewayConfig cfg;
  ManualClock clock;
  ConstRng rng;
  CountingStore accounts;
  InProcCharacterStore chars;
  InProcSessionDirectory dir;
  InProcWorldBus bus;
  InProcTicketStore tickets;
  FakeHasher hasher;
  InProcIpBanList bans;
  InProcFailureCounter fails(bans, 7, 300, 300);
  DefaultNamePolicy policy;
  GatewayDeps d{cfg, clock, rng, accounts, chars, dir, bus, tickets, hasher, bans, fails, policy};
  LoginService svc(d);
  Account a; a.id = 2000001; a.userid = "u"; a.stored_secret = hasher.hash(kFakePass);
  accounts.create(a);
  LoginRequest r; r.userid = "u"; r.secret = kFakePass; r.ip = "192.0.2.10";
  auto o = svc.login(r);
  CHECK(o.ok);                                   // the login still succeeds ...
  CHECK(o.web_token.empty());                    // ... without a token
  CHECK(accounts.probes == 20);                  // ... after exactly 20 attempts (legacy never advanced its counter)
  CHECK(accounts.find(2000001)->web_token.empty());
}

TEST("BR-LOGIN-034", logout_disables_after_10_seconds_unless_the_user_came_back) {
  Env e;
  e.add_account();
  auto o = e.login.login(e.req());
  e.login.on_logout(o.account_id);
  e.clock.advance(5);
  auto o2 = e.login.login(e.req());              // back within 10 s
  e.clock.advance(6);
  CHECK(e.accounts.find(o.account_id)->web_token == o2.web_token);       // the old timer did not clear the new token
  e.login.on_logout(o.account_id);
  e.login.on_logout(o.account_id);               // a second logout re-arms; only the newest timer acts
  e.clock.advance(11);
  CHECK(e.accounts.find(o.account_id)->web_token.empty() && !e.accounts.find(o.account_id)->web_token_enabled);
  e.login.startup();
  CHECK(e.accounts.find(o.account_id)->web_token.empty());
}

TEST("BR-LOGIN-034", startup_wipes_every_token) {
  Env e;
  e.add_account("u1", 2000001);
  e.add_account("u2", 2000002);
  e.login.login(e.req("u1"));
  e.login.login(e.req("u2"));
  e.login.startup();
  CHECK(e.accounts.find(2000001)->web_token.empty() && e.accounts.find(2000002)->web_token.empty());
}

// ---- BR-LOGIN-026..032, 035, 037: administration -------------------------------------------------------------------
TEST("BR-LOGIN-026", ban_arithmetic_through_the_service) {
  Env e;
  e.add_account();
  CHECK(!e.admin.ban(2000001, -500));                                    // would end in the past: ignored
  CHECK(e.accounts.find(2000001)->unban_time == 0 && e.bus.events.empty());
  CHECK(e.admin.ban(2000001, 1000));
  CHECK(e.admin.ban(2000001, 500));                                      // extends from the current end
  CHECK(e.accounts.find(2000001)->unban_time == e.clock.now() + 1500);
  CHECK(e.bus.events.size() == 2 && e.bus.events[1].value == e.clock.now() + 1500);
  CHECK(!e.admin.ban(2999999, 10));                                      // unknown account
  e.clock.advance(5000);                                                 // ban over: extends from now
  CHECK(e.admin.ban(2000001, 10));
  CHECK(e.accounts.find(2000001)->unban_time == e.clock.now() + 10);
}

TEST("BR-LOGIN-027", unban_is_silent_and_idempotent) {
  Env e;
  e.add_account();
  e.admin.unban(2000001);
  e.admin.ban(2000001, 100);
  e.bus.events.clear();
  e.admin.unban(2000001);
  e.admin.unban(2000001);
  CHECK(e.accounts.find(2000001)->unban_time == 0 && e.bus.events.empty());
}

TEST("BR-LOGIN-028", state_zero_is_applied_but_not_broadcast) {
  Env e;
  e.add_account();
  e.admin.set_state(2000001, 5);
  e.bus.events.clear();
  e.admin.set_state(2000001, 0);
  CHECK(e.accounts.find(2000001)->state == 0 && e.bus.events.empty());
  e.admin.set_state(999, 3);                                              // unknown account: no effect
}

TEST("BR-LOGIN-030", email_validity_rules) {
  // ported from e_mail_check: lax on purpose (same as legacy), strict about the listed cases
  CHECK(AccountAdmin::email_is_valid("user@example.invalid"));
  CHECK(AccountAdmin::email_is_valid("a@b"));
  CHECK(AccountAdmin::email_is_valid("we ird@x.com"));                     // spaces before the last '@' are tolerated
  for (const char* bad : {"", "a", "ab", "@", "a@", "nodomain.com", "a@b.", "a@.com", "a@b..c", "a@b c.com", "a@b;c.com"})
    CHECK(!AccountAdmin::email_is_valid(bad));
  CHECK(!AccountAdmin::email_is_valid(std::string("a@b\x01.com")));
  CHECK(!AccountAdmin::email_is_valid(std::string(40, 'x') + "@a.com"));
  Env e;
  e.add_account();
  CHECK(!e.admin.change_email(2000001, "user@example.invalid", "bad"));
  CHECK(!e.admin.change_email(2000001, "user@example.invalid", "A@A.COM"));      // the default address, any case
  CHECK(e.admin.change_email(2000001, "user@example.invalid", "new@example.invalid"));
  CHECK(e.accounts.find(2000001)->email == "new@example.invalid");
}

TEST("BR-LOGIN-031", slots_follow_vip_time_at_read_time_and_cap_at_max) {
  Env e;
  auto& a = e.add_account();
  Account u = a; u.char_slots = 12; e.accounts.update(u);
  CHECK(e.admin.char_slots(a.id) == 12);
  u.vip_time = e.clock.now() + 10; e.accounts.update(u);
  CHECK(e.admin.char_slots(a.id) == 15);                                   // 12 + 6 capped at max_chars
  e.clock.advance(11);
  CHECK(e.admin.char_slots(a.id) == 12);                                   // VIP expired: no stored-slot bookkeeping needed
  CHECK(e.admin.char_slots(424242) == 0);
}

TEST("BR-LOGIN-032", vip_expiry_restores_group_and_clears_vip_time) {
  Env e;
  auto& a = e.add_account();
  Account u = a; u.group_id = 2; u.vip_time = e.clock.now() + 100; e.accounts.update(u);
  e.admin.apply_vip(a.id, true);
  CHECK(e.accounts.find(a.id)->group_id == 5 && e.accounts.find(a.id)->old_group == 2);
  e.admin.apply_vip(a.id, true);                                           // idempotent: old group is not overwritten by 5
  CHECK(e.accounts.find(a.id)->old_group == 2);
  e.admin.apply_vip(a.id, false);
  auto s = e.accounts.find(a.id);
  CHECK(s->group_id == 2 && s->old_group == 0 && s->vip_time == 0);
}

TEST("BR-LOGIN-035", pin_update_resets_failures) {
  Env e;
  auto& a = e.add_account();
  Account u = a; u.pin_failures = 2; e.accounts.update(u);
  e.admin.record_pin_update(a.id, "h", e.clock.now());
  CHECK(e.accounts.find(a.id)->pin_failures == 0);
}

struct CountingPurger : AccountRegistryPurger {
  std::vector<AccountId> purged;
  void purge_account_registry(AccountId a) override { purged.push_back(a); }
};

TEST("BR-LOGIN-037", delete_account_removes_row_session_ticket_and_registry) {
  Env e;
  e.add_account();
  auto o = e.login.login(e.req());
  CountingPurger purger;
  GatewayDeps d = e.deps();
  d.registry = &purger;
  AccountAdmin admin(d);
  CHECK(admin.delete_account(2000001));
  CHECK(!e.accounts.find(2000001) && !e.dir.find(2000001));
  CHECK(!e.tickets.consume(o.account_id, o.ticket->login_id1, o.ticket->login_id2, e.clock.now()));
  CHECK(purger.purged == std::vector<AccountId>{2000001});
  CHECK(!admin.delete_account(2000001));                                   // already gone
}

TEST("BR-CHAR-055", gm_authority_rules) {
  Env e;
  auto& gm = e.add_account("gm", 2000010);
  auto& target = e.add_account("target", 2000011);
  auto& peer = e.add_account("peer", 2000012);
  Account g = gm; g.group_id = 10; e.accounts.update(g);
  Account p = peer; p.group_id = 10; e.accounts.update(p);
  CHECK(e.admin.gm_set_state(gm.id, target.id, 5));
  CHECK(e.accounts.find(target.id)->state == 5);
  CHECK(!e.admin.gm_set_state(gm.id, peer.id, 5));                         // same rank: refused
  CHECK(!e.admin.gm_set_state(target.id, gm.id, 0));                       // lower rank: refused
  CHECK(!e.admin.gm_set_state(999, target.id, 0));                         // unknown requester
  CHECK(!e.admin.gm_set_state(gm.id, 999, 0));                             // unknown target
  CHECK(e.admin.gm_ban(gm.id, target.id, 60) && e.accounts.find(target.id)->unban_time == e.clock.now() + 60);
  CHECK(!e.admin.gm_ban(target.id, gm.id, 60));
  CHECK(e.admin.gm_unban(gm.id, target.id) && e.accounts.find(target.id)->unban_time == 0);
  CHECK(e.admin.gm_toggle_sex(gm.id, target.id) && e.accounts.find(target.id)->sex == Sex::Female);
  CHECK(e.admin.gm_apply_vip(gm.id, target.id, true) && e.accounts.find(target.id)->group_id == 5);
  e.cfg.gm_min_group = 11;
  CHECK(!e.admin.gm_set_state(gm.id, target.id, 0));                       // raising the bar revokes the power
}
