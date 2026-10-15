#pragma once
// Test helpers. Every credential-shaped value here is FAKE and same-shape only;
// none is a legacy shipped default. Production secrets come from
// ${INTER_SERVER_USER} / ${INTER_SERVER_PASS} and ${LOGIN_DB_URL}.

#include <cstdint>
#include <deque>
#include <initializer_list>
#include <string>

#include "rathena/login/account.hpp"
#include "rathena/login/config.hpp"
#include "rathena/login/rng.hpp"

namespace rathena::login::test {

inline constexpr const char* kFakeInterUser = "fake-inter-user";
inline constexpr const char* kFakeInterPass = "fake-inter-pass-0000";
inline constexpr UnixSeconds kNow = 1'760'000'000;  // fixed clock for tests

/// Returns the scripted values in order, then repeats the last one.
class ScriptedRng final : public Rng {
 public:
  ScriptedRng(std::initializer_list<std::uint32_t> v) : values_(v) {}
  std::uint32_t next_u32() override {
    if (values_.size() > 1) {
      auto x = values_.front();
      values_.pop_front();
      return x;
    }
    return values_.front();
  }

 private:
  std::deque<std::uint32_t> values_;
};

inline Account player(AccountId id = 2'000'001, std::string userid = "player01") {
  Account a;
  a.account_id = id;
  a.userid = std::move(userid);
  a.pass_scheme = PassScheme::Plain;  // legacy row awaiting lazy rehash
  a.pass_hash = "secret1";            // fake
  a.sex = Sex::Male;
  return a;
}

inline InterServerCredentials fake_inter_credentials() {
  return {kFakeInterUser, kFakeInterPass};
}

}  // namespace rathena::login::test
