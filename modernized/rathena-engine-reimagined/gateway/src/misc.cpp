#include <cctype>
#include <cstdlib>

#include "gateway/config.hpp"
#include "gateway/policy.hpp"
#include "gateway/role.hpp"

namespace gateway {

std::string expand_env(const std::string& v) {
  std::string out;
  for (std::size_t i = 0; i < v.size();) {
    if (v[i] == '$' && i + 1 < v.size() && v[i + 1] == '{') {
      auto end = v.find('}', i + 2);
      if (end != std::string::npos) {
        const char* val = std::getenv(v.substr(i + 2, end - i - 2).c_str());
        if (val) out += val;
        i = end + 1;
        continue;
      }
    }
    out += v[i++];
  }
  return out;
}

std::optional<Role> parse_role(const std::string& v) {
  if (v == "all") return Role::All;
  if (v == "gateway") return Role::Gateway;
  if (v == "world") return Role::World;
  return std::nullopt;
}
bool role_runs_gateway(Role r) { return r == Role::All || r == Role::Gateway; }
bool role_runs_world(Role r) { return r == Role::All || r == Role::World; }

std::vector<LoginCheck> default_login_check_order() {
  return {LoginCheck::Dnsbl,    LoginCheck::AutoCreate, LoginCheck::UnknownAccount,
          LoginCheck::ServerAccount, LoginCheck::Password, LoginCheck::Expired,
          LoginCheck::Banned,   LoginCheck::State,      LoginCheck::ClientHash};
}

NameVerdict DefaultNamePolicy::check_name(const std::string& raw) const {
  std::size_t b = 0, e = raw.size();
  while (b < e && std::isspace(static_cast<unsigned char>(raw[b]))) ++b;
  while (e > b && std::isspace(static_cast<unsigned char>(raw[e - 1]))) --e;
  const std::string n = raw.substr(b, e - b);
  if (n.size() < 4 || n[0] == '#' || n == "Server") return NameVerdict::Rejected;
  for (char c : n) if (std::iscntrl(static_cast<unsigned char>(c))) return NameVerdict::Rejected;
  if (!allowed_.empty()) {
    for (char c : n) if (allowed_.find(c) == std::string::npos) return NameVerdict::BadLetters;
  }
  return NameVerdict::Ok;
}

}  // namespace gateway
