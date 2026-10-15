#pragma once
// Pure account-administration rules (architecture §2 `rules/account`), driven
// by char-server requests (0x2722..0x2743). Each mutation returns the
// broadcast it requires, so propagation (BR-INTER-005) is testable without IO.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "rathena/login/account.hpp"
#include "rathena/login/config.hpp"
#include "rathena/login/types.hpp"

namespace rathena::login {

/// Login -> char notifications that must reach ALL connected char servers.
enum class InterBroadcast : std::uint16_t {
  AccountStatus = 0x2731,  // ban / state change (BR-LOGIN-026/028, BR-INTER-005)
  SexChanged = 0x2723,     // BR-LOGIN-029
  GlobalReg = 0x2726,      // ## registry relay (BR-LOGIN-036) (legacy family 2728/272e/2726)
  Kick = 0x2734,           // duplicate login (BR-LOGIN-018)
};

struct AdminOutcome {
  bool changed = false;
  std::optional<InterBroadcast> broadcast;
};

/// BR-LOGIN-026: extend from max(now, current unban); result <= now is ignored.
AdminOutcome apply_ban(Account& acc, UnixSeconds now, std::int64_t delta_seconds);
/// BR-LOGIN-027.
AdminOutcome apply_unban(Account& acc);
/// BR-LOGIN-028: applied if different; broadcast if the new state is non-zero.
AdminOutcome apply_state(Account& acc, std::uint32_t new_state);
/// BR-LOGIN-029: M <-> F; S (server) accounts are excluded.
AdminOutcome toggle_sex(Account& acc);

/// BR-LOGIN-030 helpers.
bool is_valid_email(std::string_view email);
AdminOutcome change_email(Account& acc, std::string_view current_claimed, std::string_view new_email);

/// BR-LOGIN-031: slots = account slots if > 0 else chars_per_account; VIP adds vip_char_increase.
int effective_char_slots(const LoginConfig& cfg, const Account& acc, bool vip_active);

/// BR-LOGIN-032 / BR-INTER-017: VIP lifecycle. Returns the data to send back
/// to the requesting char server (0x2743), which relays it to map.
struct VipInfo {
  bool active = false;
  UnixSeconds vip_time = 0;
  GroupId group_id = 0;
  int char_slots = 0;
  bool group_changed = false;
};
VipInfo apply_vip(const LoginConfig& cfg, Account& acc, UnixSeconds now,
                  std::int64_t add_seconds = 0);

/// BR-LOGIN-035: PIN update stores pin + change time; reaching max PIN tries
/// forces the account offline.
struct PinOutcome {
  bool stored = false;
  bool force_offline = false;
};
PinOutcome apply_pin_update(Account& acc, std::string_view pin, UnixSeconds now);
PinOutcome apply_pin_failure_limit_reached();

/// BR-LOGIN-036: `##` keys are login-owned global account registry.
bool is_login_registry_key(std::string_view key);

}  // namespace rathena::login
