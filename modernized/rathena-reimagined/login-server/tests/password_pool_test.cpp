// Password at rest (S6, M6) and the bounded hash pool (architecture §3/§7).
#include <gtest/gtest.h>

#include <atomic>
#include <future>

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

TEST(LoginPassword, S6_Argon2idStubFailsClosed) {
  Argon2idHasher h;
  Account a = player();
  a.pass_scheme = PassScheme::Argon2id;
  a.pass_hash = "$argon2id$v=19$m=65536,t=2,p=1$ZmFrZXNhbHQ$ZmFrZWhhc2g";  // fake shape
  EXPECT_FALSE(verify_account_password(h, a, "anything").ok);
  EXPECT_THROW(h.hash("x"), std::logic_error);
}

TEST(LoginPassword, S6_Argon2idRealBinding) {
  GTEST_SKIP() << "PENDING M6/S6: libsodium crypto_pwhash_str{,_verify} binding not vendored yet";
}

TEST(LoginHashPool, Arch3_QueueFullRejectsForRefuseCode3) {
  HashWorkerPool pool(1, 2);
  std::promise<void> started, release;
  auto release_f = release.get_future().share();
  ASSERT_TRUE(pool.try_submit([&] {
    started.set_value();
    release_f.wait();
  }));
  started.get_future().wait();  // the worker is now busy
  std::atomic<int> ran{0};
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
