#pragma once
// Policy hooks supplied by rathena.base. The gateway is mechanism-only: slot counts, name rules,
// starting data, PIN complexity, cleanup plan and check order are decided behind this interface.
// DefaultNamePolicy at the bottom is the default policy FIXTURE (legacy-parity values), standing in
// for the rathena.base binding; it is the only place in the gateway that holds those numbers.
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "gateway/rules.hpp"

namespace gateway {

enum class NameVerdict { Ok = 0, Rejected = -1, BadLetters = -2 };

// Order of login checks (BR-LOGIN-001). Configurable, default below.
enum class LoginCheck {
  Dnsbl, AutoCreate, UnknownAccount, ServerAccount, Password, Expired, Banned, State, ClientHash
};
std::vector<LoginCheck> default_login_check_order();

// What a new character starts with. The gateway copies these into Character and forwards `numbers`
// untouched; it never interprets the keys.
struct StartPackage {
  int max_hp = 0;
  int max_sp = 0;
  std::map<std::string, std::int64_t> numbers;
};

struct PolicyHooks {
  virtual ~PolicyHooks() = default;
  virtual NameVerdict check_name(const std::string& name) const = 0;  // BR-CHAR-001
  virtual std::vector<LoginCheck> login_check_order() const { return default_login_check_order(); }

  // ---- character creation (BR-CHAR-004..007). Fail-safe defaults: refuse. ----------------------
  virtual bool starting_stats_valid(bool /*new_client*/, const rules::StartStats&) const { return false; }
  virtual bool start_job_allowed(int /*job_flag*/) const { return false; }
  virtual std::optional<StartPackage> starting_package(int /*job_flag*/, const rules::StartStats&) const {
    return std::nullopt;
  }
  // The make-character packet carries a client job number; policy maps it to the opaque job flag
  // used by start_job_allowed / starting_package (0 = unknown job => refused).
  virtual int job_flag_for_client_job(std::uint32_t /*client_job*/) const { return 0; }
  // ---- PIN (BR-CHAR-020) -------------------------------------------------------------------------
  virtual bool pin_is_legal(const std::string& pin) const { return rules::pin_is_legal(pin); }
  // ---- delete (BR-CHAR-013): ordered world-side cleanup steps, opaque ids ------------------------
  virtual std::vector<std::string> delete_cleanup_plan() const { return {}; }
};

// Default policy fixture. Name: trimmed, >= 4 chars, no control chars, no leading '#', not "Server"
// (all -1); characters outside `allowed_letters` (if non-empty) -> -2. Starting data: the legacy
// numbers, which production takes from rathena.base config instead.
class DefaultNamePolicy : public PolicyHooks {
 public:
  explicit DefaultNamePolicy(std::string allowed_letters = "", int allowed_job_flags = 3)
      : allowed_(std::move(allowed_letters)), job_flags_(allowed_job_flags) {}
  NameVerdict check_name(const std::string& name) const override;
  bool starting_stats_valid(bool new_client, const rules::StartStats& s) const override;
  bool start_job_allowed(int job_flag) const override;
  int job_flag_for_client_job(std::uint32_t client_job) const override;
  std::optional<StartPackage> starting_package(int job_flag, const rules::StartStats& s) const override;
  std::vector<std::string> delete_cleanup_plan() const override;
 private:
  std::string allowed_;
  int job_flags_;
};

}  // namespace gateway
