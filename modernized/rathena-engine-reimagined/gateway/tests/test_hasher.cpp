// Password hashing: what is and is not Argon2id (see hasher.hpp), lazy rehash, pooled hashing.
#include <atomic>
#include <thread>

#include "fixture.hpp"
#include "gateway/crypto.hpp"
#include "gateway/hasher.hpp"

using namespace gt;
using namespace gateway;

static HashParams small() {
  HashParams p;
  p.pbkdf2_iterations = 1000;
  p.argon2_memory_kib = 64;
  p.argon2_time_cost = 2;
  p.argon2_parallelism = 1;
  return p;
}

// Known answers from an INDEPENDENT Argon2id implementation (Python `cryptography` 50.0.1), parameters
// t=2 m=64KiB p=1 and t=3 m=256KiB p=2. Passes only against the real libargon2; if the library cannot be
// loaded on a machine the test checks that the layered hasher says so and uses the fallback instead.
TEST("SEC-S6", argon2id_known_answers_against_independent_implementation) {
  if (!Argon2idHasher::available()) {
    LayeredHasher h(small());
    CHECK(!h.uses_argon2id());
    CHECK(h.algorithm_name().find("NOT argon2id") != std::string::npos);
    return;
  }
  CHECK(crypto::to_hex(Argon2idHasher::raw(2, 64, 1, "password", "somesaltsomesalt", 32)) ==
        "1a98fba8e6394425c9bdc333a9c750aaf811df070f5fb4a5788e1cf4db5793a1");
  CHECK(crypto::to_hex(Argon2idHasher::raw(3, 256, 2, "Fake-Pass-1", "somesaltsomesalt", 32)) ==
        "cd20408f19c44c95173e087031e2b6ff3c375a4fa236ecb0b9c50d9eb7856231");
  Argon2idHasher a(small());
  std::string phc = a.hash("Fake-Pass-1");
  CHECK(phc.rfind("$argon2id$v=19$m=64,t=2,p=1$", 0) == 0);
  CHECK(a.verify(PasswordScheme::Argon2id, phc, "Fake-Pass-1").ok);
  CHECK(!a.verify(PasswordScheme::Argon2id, phc, "Fake-Pass-2").ok);
}

TEST("SEC-S6", pbkdf2_fallback_phc_roundtrip_params_and_tamper) {
  Pbkdf2Sha256Hasher h(small());
  const std::string s = h.hash("Fake-Pass-1");
  CHECK(s.rfind("$pbkdf2-sha256$i=1000$", 0) == 0);
  CHECK(h.algorithm_name().find("NOT argon2id") != std::string::npos);   // the label is part of the contract
  CHECK(h.verify(PasswordScheme::Argon2id, s, "Fake-Pass-1").ok);
  CHECK(!h.verify(PasswordScheme::Argon2id, s, "Fake-Pass-1 ").ok);
  CHECK(!h.verify(PasswordScheme::Argon2id, s, "").ok);
  // stronger configuration later => the old row verifies and asks for a rehash
  HashParams stronger = small();
  stronger.pbkdf2_iterations = 5000;
  Pbkdf2Sha256Hasher h2(stronger);
  auto r = h2.verify(PasswordScheme::Argon2id, s, "Fake-Pass-1");
  CHECK(r.ok && r.needs_rehash);
  CHECK(!h.verify(PasswordScheme::Argon2id, s, "Fake-Pass-1").needs_rehash);
  // malformed rows never verify and never throw
  for (std::string bad : {std::string(""), std::string("$pbkdf2-sha256$"), std::string("$pbkdf2-sha256$i=0$AA$AA"),
                          std::string("$pbkdf2-sha256$i=x$AAAA$AAAA"), std::string("$pbkdf2-sha256$i=5$%%%%$AAAA")})
    CHECK(!h.verify(PasswordScheme::Argon2id, bad, "x").ok);
}

TEST("ARCH-HASH-LAZY", legacy_rows_verify_case_sensitively_and_ask_for_rehash) {
  LayeredHasher h(small());
  auto p = h.verify(PasswordScheme::Plaintext, "Secret1", "Secret1");
  CHECK(p.ok && p.needs_rehash);
  CHECK(!h.verify(PasswordScheme::Plaintext, "Secret1", "secret1").ok);
  const std::string md5 = crypto::md5_hex("Secret1");
  auto m = h.verify(PasswordScheme::Md5, md5, "Secret1");
  CHECK(m.ok && m.needs_rehash);
  CHECK(!h.verify(PasswordScheme::Md5, md5, "secret1").ok);
  CHECK(!h.verify(PasswordScheme::Md5, "", "").ok);   // an empty stored value is not md5("")
}

TEST("ARCH-HASH-LAZY", pbkdf2_row_is_upgraded_once_argon2id_is_available) {
  if (!Argon2idHasher::available()) return;                      // nothing to upgrade to on this machine
  Pbkdf2Sha256Hasher old(small());
  const std::string row = old.hash("Fake-Pass-1");
  LayeredHasher h(small());
  auto r = h.verify(PasswordScheme::Argon2id, row, "Fake-Pass-1");
  CHECK(r.ok && r.needs_rehash);
  const std::string fresh = h.hash("Fake-Pass-1");
  auto r2 = h.verify(PasswordScheme::Argon2id, fresh, "Fake-Pass-1");
  CHECK(r2.ok && !r2.needs_rehash);
}

// End to end: a legacy plaintext row and a legacy MD5 row become hashed rows on the first successful
// login, a failed login leaves them untouched, and the hash is a PHC string produced by the real hasher.
TEST("ARCH-HASH-LAZY", first_successful_login_rehashes_plaintext_and_md5_rows_with_real_hasher) {
  Env e;
  LayeredHasher real(small());
  GatewayDeps d{e.cfg, e.clock, e.rng, e.accounts, e.chars, e.dir, e.bus, e.tickets, real, e.bans, e.fails, e.policy};
  LoginService svc(d);

  Account a; a.id = 2000100; a.userid = "plainuser"; a.scheme = PasswordScheme::Plaintext; a.stored_secret = "Secret1";
  Account b; b.id = 2000101; b.userid = "md5user"; b.scheme = PasswordScheme::Md5; b.stored_secret = crypto::md5_hex("Secret1");
  e.accounts.create(a);
  e.accounts.create(b);
  for (const char* who : {"plainuser", "md5user"}) {
    LoginRequest req = e.req(who, "secret1");                    // wrong case
    CHECK(!svc.login(req).ok);
    const Account unchanged = e.accounts.find_by_userid(who, true)[0];
    CHECK(unchanged.scheme != PasswordScheme::Argon2id);         // untouched by the failure
    CHECK(unchanged.logincount == 0);
    CHECK(svc.login(e.req(who, "Secret1")).ok);
    const Account upgraded = e.accounts.find_by_userid(who, true)[0];
    CHECK(upgraded.scheme == PasswordScheme::Argon2id);
    CHECK(upgraded.stored_secret.rfind("$", 0) == 0 && upgraded.stored_secret.find("Secret1") == std::string::npos);
    CHECK(upgraded.stored_secret.rfind(real.uses_argon2id() ? "$argon2id$" : "$pbkdf2-sha256$", 0) == 0);
    e.dir.erase(upgraded.id);
    CHECK(svc.login(e.req(who, "Secret1")).ok);                  // and it keeps working
    CHECK(e.accounts.find(upgraded.id)->stored_secret == upgraded.stored_secret);  // no second rehash
    e.dir.erase(upgraded.id);
  }
}

TEST("SEC-OVERLOAD", pooled_hasher_runs_on_pool_threads_and_matches_inner_results) {
  LayeredHasher inner(small());
  HashPool pool(2, 64);
  PooledHasher h(inner, pool);
  const std::string row = h.hash("Fake-Pass-1");
  std::atomic<int> okc{0};
  std::vector<std::thread> ts;
  for (int t = 0; t < 8; ++t)
    ts.emplace_back([&] {
      for (int i = 0; i < 5; ++i) {
        if (h.verify(PasswordScheme::Argon2id, row, "Fake-Pass-1").ok) ++okc;
        if (h.verify(PasswordScheme::Argon2id, row, "nope").ok) --okc;
      }
    });
  for (auto& t : ts) t.join();
  CHECK(okc.load() == 40);
  CHECK(pool.stats().completed >= 81 && pool.stats().rejected == 0);
  CHECK(h.verify(PasswordScheme::Plaintext, "a", "a").ok);        // legacy compare does not touch the pool
}
