#pragma once
// Test fixture. All credentials/secrets here are FAKE same-shape values.
#include <deque>
#include <set>

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

// DNSBL resolver double: only the addresses added to `hits` are "listed".
struct FakeDnsbl : DnsblResolver {
  std::set<std::string> hits;
  int calls = 0;
  bool listed(const std::string& ip) override { ++calls; return hits.count(ip) != 0; }
};

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
  FakeDnsbl dnsbl;
  MemoryAuditSink audit;
  LoginService login{deps()};
  AccountAdmin admin{deps()};
  CharService chr{deps()};

  GatewayDeps deps() {
    GatewayDeps d{cfg, clock, rng, accounts, chars, dir, bus, tickets, hasher, bans, fails, policy};
    d.dnsbl = &dnsbl;
    d.audit = &audit;
    return d;
  }

  Account& add_account(const std::string& userid = "fakeuser", AccountId id = 2000001) {
    Account a;
    a.id = id;
    a.userid = userid;
    a.scheme = PasswordScheme::Argon2id;
    a.stored_secret = hasher.hash(kFakePass);
    a.challenge_secret = kFakePass;  // account kept a challenge secret for the MD5 wire shim (spec section 3)
    a.birthdate = "900101";
    a.email = "user@example.invalid";
    accounts.create(a);
    last = a;
    made.push_back(a);   // stable address: `auto& x = add_account(); auto& y = add_account(...)` must not alias
    return made.back();
  }
  Account last;
  std::deque<Account> made;

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
