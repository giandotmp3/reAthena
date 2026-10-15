#pragma once
// Web auth token (BR-LOGIN-034; consumed by web-server BR-WEB-001/003).

#include <functional>
#include <optional>
#include <string>

#include "rathena/login/account.hpp"
#include "rathena/login/rng.hpp"
#include "rathena/login/types.hpp"

namespace rathena::login {

inline constexpr int kWebTokenMaxAttempts = 20;  // [DEFECT fix] legacy loop never incremented
inline constexpr UnixSeconds kWebTokenDisableDelay = 10;

/// 16 alphanumeric chars, unique per `is_taken`. nullopt after 20 collisions.
std::optional<std::string> generate_web_token(Rng& rng,
                                              const std::function<bool(const std::string&)>& is_taken);

/// Token lifecycle: enabled while online; disabled 10 s after logout; all wiped at startup.
struct WebTokenSchedule {
  UnixSeconds disable_at = 0;
};
void enable_web_token(Account& acc, std::string token);
WebTokenSchedule schedule_web_token_disable(UnixSeconds logout_at);
void disable_web_token(Account& acc);
void wipe_web_token_on_startup(Account& acc);

}  // namespace rathena::login
