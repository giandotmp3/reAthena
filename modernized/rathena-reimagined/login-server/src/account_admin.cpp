#include "rathena/login/account_admin.hpp"

#include <algorithm>
#include <cctype>

namespace rathena::login {
namespace {
bool iequals(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i)
    if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
      return false;
  return true;
}
}  // namespace

AdminOutcome apply_ban(Account& acc, UnixSeconds now, std::int64_t delta_seconds) {
  const UnixSeconds base = std::max(now, acc.unban_time);
  const UnixSeconds result = base + delta_seconds;
  if (result <= now) return {};
  acc.unban_time = result;
  return {true, InterBroadcast::AccountStatus};
}

AdminOutcome apply_unban(Account& acc) {
  if (acc.unban_time == 0) return {};
  acc.unban_time = 0;
  return {true, std::nullopt};
}

AdminOutcome apply_state(Account& acc, std::uint32_t new_state) {
  if (acc.state == new_state) return {};
  acc.state = new_state;
  if (new_state != 0) return {true, InterBroadcast::AccountStatus};
  return {true, std::nullopt};
}

AdminOutcome toggle_sex(Account& acc) {
  if (acc.sex == Sex::Server) return {};
  acc.sex = acc.sex == Sex::Male ? Sex::Female : Sex::Male;
  return {true, InterBroadcast::SexChanged};
}

bool is_valid_email(std::string_view e) {
  if (e.size() < 3 || e.size() > 39) return false;
  if (std::count(e.begin(), e.end(), '@') != 1) return false;
  const auto at = e.find('@');
  if (at == 0 || e.back() == '@' || e.back() == '.' || e.front() == '.') return false;
  if (e.find("@.") != std::string_view::npos || e.find(".@") != std::string_view::npos ||
      e.find("..") != std::string_view::npos)
    return false;
  if (e.substr(at + 1).find('.') == std::string_view::npos) return false;
  for (char c : e) {
    const auto u = static_cast<unsigned char>(c);
    if (u < 0x21 || u > 0x7e) return false;
    if (std::string_view(" \",:;<>[\\]`|").find(c) != std::string_view::npos) return false;
  }
  return true;
}

AdminOutcome change_email(Account& acc, std::string_view current_claimed, std::string_view new_email) {
  if (!is_valid_email(current_claimed) || !is_valid_email(new_email)) return {};
  if (iequals(new_email, kDefaultEmail)) return {};
  if (!iequals(acc.email, current_claimed)) return {};
  acc.email = std::string(new_email);
  return {true, std::nullopt};
}

int effective_char_slots(const LoginConfig& cfg, const Account& acc, bool vip_active) {
  int base = acc.character_slots > 0 ? acc.character_slots
             : cfg.chars_per_account > 0 ? cfg.chars_per_account
                                         : cfg.max_chars - cfg.vip_char_increase;
  if (vip_active) base += cfg.vip_char_increase;
  return std::clamp(base, 0, cfg.max_chars);
}

VipInfo apply_vip(const LoginConfig& cfg, Account& acc, UnixSeconds now, std::int64_t add_seconds) {
  VipInfo info;
  if (add_seconds != 0) acc.vip_time = std::max(now, acc.vip_time) + add_seconds;
  const bool active = acc.vip_time > now;
  if (active) {
    if (acc.group_id < cfg.vip_group) {
      acc.old_group = acc.group_id;
      acc.group_id = cfg.vip_group;
      info.group_changed = true;
    }
  } else {
    if (acc.group_id == cfg.vip_group && acc.vip_time != 0) {
      acc.group_id = acc.old_group;
      acc.old_group = 0;
      info.group_changed = true;
    }
    acc.vip_time = 0;
  }
  info.active = active;
  info.vip_time = acc.vip_time;
  info.group_id = acc.group_id;
  info.char_slots = effective_char_slots(cfg, acc, active);
  return info;
}

PinOutcome apply_pin_update(Account& acc, std::string_view pin, UnixSeconds now) {
  acc.pincode = std::string(pin);
  acc.pincode_change = now;
  return {true, false};
}

PinOutcome apply_pin_failure_limit_reached() {
  return {false, true};
}

bool is_login_registry_key(std::string_view key) {
  return key.size() > 2 && key.substr(0, 2) == "##";
}

}  // namespace rathena::login
