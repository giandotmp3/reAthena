#pragma once
// Test fixture. All credentials/secrets here are FAKE same-shape values.
#include "gateway/role.hpp"
#include "gateway/services.hpp"
#include "harness.hpp"

namespace gt {
using namespace gateway;

struct FakeHasher : PasswordHasher {
  std::string hash(const std::string& pw) override { return "fake-argon2id$" + pw; }
  VerifyResult verify(PasswordScheme s, const std::string& stored, const std::string& pw) override {
    switch (s) {
      case PasswordScheme::Argon2id: return {stored == hash(pw), false};
      case PasswordScheme::Plaintext: return {stored == pw, stored == pw};
      case PasswordScheme::Md5: return {stored == md5_hex(pw), stored == md5_hex(pw)};
    }
    return {};
  }
  std::string md5_hex(const std::string& d) override { return "fake-md5:" + d; }
};

inline const char* kFakePass = "Fake-Pass-1";

struct Env {
  GatewayConfig cfg;
  ManualClock clock;
  SequenceRng rng;
  InProcAccountStore accounts;
  InProcCharacterStore chars;
  InProcSessionDirectory dir;
  InProcWorldBus bus;
  InProcTicketStore tickets{30};
  FakeHasher hasher;
  InProcIpBanList bans;
  InProcFailureCounter fails{bans, 7, 300, 300};
  DefaultNamePolicy policy{"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_ "};
  LoginService login{deps()};
  AccountAdmin admin{deps()};
  CharService chr{deps()};

  GatewayDeps deps() {
    return GatewayDeps{cfg, clock, rng, accounts, chars, dir, bus, tickets, hasher, bans, fails, policy};
  }

  Account& add_account(const std::string& userid = "fakeuser", AccountId id = 2000001) {
    Account a;
    a.id = id;
    a.userid = userid;
    a.scheme = PasswordScheme::Argon2id;
    a.stored_secret = hasher.hash(kFakePass);
    a.birthdate = "900101";
    a.email = "user@example.invalid";
    accounts.create(a);
    last = a;
    return last;
  }
  Account last;

  LoginRequest req(const std::string& userid = "fakeuser", const std::string& pw = kFakePass) {
    LoginRequest r;
    r.userid = userid;
    r.secret = pw;
    r.ip = "192.0.2.10";
    return r;
  }

  CharSession session(const Account& a, rules::SessionPhase ph = rules::SessionPhase::Ready) {
    CharSession s;
    s.account_id = a.id;
    s.login_id1 = 11;
    s.login_id2 = 22;
    s.phase = ph;
    s.ip = "192.0.2.10";
    return s;
  }

  CharId add_char(AccountId aid, const std::string& name, int slot) {
    Character c;
    c.account_id = aid;
    c.name = name;
    c.slot = slot;
    return chars.insert(c);
  }
};

}  // namespace gt
