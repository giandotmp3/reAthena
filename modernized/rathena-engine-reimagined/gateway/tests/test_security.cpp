// Security invariants (architecture and spec section 4.1). Active.
#include <atomic>
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>

#include "gateway/hasher.hpp"

#include "fixture.hpp"

using namespace gt;

static std::string slurp(const std::string& p) {
  std::ifstream f(p);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

TEST("SEC-S1", config_defaults_are_env_placeholders_not_literals) {
  GatewayConfig c;
  CHECK(c.database_url == "${DATABASE_URL}");
  CHECK(c.login_bind == "${LOGIN_BIND}");
  std::string toml = slurp(std::string(GATEWAY_SOURCE_DIR) + "/config/gateway.example.toml");
  CHECK(!toml.empty());
  CHECK(toml.find("${DATABASE_URL}") != std::string::npos);
  CHECK(toml.find("passwd") == std::string::npos && toml.find("password") == std::string::npos);
  CHECK(toml.find("://") == std::string::npos);  // no literal connection strings
}

TEST("SEC-S1", expand_env_reads_environment_only_and_unset_is_empty) {
  setenv("GATEWAY_TEST_FAKE_VAR", "fake-value", 1);
  CHECK(expand_env("pre-${GATEWAY_TEST_FAKE_VAR}-post") == "pre-fake-value-post");
  unsetenv("GATEWAY_TEST_FAKE_VAR");
  CHECK(expand_env("${GATEWAY_TEST_FAKE_VAR}").empty());
  CHECK(expand_env("no placeholders") == "no placeholders");
}

TEST("SEC-S4", declared_length_must_cover_header_and_stay_in_bounds) {
  CHECK(!rules::declared_length_ok(0, 4));
  CHECK(!rules::declared_length_ok(3, 4));
  CHECK(rules::declared_length_ok(4, 4));
  CHECK(rules::declared_length_ok(32768, 4));
  CHECK(!rules::declared_length_ok(32769, 4));
}

TEST("SEC-S5", unauthenticated_session_never_reaches_char_handlers) {
  using rules::CharPacket;
  for (auto p : {CharPacket::Ping, CharPacket::PinCheck, CharPacket::PinSet, CharPacket::CharList,
                 CharPacket::SelectChar, CharPacket::MakeChar, CharPacket::DeleteChar, CharPacket::Other})
    CHECK(!rules::char_packet_allowed(rules::SessionPhase::Unauthenticated, p));
  CHECK(rules::char_packet_allowed(rules::SessionPhase::Unauthenticated, CharPacket::Connect));
}

TEST("SEC-S6", fixture_hasher_never_stores_plaintext_for_argon2id_accounts) {
  Env e;
  auto& a = e.add_account();
  CHECK(a.scheme == PasswordScheme::Argon2id);
  CHECK(a.stored_secret != kFakePass);
}

// [impl] was an expected-failure test (Argon2idPoolHasher not wired). The production hasher is the
// LayeredHasher: real Argon2id (libargon2 through dlopen) when the library is present, else the
// clearly-labelled PBKDF2-SHA256 fallback. Small parameters keep the test fast.
TEST("SEC-S6", real_argon2id_hasher_verifies_and_rejects_wrong_password) {
  HashParams p;
  p.pbkdf2_iterations = 2000;
  p.argon2_memory_kib = 64;
  p.argon2_time_cost = 2;
  LayeredHasher h(p);
  CHECK(h.uses_argon2id() == Argon2idHasher::available());
  CHECK(h.algorithm_name().find(h.uses_argon2id() ? "argon2id" : "NOT argon2id") != std::string::npos);
  const std::string stored = h.hash(kFakePass);
  CHECK(stored.rfind(h.uses_argon2id() ? "$argon2id$" : "$pbkdf2-sha256$", 0) == 0);
  CHECK(stored.find(kFakePass) == std::string::npos);                 // never the plaintext
  CHECK(h.hash(kFakePass) != stored);                                  // random salt
  CHECK(h.verify(PasswordScheme::Argon2id, stored, kFakePass).ok);
  CHECK(!h.verify(PasswordScheme::Argon2id, stored, "Wrong-Pass-9").ok);
  CHECK(!h.verify(PasswordScheme::Argon2id, stored, "fake-pass-1").ok);  // case-sensitive
  CHECK(!h.verify(PasswordScheme::Argon2id, stored.substr(0, stored.size() - 2) + "AA", kFakePass).ok);
  CHECK(!h.verify(PasswordScheme::Argon2id, "garbage", kFakePass).ok);
}

TEST("SEC-OVERLOAD", gateway_hash_queue_is_bounded_and_refuses_beyond_cap) {
  // 1 worker, queue cap 2: block the worker, fill the queue, the next job is refused at once.
  HashPool pool(1, 2);
  std::mutex m;
  std::condition_variable cv;
  bool release = false;
  std::atomic<int> started{0}, finished{0}, refused{0};
  auto blocker = [&] {
    ++started;
    std::unique_lock<std::mutex> lk(m);
    cv.wait(lk, [&] { return release; });
    ++finished;
  };
  std::vector<std::thread> callers;
  callers.emplace_back([&] { pool.run(blocker); });          // taken by the worker
  while (started.load() < 1) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  for (int i = 0; i < 2; ++i) callers.emplace_back([&] { pool.run(blocker); });  // fill the queue (cap 2)
  while (pool.stats().queued < 2) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  bool threw = false;
  try { pool.run(blocker); } catch (const HashOverloaded&) { threw = true; ++refused; }
  CHECK(threw);
  CHECK(pool.stats().rejected == 1 && pool.stats().max_queued == 2);
  { std::lock_guard<std::mutex> g(m); release = true; }
  cv.notify_all();
  for (auto& t : callers) t.join();
  CHECK(finished.load() == 3);                                // the refused job never ran
  // and through the login path: overload is a refusal with a reason, never an exception
  Env e;
  e.add_account();
  struct Overloaded : PasswordHasher {
    std::string hash(const std::string&) override { throw HashOverloaded(); }
    VerifyResult verify(PasswordScheme, const std::string&, const std::string&) override { throw HashOverloaded(); }
    std::string md5_hex(const std::string& d) override { return d; }
  } over;
  GatewayDeps d{e.cfg, e.clock, e.rng, e.accounts, e.chars, e.dir, e.bus, e.tickets, over, e.bans, e.fails, e.policy};
  LoginService svc(d);
  auto o = svc.login(e.req());
  CHECK(!o.ok && o.reason == Reason::Overloaded && o.refusal_code == refuse::ServerClosed);
}
