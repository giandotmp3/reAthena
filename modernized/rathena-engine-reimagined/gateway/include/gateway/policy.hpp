#pragma once
// Policy hooks supplied by rathena.base. The gateway is mechanism-only.
#include <string>
#include <utility>
#include <vector>

namespace gateway {

enum class NameVerdict { Ok = 0, Rejected = -1, BadLetters = -2 };

// Order of login checks (BR-LOGIN-001). Configurable, default below.
enum class LoginCheck {
  Dnsbl, AutoCreate, UnknownAccount, ServerAccount, Password, Expired, Banned, State, ClientHash
};
std::vector<LoginCheck> default_login_check_order();

struct PolicyHooks {
  virtual ~PolicyHooks() = default;
  virtual NameVerdict check_name(const std::string& name) const = 0;  // BR-CHAR-001
  virtual std::vector<LoginCheck> login_check_order() const { return default_login_check_order(); }
};

// Default policy: trimmed name, >=4 chars, no control chars, no leading '#',
// not "Server" (all -1); characters outside `allowed_letters` (if non-empty) -> -2.
class DefaultNamePolicy : public PolicyHooks {
 public:
  explicit DefaultNamePolicy(std::string allowed_letters = "") : allowed_(std::move(allowed_letters)) {}
  NameVerdict check_name(const std::string& name) const override;
 private:
  std::string allowed_;
};

}  // namespace gateway
