// Password at rest (S6, M6) and the bounded hash pool (architecture §3/§7).
#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <future>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "rathena/login/argon2id.hpp"
#include "rathena/login/config.hpp"
#include "rathena/login/md5.hpp"
#include "rathena/login/password.hpp"
#include "test_support.hpp"

using namespace rathena::login;
using namespace rathena::login::test;

namespace {
/// Test-only fake KDF: NOT a real hash, never used outside tests.
class FakeHasher final : public PasswordHasher {
 public:
  std::string hash(std::string_view p) override { return "$fake$" + std::string(p); }
  VerifyStatus verify(std::string_view enc, std::string_view p) override {
    return enc == hash(p) ? VerifyStatus::Match : VerifyStatus::Mismatch;
  }
};
}  // namespace

TEST(LoginPassword, BR_LOGIN_002_PlainRowVerifiesCaseSensitivelyAndNeedsRehash) {
  FakeHasher h;
  Account a = player();
  auto r = verify_account_password(h, a, "secret1");
  EXPECT_TRUE(r.ok);
  EXPECT_TRUE(r.needs_rehash);
  r = verify_account_password(h, a, "Secret1");
  EXPECT_FALSE(r.ok);
  EXPECT_FALSE(r.needs_rehash);
}

TEST(LoginPassword, S6_Md5RowVerifiesAndNeedsRehash) {
  FakeHasher h;
  Account a = player();
  a.pass_scheme = PassScheme::Md5;
  a.pass_hash = to_hex(md5("fakepass9"));
  EXPECT_TRUE(verify_account_password(h, a, "fakepass9").needs_rehash);
  EXPECT_FALSE(verify_account_password(h, a, "fakepass8").ok);
}

TEST(LoginPassword, S6_Argon2idRowUsesHasherAndNoRehash) {
  FakeHasher h;
  Account a = player();
  a.pass_scheme = PassScheme::Argon2id;
  a.pass_hash = h.hash("fakepass9");
  const auto r = verify_account_password(h, a, "fakepass9");
  EXPECT_TRUE(r.ok);
  EXPECT_FALSE(r.needs_rehash);
}

// ---- Argon2id (P-H-C reference implementation) ------------------------------------------

namespace {
/// Small cost for fast tests; the default-cost path has its own test.
constexpr Argon2Params kFast{64, 1, 1};

std::vector<std::uint8_t> bytes(std::string_view s) { return {s.begin(), s.end()}; }

std::string hex(std::span<const std::uint8_t> b) {
  static constexpr char d[] = "0123456789abcdef";
  std::string out;
  for (auto x : b) {
    out.push_back(d[x >> 4]);
    out.push_back(d[x & 0xF]);
  }
  return out;
}
}  // namespace

TEST(LoginPassword, S6_Argon2idRfc9106KnownAnswer) {
  // RFC 9106 §5.3 (also kats/argon2id in the reference repo): m=32 KiB, t=3, p=4,
  // P = 32 x 0x01, S = 16 x 0x02, K = 8 x 0x03, X = 12 x 0x04, 32-byte tag.
  const std::vector<std::uint8_t> pwd(32, 0x01), salt(16, 0x02), secret(8, 0x03), ad(12, 0x04);
  std::array<std::uint8_t, 32> tag{};
  argon2id_raw({32, 3, 4}, pwd, salt, secret, ad, tag);
  EXPECT_EQ(hex(tag), "0d640df58d78766c08c037a34a8b53c9d01ef0452d75b65eb52520e96b01e659");
}

TEST(LoginPassword, S6_Argon2idReferencePhcKnownAnswers) {
  // Argon2id vectors from the reference repo's src/test.c (tag 20190702), m = 2^8 KiB.
  struct Kat {
    Argon2Params params;
    const char* raw_hex;
    const char* phc;
  };
  const Kat kats[] = {
      {{256, 2, 1},
       "9dfeb910e80bad0311fee20f9c0e2b12c17987b4cac90c2ef54d5b3021c68bfe",
       "$argon2id$v=19$m=256,t=2,p=1$c29tZXNhbHQ$nf65EOgLrQMR/uIPnA4rEsF5h7TKyQwu9U1bMCHGi/4"},
      {{256, 2, 2},
       "6d093c501fd5999645e0ea3bf620d7b8be7fd2db59c20d9fff9539da2bf57037",
       "$argon2id$v=19$m=256,t=2,p=2$c29tZXNhbHQ$bQk8UB/VmZZF4Oo79iDXuL5/0ttZwg2f/5U52iv1cDc"},
  };
  Argon2idHasher h(kFast);
  const auto salt = bytes("somesalt");
  const auto pwd = bytes("password");
  for (const auto& k : kats) {
    EXPECT_EQ(argon2id_encode(k.params, "password", salt), k.phc);
    std::array<std::uint8_t, 32> raw{};
    argon2id_raw(k.params, pwd, salt, {}, {}, raw);
    EXPECT_EQ(hex(raw), k.raw_hex);
    EXPECT_EQ(h.verify(k.phc, "password"), VerifyStatus::Match);
    EXPECT_EQ(h.verify(k.phc, "passwore"), VerifyStatus::Mismatch);
  }
}

TEST(LoginPassword, S6_Argon2idRoundTripAndWrongPassword) {
  Argon2idHasher h(kFast);
  const auto enc = h.hash("fake-pass-0001");
  EXPECT_EQ(h.verify(enc, "fake-pass-0001"), VerifyStatus::Match);
  EXPECT_EQ(h.verify(enc, "fake-pass-0002"), VerifyStatus::Mismatch);
  EXPECT_EQ(h.verify(enc, "Fake-pass-0001"), VerifyStatus::Mismatch);
  EXPECT_EQ(h.verify(enc, ""), VerifyStatus::Mismatch);
  // A fresh CSPRNG salt every time: same password, different strings, both verify.
  const auto enc2 = h.hash("fake-pass-0001");
  EXPECT_NE(enc, enc2);
  EXPECT_EQ(h.verify(enc2, "fake-pass-0001"), VerifyStatus::Match);
  // Empty passwords hash and verify too (registration enforces the minimum length).
  EXPECT_EQ(h.verify(h.hash(""), ""), VerifyStatus::Match);
}

TEST(LoginPassword, S6_Argon2idPhcFormatAndParse) {
  Argon2idHasher h({128, 3, 2});
  const auto enc = h.hash("fake-pass-0001");
  constexpr std::string_view kHead = "$argon2id$v=19$m=128,t=3,p=2$";
  EXPECT_EQ(enc.rfind(kHead, 0), 0u) << enc;
  // 16-byte salt -> 22 base64 chars, 32-byte tag -> 43 chars: libsodium's layout.
  EXPECT_EQ(enc.size(), kHead.size() + 22 + 1 + 43);
  const auto phc = parse_argon2id_phc(enc);
  ASSERT_TRUE(phc.has_value());
  EXPECT_EQ(phc->params.memory_kib, 128u);
  EXPECT_EQ(phc->params.iterations, 3u);
  EXPECT_EQ(phc->params.parallelism, 2u);
  EXPECT_EQ(phc->salt.size(), kArgon2SaltBytes);
  EXPECT_EQ(phc->hash.size(), kArgon2HashBytes);
  EXPECT_EQ(format_argon2id_phc(*phc), enc);
}

TEST(LoginPassword, S6_Argon2idMalformedHashRejected) {
  Argon2idHasher h(kFast);
  const std::string good =
      "$argon2id$v=19$m=256,t=2,p=1$c29tZXNhbHQ$nf65EOgLrQMR/uIPnA4rEsF5h7TKyQwu9U1bMCHGi/4";
  ASSERT_EQ(h.verify(good, "password"), VerifyStatus::Match);
  const std::string tail = "$c29tZXNhbHQ$nf65EOgLrQMR/uIPnA4rEsF5h7TKyQwu9U1bMCHGi/4";
  const std::string bad[] = {
      "",
      "password",                          // plaintext-shaped row
      "5f4dcc3b5aa765d61d8327deb882cf99",  // MD5-shaped row
      "$argon2i$v=19$m=256,t=2,p=1" + tail,
      "$argon2d$v=19$m=256,t=2,p=1" + tail,
      "$argon2id$m=256,t=2,p=1" + tail,               // no version
      "$argon2id$v=16$m=256,t=2,p=1" + tail,          // Argon2 v1.0
      "$argon2id$v=19$m=0256,t=2,p=1" + tail,         // leading zero
      "$argon2id$v=19$m=256,t=0,p=1" + tail,
      "$argon2id$v=19$m=256,t=2,p=0" + tail,
      "$argon2id$v=19$m=4,t=2,p=1" + tail,            // m < 8 KiB per lane
      "$argon2id$v=19$m=4294967295,t=2,p=1" + tail,   // memory DoS
      "$argon2id$v=19$m=99999999999,t=2,p=1" + tail,  // overflows uint32
      "$argon2id$v=19$m=256,t=100000,p=1" + tail,     // CPU DoS
      "$argon2id$v=19$m=256,t=2,p=1,x=1" + tail,
      "$argon2id$v=19$t=2,m=256,p=1" + tail,          // field order
      "$argon2id$v=19$m=256,t=2,p=1$c29tZXNhbHQ=$nf65EOgLrQMR/uIPnA4rEsF5h7TKyQwu9U1bMCHGi/4",  // padding
      "$argon2id$v=19$m=256,t=2,p=1$c29tZXNh$nf65EOgLrQMR/uIPnA4rEsF5h7TKyQwu9U1bMCHGi/4",      // 6-byte salt
      "$argon2id$v=19$m=256,t=2,p=1$c29tZXNhbHQ$nf65EOgLrQMR",                                  // 9-byte tag
      "$argon2id$v=19$m=256,t=2,p=1$c29tZXNhbHQ$nf65EOgLrQMR/uIPnA4rEsF5h7TKyQwu9U1bMCHGi/5",   // non-canonical
      "$argon2id$v=19$m=256,t=2,p=1$c29tZXNhbHQ$nf65EOgLrQMR/uIPnA4rEsF5h7TKyQwu9U1bMCHGi/4$",  // trailing
      "$argon2id$v=19$m=256,t=2,p=1$c29tZXNhbHQ$nf65EOgLrQMR-uIPnA4rEsF5h7TKyQwu9U1bMCHGi/4",   // bad char
      "$argon2id$v=19$m=256,t=2,p=1$c29tZXNhbHQ",
      good + std::string(1, '\0'),  // never reaches the C decoder as a shorter valid string
  };
  for (const auto& b : bad) {
    EXPECT_FALSE(parse_argon2id_phc(b).has_value()) << b;
    EXPECT_EQ(h.verify(b, "password"), VerifyStatus::Unsupported) << b;
    EXPECT_FALSE(h.needs_rehash(b)) << b;
  }
  Account a = player();
  a.pass_scheme = PassScheme::Argon2id;
  a.pass_hash = bad[3];
  EXPECT_FALSE(verify_account_password(h, a, "password").ok);
}

TEST(LoginPassword, S6_Argon2idNeedsRehashWhenStoredParamsAreWeaker) {
  Argon2idHasher configured({128, 2, 1});
  EXPECT_FALSE(configured.needs_rehash(Argon2idHasher({128, 2, 1}).hash("fake-pass-0001")));
  EXPECT_TRUE(configured.needs_rehash(Argon2idHasher({64, 2, 1}).hash("fake-pass-0001")));    // less memory
  EXPECT_TRUE(configured.needs_rehash(Argon2idHasher({128, 1, 1}).hash("fake-pass-0001")));   // fewer passes
  EXPECT_FALSE(configured.needs_rehash(Argon2idHasher({256, 3, 1}).hash("fake-pass-0001")));  // stronger
  EXPECT_FALSE(configured.needs_rehash(Argon2idHasher({128, 2, 2}).hash("fake-pass-0001")));  // lanes only
  // An 8-byte salt (valid, e.g. imported) is weaker than the 16 bytes we write.
  EXPECT_TRUE(configured.needs_rehash(argon2id_encode({128, 2, 1}, "fake-pass-0001", bytes("somesalt"))));

  // Through the account check: only a successful login asks for the upgrade.
  Account a = player();
  a.pass_scheme = PassScheme::Argon2id;
  a.pass_hash = Argon2idHasher({64, 1, 1}).hash("fake-pass-0001");
  auto r = verify_account_password(configured, a, "fake-pass-0001");
  EXPECT_TRUE(r.ok);
  EXPECT_TRUE(r.needs_rehash);
  r = verify_account_password(configured, a, "fake-pass-0002");
  EXPECT_FALSE(r.ok);
  EXPECT_FALSE(r.needs_rehash);
}

TEST(LoginPassword, S6_LegacyRowsUpgradeToArgon2idOnSuccessfulLogin) {
  // M6: plaintext and MD5 rows are rewritten as Argon2id after the next successful login.
  Argon2idHasher h(kFast);
  Account plain = player();  // Plain row, password "secret1" (fake)
  Account md5row = player();
  md5row.pass_scheme = PassScheme::Md5;
  md5row.pass_hash = to_hex(md5("fakepass9"));
  const std::pair<Account, std::string> rows[] = {{plain, "secret1"}, {md5row, "fakepass9"}};
  for (auto [acc, pw] : rows) {
    const auto r = verify_account_password(h, acc, pw);
    ASSERT_TRUE(r.ok);
    ASSERT_TRUE(r.needs_rehash);
    upgrade_account_password(h, acc, pw);
    EXPECT_EQ(acc.pass_scheme, PassScheme::Argon2id);
    EXPECT_TRUE(parse_argon2id_phc(acc.pass_hash).has_value());
    const auto again = verify_account_password(h, acc, pw);
    EXPECT_TRUE(again.ok);
    EXPECT_FALSE(again.needs_rehash);
    EXPECT_FALSE(verify_account_password(h, acc, pw + "x").ok);
  }
}

TEST(LoginPassword, S6_Argon2idDefaultParamsAreOwasp) {
  const LoginConfig cfg;
  const auto p = argon2_params_from(cfg);
  EXPECT_EQ(p.memory_kib, 19456u);
  EXPECT_EQ(p.iterations, 2u);
  EXPECT_EQ(p.parallelism, 1u);
  Argon2idHasher h(p);  // the one test at production cost
  const auto enc = h.hash("fake-pass-0001");
  EXPECT_EQ(enc.rfind("$argon2id$v=19$m=19456,t=2,p=1$", 0), 0u) << enc;
  EXPECT_EQ(h.verify(enc, "fake-pass-0001"), VerifyStatus::Match);
  EXPECT_EQ(h.verify(enc, "fake-pass-0002"), VerifyStatus::Mismatch);
  EXPECT_FALSE(h.needs_rehash(enc));
}

TEST(LoginPassword, S6_Argon2idParamsConfigurableAndValidated) {
  LoginConfig cfg;
  EXPECT_EQ(apply_config_key(cfg, "argon2_memory_kib", "65536"), ConfigKeyStatus::Applied);
  EXPECT_EQ(apply_config_key(cfg, "argon2_iterations", "3"), ConfigKeyStatus::Applied);
  EXPECT_EQ(apply_config_key(cfg, "argon2_parallelism", "4"), ConfigKeyStatus::Applied);
  const auto p = argon2_params_from(cfg);
  EXPECT_EQ(p.memory_kib, 65536u);
  EXPECT_EQ(p.iterations, 3u);
  EXPECT_EQ(p.parallelism, 4u);
  EXPECT_EQ(apply_config_key(cfg, "argon2_iterations", "0"), ConfigKeyStatus::Invalid);
  EXPECT_EQ(apply_config_key(cfg, "argon2_parallelism", "0"), ConfigKeyStatus::Invalid);
  EXPECT_EQ(apply_config_key(cfg, "argon2_memory_kib", "4"), ConfigKeyStatus::Invalid);
  EXPECT_EQ(apply_config_key(cfg, "argon2_memory_kib", "99999999999"), ConfigKeyStatus::Invalid);
  EXPECT_EQ(cfg.argon2_iterations, 3u);  // unchanged by the rejected values
  EXPECT_THROW(Argon2idHasher({16, 1, 4}), Argon2Error);  // m < 8 KiB per lane
  EXPECT_THROW(Argon2idHasher({64, 0, 1}), Argon2Error);
}

TEST(LoginPassword, S6_OsCsprngFillsBuffers) {
  std::array<std::uint8_t, 32> a{}, b{};
  os_random_bytes(a);
  os_random_bytes(b);
  EXPECT_NE(a, b);
  EXPECT_NE(a, (std::array<std::uint8_t, 32>{}));
  os_random_bytes(std::span<std::uint8_t>{});  // an empty request is fine
}

TEST(LoginHashPool, S6_PooledCheckVerifiesAndRehashesOnTheWorker) {
  Argon2idHasher h(kFast);
  std::promise<PasswordOutcome> ok_p, bad_p;
  HashWorkerPool pool(2, 8);
  ASSERT_TRUE(submit_password_check(pool, h, player(), "secret1",
                                    [&](PasswordOutcome o) { ok_p.set_value(std::move(o)); }));
  ASSERT_TRUE(submit_password_check(pool, h, player(), "secret2",
                                    [&](PasswordOutcome o) { bad_p.set_value(std::move(o)); }));
  const auto ok = ok_p.get_future().get();
  EXPECT_TRUE(ok.ok);
  EXPECT_FALSE(ok.error);
  ASSERT_TRUE(ok.new_argon2id_hash.has_value());  // Plain row -> upgraded on the worker
  EXPECT_EQ(h.verify(*ok.new_argon2id_hash, "secret1"), VerifyStatus::Match);
  const auto bad = bad_p.get_future().get();
  EXPECT_FALSE(bad.ok);
  EXPECT_FALSE(bad.new_argon2id_hash.has_value());
}

TEST(LoginHashPool, S6_PooledCheckReportsHasherFailureAsError) {
  /// Test-only hasher whose rehash fails (stands in for a CSPRNG or allocation failure).
  struct ThrowingHasher final : PasswordHasher {
    std::string hash(std::string_view) override { throw Argon2Error("fake failure"); }
    VerifyStatus verify(std::string_view, std::string_view) override { return VerifyStatus::Match; }
    bool needs_rehash(std::string_view) const override { return true; }
  } h;
  Account a = player();
  a.pass_scheme = PassScheme::Argon2id;
  std::promise<PasswordOutcome> p;
  HashWorkerPool pool(1, 1);
  ASSERT_TRUE(submit_password_check(pool, h, a, "x", [&](PasswordOutcome o) { p.set_value(std::move(o)); }));
  const auto o = p.get_future().get();
  EXPECT_TRUE(o.error);
  EXPECT_FALSE(o.ok);
}

TEST(LoginHashPool, S6_PooledCheckIsBoundedByTheQueue) {
  Argon2idHasher h(kFast);
  std::promise<void> started, release;
  auto release_f = release.get_future().share();
  std::atomic<int> done{0};
  HashWorkerPool pool(1, 1);
  ASSERT_TRUE(pool.try_submit([&] {
    started.set_value();
    release_f.wait();
  }));
  started.get_future().wait();  // the worker is now busy
  EXPECT_TRUE(submit_password_check(pool, h, player(), "secret1", [&](PasswordOutcome) { ++done; }));
  // Queue full: refused without running (the caller answers with refuse code 3).
  EXPECT_FALSE(submit_password_check(pool, h, player(), "secret1", [&](PasswordOutcome) { ++done; }));
  EXPECT_EQ(pool.queued(), 1u);
  release.set_value();
}

TEST(LoginHashPool, Arch3_QueueFullRejectsForRefuseCode3) {
  // Everything the jobs touch is declared before the pool, so the pool (and its worker
  // threads) is destroyed first and no job can outlive the objects it references.
  std::promise<void> started, release;
  auto release_f = release.get_future().share();
  std::atomic<int> ran{0};
  HashWorkerPool pool(1, 2);
  ASSERT_TRUE(pool.try_submit([&] {
    started.set_value();
    release_f.wait();
  }));
  started.get_future().wait();  // the worker is now busy
  EXPECT_TRUE(pool.try_submit([&] { ++ran; }));
  EXPECT_TRUE(pool.try_submit([&] { ++ran; }));
  EXPECT_FALSE(pool.try_submit([&] { ++ran; }));  // back-pressure
  EXPECT_EQ(pool.queued(), 2u);
  EXPECT_EQ(kBackPressureRefuseCode, 3u);
  release.set_value();
}

TEST(LoginHashPool, Arch7_DefaultQueueDepthIs256) {
  EXPECT_EQ(LoginConfig{}.hash_pool_queue_depth, 256u);
}
