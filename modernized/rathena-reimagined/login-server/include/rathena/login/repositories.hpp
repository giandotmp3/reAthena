#pragma once
// Persistence ports used by the login server. Interfaces exist because test
// fakes exist (architecture §9 m6). The concrete MySQL classes live in the
// persistence unit and connect via ${LOGIN_DB_URL} (default ${DATABASE_URL}).

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "rathena/login/account.hpp"

namespace rathena::login {

class AccountRepository {
 public:
  virtual ~AccountRepository() = default;
  virtual std::vector<Account> find_by_userid(std::string_view userid, bool case_sensitive) = 0;
  virtual std::optional<Account> find_by_id(AccountId id) = 0;
  virtual std::optional<AccountId> max_account_id() = 0;
  virtual bool save(const Account& acc) = 0;
  /// BR-LOGIN-037: deletes the row and its `##` regs in one transaction.
  virtual bool remove_atomically(AccountId id) = 0;
  virtual bool web_token_taken(const std::string& token) = 0;
};

class IpBanRepository {
 public:
  virtual ~IpBanRepository() = default;
  virtual bool lookup(Ipv4 ip, std::vector<IpBan>& out) = 0;  // false = SQL error
  virtual bool insert(const IpBan& ban) = 0;
  virtual std::size_t purge_expired(UnixSeconds now) = 0;
};

class LoginLogRepository {
 public:
  virtual ~LoginLogRepository() = default;
  virtual void append(const LoginLogEntry& e) = 0;
};

}  // namespace rathena::login
