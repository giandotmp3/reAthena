#pragma once
// Login-owned entities (spec §2.1: login owns `login`, `global_acc_reg_*`, `ipbanlist`;
// architecture §3: Account, GlobalAccReg, IpBan, LoginLog, WebAuthToken).

#include <cstdint>
#include <optional>
#include <string>

#include "rathena/login/types.hpp"

namespace rathena::login {

/// Password-at-rest scheme (migration M6). Plain/MD5 rows are rehashed to
/// Argon2id after the next successful login (HITL #1 drop decision 3).
enum class PassScheme { Plain, Md5, Argon2id };

struct Account {
  AccountId account_id = 0;
  std::string userid;
  std::string pass_hash;  // encoded per pass_scheme; never logged
  PassScheme pass_scheme = PassScheme::Argon2id;
  /// MD5 challenge shim secret (BR-LOGIN-003, M6b). Only present while the shim
  /// is enabled for this account; a documented residual risk.
  std::optional<std::string> challenge_secret;
  Sex sex = Sex::Male;
  std::string email = kDefaultEmail;
  GroupId group_id = 0;
  std::uint32_t state = 0;
  UnixSeconds unban_time = 0;
  UnixSeconds expiration_time = 0;  // 0 = never expires
  std::uint32_t logincount = 0;
  std::string lastlogin;
  std::string last_ip;
  std::string birthdate;
  std::uint8_t character_slots = 0;  // 0 = use chars_per_account
  std::string pincode;
  UnixSeconds pincode_change = 0;
  UnixSeconds vip_time = 0;
  GroupId old_group = 0;
  std::string web_auth_token;
  bool web_auth_token_enabled = false;
};

/// One-time auth ticket handed from login to char (CAP-04). BR-LOGIN-019.
struct LoginTicket {
  AccountId account_id = 0;
  std::uint32_t login_id1 = 0;
  std::uint32_t login_id2 = 0;
  Sex sex = Sex::Male;
  Ipv4 ip = 0;
  UnixSeconds issued_at = 0;
};

/// An `ipbanlist` row. Mask has 1..4 significant octets: a.*.*.*, a.b.*.*, a.b.c.*, a.b.c.d.
struct IpBan {
  Ipv4 base = 0;
  std::uint8_t octets = 4;  // number of significant leading octets (1..4)
  UnixSeconds btime = 0;
  UnixSeconds rtime = 0;  // release time
  std::string reason;
};

/// A `loginlog` row. BR-LOGIN-039. Never carries the presented password.
struct LoginLogEntry {
  UnixSeconds time = 0;
  Ipv4 ip = 0;
  std::string userid;
  std::int32_t rcode = 0;
  std::string log;
};

}  // namespace rathena::login
