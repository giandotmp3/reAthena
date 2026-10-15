#include "rathena/login/web_token.hpp"

namespace rathena::login {

std::optional<std::string> generate_web_token(Rng& rng,
                                              const std::function<bool(const std::string&)>& is_taken) {
  static constexpr char kAlphabet[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
  for (int attempt = 0; attempt < kWebTokenMaxAttempts; ++attempt) {
    std::string token(kWebAuthTokenLength, '0');
    for (auto& c : token) c = kAlphabet[rng.next_u32() % 62u];
    if (!is_taken(token)) return token;
  }
  return std::nullopt;
}

void enable_web_token(Account& acc, std::string token) {
  acc.web_auth_token = std::move(token);
  acc.web_auth_token_enabled = true;
}

WebTokenSchedule schedule_web_token_disable(UnixSeconds logout_at) {
  return {logout_at + kWebTokenDisableDelay};
}

void disable_web_token(Account& acc) {
  acc.web_auth_token_enabled = false;
}

void wipe_web_token_on_startup(Account& acc) {
  acc.web_auth_token.clear();
  acc.web_auth_token_enabled = false;
}

}  // namespace rathena::login
