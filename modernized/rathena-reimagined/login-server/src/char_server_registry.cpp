#include "rathena/login/char_server_registry.hpp"

#include "rathena/login/auth_rules.hpp"

namespace rathena::login {

RegistrationResult CharServerRegistry::try_register(const InterServerCredentials& expected,
                                                    std::string_view user, std::string_view pass,
                                                    const Account& acc, const CharServerInfo& info) {
  const bool user_ok = plaintext_password_matches(expected.user, user);
  const bool pass_ok = plaintext_password_matches(expected.pass, pass);
  if (expected.user.empty() || expected.pass.empty() || !user_ok || !pass_ok)
    return RegistrationResult::BadCredentials;
  if (acc.sex != Sex::Server) return RegistrationResult::NotServerAccount;
  if (acc.account_id >= static_cast<AccountId>(kMaxServers)) return RegistrationResult::IdOutOfRange;
  auto& slot = slots_.at(acc.account_id);
  if (slot) return RegistrationResult::SlotTaken;
  slot = info;
  return RegistrationResult::Accepted;
}

void CharServerRegistry::unregister(int slot) {
  slots_.at(static_cast<std::size_t>(slot)).reset();
}

int CharServerRegistry::connected_count() const {
  int n = 0;
  for (const auto& s : slots_) n += s ? 1 : 0;
  return n;
}

std::vector<int> CharServerRegistry::connected_slots() const {
  std::vector<int> out;
  for (std::size_t i = 0; i < slots_.size(); ++i)
    if (slots_[i]) out.push_back(static_cast<int>(i));
  return out;
}

bool CharServerRegistry::update_ip(int slot, Ipv4 new_ip) {
  auto& s = slots_.at(static_cast<std::size_t>(slot));
  if (!s) return false;
  s->ip = new_ip;
  return true;
}

Ipv4 select_char_ip(const LoginConfig& cfg, Ipv4 client_ip, Ipv4 public_char_ip) {
  for (const auto& sn : cfg.lan_subnets)
    if ((sn.char_ip & sn.mask) == (client_ip & sn.mask)) return sn.char_ip;
  return public_char_ip;
}

int population_display(const LoginConfig& cfg, int users) {
  if (cfg.usercount_disable) return 4;
  if (users <= cfg.usercount_low) return 0;
  if (users <= cfg.usercount_medium) return 1;
  if (users <= cfg.usercount_high) return 2;
  return 3;
}

}  // namespace rathena::login
