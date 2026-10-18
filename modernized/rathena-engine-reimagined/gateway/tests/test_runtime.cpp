// Roles, configuration parsing, the gateway-owned runtime (IO queue, rate limits) and the loopback
// WorldBus transport.
#include <atomic>
#include <condition_variable>
#include <fstream>
#include <functional>
#include <mutex>
#include <sstream>
#include <thread>

#include "fixture.hpp"
#include "gateway/loopback.hpp"
#include "gateway/runtime.hpp"

using namespace gt;

// ---- roles and configuration ---------------------------------------------------------------------------
TEST("ARCH-ROLE", cli_parsing_precedence_and_errors) {
  auto o = parse_cli({});
  CHECK(o.error.empty() && o.role == Role::All && !o.role_from_cli && !o.check_only && o.run_for_ms == 0);
  o = parse_cli({"--role=gateway", "--config=/etc/x.toml", "--check", "--run-for=250"});
  CHECK(o.error.empty() && o.role == Role::Gateway && o.role_from_cli && o.config_path == "/etc/x.toml" && o.check_only && o.run_for_ms == 250);
  o = parse_cli({"--role=world", "--role=all"});
  CHECK(o.role == Role::All);                                                  // later wins
  for (const char* bad : {"--role=map", "--role=", "--role", "--Role=all", "--config=", "--run-for=abc", "--run-for=", "--verbose", "gateway", "-role=all"})
    CHECK(!parse_cli({bad}).error.empty());
  CHECK(!parse_cli({"--role=all", "--oops"}).error.empty());
  CHECK(std::string(role_name(Role::World)) == "world");
}

TEST("ARCH-ROLE", role_from_config_text_and_process_plan) {
  std::string err;
  CHECK(role_from_config_text("[process]\nrole = \"gateway\"\n", &err) == Role::Gateway && err.empty());
  CHECK(role_from_config_text("# c\n[policy]\nrole = \"world\"\n[process]\n  role=world # x\n", &err) == Role::World);
  CHECK(!role_from_config_text("[policy]\nrole = \"world\"\n", &err).has_value() && err.empty());   // wrong section: not a role
  CHECK(!role_from_config_text("[process]\nrole = \"mapp\"\n", &err).has_value() && !err.empty());
  const auto all = plan_for(Role::All), gw = plan_for(Role::Gateway), wo = plan_for(Role::World);
  CHECK(all.gateway_threads && all.world_side && !all.needs_world_peer);
  CHECK(gw.gateway_threads && !gw.world_side && gw.needs_world_peer);
  CHECK(!wo.gateway_threads && wo.world_side && !wo.needs_world_peer);
}

TEST("SEC-S1", config_file_parser_typed_values_unknown_keys_and_env_placeholders) {
  GatewayConfig c;
  const std::string good =
      "# comment\n[storage]\ndatabase_url = \"${DATABASE_URL}\"\n[policy]\nticket_ttl_s = 45\nnew_account = true\n"
      "char_del_delay_s = 7200\nchar_del_level = -5\nmax_connect_user = 100\n[hashing]\nargon2_queue_cap = 8\n"
      "pbkdf2_iterations = 1000\nargon2_memory_kib = 64\n[pin]\npepper = \"${GATEWAY_PIN_PEPPER}\"\n";
  CHECK(apply_config_text(c, good).empty());
  CHECK(c.ticket_ttl_s == 45 && c.new_account && c.char_del_delay_s == 7200 && c.char_del_level == -5 && c.max_connect_user == 100);
  CHECK(c.argon2_queue_cap == 8 && c.pbkdf2_iterations == 1000 && c.argon2_memory_kib == 64);
  CHECK(c.database_url == "${DATABASE_URL}" && c.pin_pepper == "${GATEWAY_PIN_PEPPER}");           // placeholders stay placeholders
  CHECK(apply_config_text(c, "[policy]\nnope = 1\n").find("unknown key 'policy.nope'") != std::string::npos);
  CHECK(apply_config_text(c, "[policy]\nticket_ttl_s = \"x\"\n").find("wrong value type") != std::string::npos);
  CHECK(apply_config_text(c, "[policy]\nnew_account = maybe\n").find("line 2") != std::string::npos);
  CHECK(apply_config_text(c, "[policy\n").find("bad section") != std::string::npos);
  CHECK(apply_config_text(c, "just words\n").find("expected key = value") != std::string::npos);
  CHECK(apply_config_text(c, "[hashing]\npbkdf2_iterations = -1\n").find("wrong value type") != std::string::npos);
  // the shipped example file parses cleanly into the typed config
  std::ifstream f(std::string(GATEWAY_SOURCE_DIR) + "/config/gateway.example.toml");
  std::stringstream ss; ss << f.rdbuf();
  GatewayConfig d;
  const std::string text = ss.str();   // includes [process]; the typed parser accepts it
  CHECK(apply_config_text(d, text).empty());
  CHECK(d.ip_ban_failures == 7 && d.ip_ban_window_s == 300 && d.chars_per_account == 9);
}

// ---- rate limiter and runtime ------------------------------------------------------------------------------------------
TEST("SEC-OVERLOAD", rate_limiter_windows_and_bounded_memory) {
  RateLimiter r(3, 10, 4);
  CHECK(r.allow("a", 100) && r.allow("a", 100) && r.allow("a", 101) && !r.allow("a", 102));
  CHECK(r.allow("b", 102));                                                    // per key
  CHECK(r.allow("a", 110));                                                    // window over
  CHECK(r.allow("c", 110) && r.allow("d", 110));
  CHECK(r.keys() == 4);
  CHECK(!r.allow("e", 111));                                                   // full of live windows: new keys are refused, memory stays bounded
  CHECK(r.allow("e", 125) && r.keys() <= 4);                                   // expired windows are reclaimed first
}

TEST("SEC-OVERLOAD", runtime_queue_is_bounded_and_every_login_gets_exactly_one_answer) {
  ManualClock clock;
  RuntimeOptions opt; opt.io_threads = 1; opt.io_queue_cap = 4; opt.rate_max_attempts = 1000;
  GatewayRuntime rt(clock, opt);
  std::mutex m; std::condition_variable cv; bool release = false;
  std::atomic<int> started{0};
  CHECK(rt.submit([&] { ++started; std::unique_lock<std::mutex> lk(m); cv.wait(lk, [&] { return release; }); }));
  while (started.load() < 1) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  int accepted = 0;
  for (int i = 0; i < 10; ++i) if (rt.submit([] {})) ++accepted;
  CHECK(accepted == 4 && rt.stats().refused_queue_full == 6 && rt.stats().max_queued == 4);
  // login admission: queue full => refusal answered inline with Overloaded, exactly once
  Env e;
  e.add_account();
  std::atomic<int> answers{0};
  LoginOutcome seen;
  rt.submit_login(e.login, e.req(), [&](const LoginOutcome& o) { ++answers; seen = o; });
  CHECK(answers.load() == 1 && !seen.ok && seen.reason == Reason::Overloaded && seen.refusal_code == refuse::ServerClosed);
  { std::lock_guard<std::mutex> g(m); release = true; }
  cv.notify_all();
  rt.stop();
  CHECK(answers.load() == 1);
}

TEST("SEC-OVERLOAD", per_address_rate_limit_refuses_inline_and_leaves_other_addresses_alone) {
  ManualClock clock;
  RuntimeOptions opt; opt.io_threads = 2; opt.rate_max_attempts = 3; opt.rate_window_s = 10;
  GatewayRuntime rt(clock, opt);
  Env e;
  e.add_account();
  GatewayDeps d = e.deps();
  d.strand = rt.strand();
  LoginService svc(d);
  std::atomic<int> ok{0}, limited{0}, other{0};
  auto cb = [&](const LoginOutcome& o) { if (o.reason == Reason::RateLimited) ++limited; else ++other; if (o.ok) ++ok; };
  for (int i = 0; i < 6; ++i) rt.submit_login(svc, e.req("fakeuser", "wrong"), cb);
  auto fresh = e.req("fakeuser", kFakePass); fresh.ip = "198.51.100.9";
  rt.submit_login(svc, fresh, cb);
  rt.stop();                                                                   // drains the queue
  CHECK(limited.load() == 3 && other.load() == 4 && ok.load() == 1);
  CHECK(rt.stats().refused_rate == 3);
}

// ---- loopback WorldBus -------------------------------------------------------------------------------------------------------
TEST("ARCH-SEAMS", loopback_bus_delivers_in_order_to_the_sim_side_and_publishes_membership) {
  LoopbackWorldBus bus(8);
  CHECK(bus.available());
  bus.kick(1);
  bus.broadcast({AccountEventKind::Ban, 2, 99});
  bus.purge_character_data(7, {"a", "b"});
  CHECK(bus.queued() == 3);
  std::vector<LoopbackWorldBus::Message> got;
  CHECK(bus.drain(2, [&](const LoopbackWorldBus::Message& m) { got.push_back(m); }) == 2);
  CHECK(got[0].kind == LoopbackWorldBus::Message::Kind::Kick && got[0].account_id == 1);
  CHECK(got[1].kind == LoopbackWorldBus::Message::Kind::Account && got[1].event.value == 99);
  CHECK(bus.drain(10, [&](const LoopbackWorldBus::Message& m) { got.push_back(m); }) == 1);
  CHECK(got[2].kind == LoopbackWorldBus::Message::Kind::Purge && got[2].char_id == 7 && got[2].plan == (std::vector<std::string>{"a", "b"}));
  CHECK(bus.membership(5).party_id == 0);
  bus.publish_membership({{5, {3, 4}}});
  CHECK(bus.membership(5).party_id == 3 && bus.membership(5).guild_id == 4);
  bus.set_available(false);
  CHECK(!bus.available());
}

TEST("SEC-OVERLOAD", loopback_mailbox_is_bounded_and_refuses_instead_of_blocking_or_dropping) {
  LoopbackWorldBus bus(3);
  for (int i = 0; i < 3; ++i) bus.kick(static_cast<AccountId>(i));
  bool threw = false;
  try { bus.kick(9); } catch (const StoreUnavailable&) { threw = true; }
  CHECK(threw && bus.refused() == 1 && bus.queued() == 3);
  // through the services: a kick that cannot be queued makes the login a refusal, not a lost kick
  Env e;
  e.add_account();
  e.dir.mark({2000001, 0, 5, e.clock.now()});
  GatewayDeps d{e.cfg, e.clock, e.rng, e.accounts, e.chars, e.dir, bus, e.tickets, e.hasher, e.bans, e.fails, e.policy};
  LoginService svc(d);
  auto o = svc.login(e.req());
  CHECK(!o.ok && o.refusal_code == refuse::ServerClosed);
  bus.drain(10, [](const LoopbackWorldBus::Message&) {});
  CHECK(svc.login(e.req()).refusal_code == refuse::AlreadyOnline);              // room again: the duplicate path works
}

TEST("ARCH-SEAMS", services_run_unchanged_on_the_loopback_bus_and_the_world_side_drains_them) {
  LoopbackWorldBus bus;
  Env e;
  auto& a = e.add_account();
  CharId c = e.add_char(a.id, "Bobby", 0);
  GatewayDeps d{e.cfg, e.clock, e.rng, e.accounts, e.chars, e.dir, bus, e.tickets, e.hasher, e.bans, e.fails, e.policy};
  AccountAdmin admin(d);
  CharService chr(d);
  LoginService login(d);
  CHECK(admin.ban(a.id, 100));
  e.cfg.char_del_delay_s = 0;
  auto s = e.session(a);
  CHECK(chr.confirm_delete(s, c, "900101").ok);
  std::vector<LoopbackWorldBus::Message::Kind> kinds;
  bus.drain(10, [&](const LoopbackWorldBus::Message& m) { kinds.push_back(m.kind); });
  CHECK((kinds == std::vector<LoopbackWorldBus::Message::Kind>{LoopbackWorldBus::Message::Kind::Account, LoopbackWorldBus::Message::Kind::Purge}));
  bus.set_available(false);
  admin.unban(a.id);
  CHECK(login.login(e.req()).reason == Reason::WorldDown);                       // role=gateway without a world peer: closed, not broken
}
