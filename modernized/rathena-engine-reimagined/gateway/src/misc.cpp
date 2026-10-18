#include <cctype>
#include <cstdlib>
#include <sstream>
#include <strings.h>

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
const char* role_name(Role r) {
  switch (r) {
    case Role::All: return "all";
    case Role::Gateway: return "gateway";
    case Role::World: return "world";
  }
  return "?";
}

CliOptions parse_cli(const std::vector<std::string>& args) {
  CliOptions o;
  for (const std::string& a : args) {
    if (a.rfind("--role=", 0) == 0) {
      auto r = parse_role(a.substr(7));
      if (!r) { o.error = "invalid --role '" + a.substr(7) + "' (all|gateway|world)"; return o; }
      o.role = *r;
      o.role_from_cli = true;
    } else if (a.rfind("--config=", 0) == 0) {
      o.config_path = a.substr(9);
      if (o.config_path.empty()) { o.error = "--config needs a path"; return o; }
    } else if (a == "--check") {
      o.check_only = true;
    } else if (a.rfind("--run-for=", 0) == 0) {
      const std::string v = a.substr(10);
      if (v.empty() || v.size() > 7 || v.find_first_not_of("0123456789") != std::string::npos) {
        o.error = "--run-for needs milliseconds";
        return o;
      }
      o.run_for_ms = std::atoi(v.c_str());
    } else {
      o.error = "unknown argument '" + a + "'";
      return o;
    }
  }
  return o;
}

std::optional<Role> role_from_config_text(const std::string& text, std::string* error) {
  std::istringstream in(text);
  std::string line, section;
  while (std::getline(in, line)) {
    const auto b = line.find_first_not_of(" \t\r");
    if (b == std::string::npos || line[b] == '#') continue;
    line = line.substr(b);
    if (line[0] == '[') { section = line.substr(1, line.find(']') - 1); continue; }
    if (section != "process") continue;
    const auto eq = line.find('=');
    if (eq == std::string::npos) continue;
    std::string key = line.substr(0, eq), val = line.substr(eq + 1);
    val = val.substr(0, val.find('#'));   // trailing comment (a role name never contains '#')
    auto trim = [](std::string s) {
      const auto x = s.find_first_not_of(" \t\r\"");
      if (x == std::string::npos) return std::string();
      return s.substr(x, s.find_last_not_of(" \t\r\"") - x + 1);
    };
    if (trim(key) != "role") continue;
    auto r = parse_role(trim(val));
    if (!r && error) *error = "invalid role in config: '" + trim(val) + "'";
    return r;
  }
  return std::nullopt;
}

ProcessPlan plan_for(Role r) {
  ProcessPlan p;
  p.gateway_threads = role_runs_gateway(r);
  p.world_side = role_runs_world(r);
  p.needs_world_peer = r == Role::Gateway;
  return p;
}

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
  if (n.size() < 4 || n[0] == '#' || strcasecmp(n.c_str(), "Server") == 0) return NameVerdict::Rejected;   // reserved name: case-insensitive, like strcmpi
  for (char c : n) if (std::iscntrl(static_cast<unsigned char>(c))) return NameVerdict::Rejected;
  if (!allowed_.empty()) {
    for (char c : n) if (allowed_.find(c) == std::string::npos) return NameVerdict::BadLetters;
  }
  return NameVerdict::Ok;
}

// ---- default policy fixture: legacy-parity starting data (production: rathena.base config) ------
bool DefaultNamePolicy::starting_stats_valid(bool new_client, const rules::StartStats& s) const {
  return rules::starting_stats_valid(s, new_client);
}
bool DefaultNamePolicy::start_job_allowed(int job_flag) const {
  return job_flag > 0 && (job_flag & job_flags_) == job_flag && (job_flag == 1 || job_flag == 2);
}
int DefaultNamePolicy::job_flag_for_client_job(std::uint32_t client_job) const {
  // fixture numbers: client job 0 -> flag 1, client job 4218 -> flag 2 (legacy JOB_NOVICE / JOB_SUMMONER)
  return client_job == 0 ? 1 : client_job == 4218 ? 2 : 0;
}
std::optional<StartPackage> DefaultNamePolicy::starting_package(int job_flag, const rules::StartStats& s) const {
  if (!start_job_allowed(job_flag)) return std::nullopt;
  const rules::HpSp hs = rules::initial_hp_sp(s.vit, s.int_);
  StartPackage p;
  p.max_hp = hs.hp;
  p.max_sp = hs.sp;
  p.numbers["start_zeny"] = 0;
  p.numbers["start_job_flag"] = job_flag;
  return p;
}
std::vector<std::string> DefaultNamePolicy::delete_cleanup_plan() const {
  // Order of char_delete() in legacy char.cpp: relationships first, then owned data, row last.
  return {"divorce", "deadopt", "party_leave", "companions", "friends", "inventory", "cart", "memo",
          "registries", "skills", "mail", "status_changes", "bonus_scripts", "quests", "achievements",
          "guild_leave_or_break"};
}

}  // namespace gateway
