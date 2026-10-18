#include "legacy_npc/package.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace legacy_npc {

std::filesystem::path package_dir() {
#ifdef LEGACY_NPC_LUAU_DIR
  return LEGACY_NPC_LUAU_DIR;
#else
  return "luau";
#endif
}

static std::string slurp(const std::filesystem::path& p) {
  std::ifstream f(p, std::ios::binary);
  if (!f) throw std::runtime_error("cannot read " + p.string());
  std::ostringstream o;
  o << f.rdbuf();
  return o.str();
}

std::string long_string(const std::string& s) {
  std::string eq = "==";
  // text ending in ']' next to the closing bracket cannot be confused with it because the level is chosen above any "]=..=]" in the text
  while (s.find("]" + eq + "]") != std::string::npos) eq += "=";
  // Luau skips one newline right after the opening bracket: emit one so that text starting with a newline survives
  return "[" + eq + "[\n" + s + "]" + eq + "]";
}

std::string bundle_package(const std::filesystem::path& dir, bool with_tests) {
  std::vector<std::filesystem::path> files;
  for (auto& e : std::filesystem::directory_iterator(dir))
    if (e.is_regular_file() && e.path().extension() == ".luau") files.push_back(e.path());
  if (with_tests) {
    auto tdir = dir / "tests";
    if (std::filesystem::is_directory(tdir))
      for (auto& e : std::filesystem::directory_iterator(tdir))
        if (e.is_regular_file() && e.path().extension() == ".luau" && e.path().stem() != "run_cli") files.push_back(e.path());
  }
  std::sort(files.begin(), files.end());
  std::string out =
      "--!nonstrict\n"
      "local __defs, __loaded = {}, {}\n"
      "local function __require(path)\n"
      "  local name = string.match(path, '([^/]+)$')\n"
      "  local m = __loaded[name]\n"
      "  if m ~= nil then\n"
      "    if m == false then error('legacy.npc: cyclic require of ' .. name, 2) end\n"
      "    return m\n"
      "  end\n"
      "  local def = __defs[name]\n"
      "  if not def then error('legacy.npc: module not found: ' .. tostring(path), 2) end\n"
      "  __loaded[name] = false\n"
      "  m = def(__require)\n"
      "  if m == nil then m = true end\n"
      "  __loaded[name] = m\n"
      "  return m\n"
      "end\n";
  for (auto& f : files) {
    out += "__defs['" + f.stem().string() + "'] = function(require)\n";
    out += slurp(f);
    out += "\nend\n";
  }
  out += "LEGACY = __require('legacy')\n";
  if (with_tests) out += "LNPC_H = __require('harness')\nLNPC_TESTS = __require('all')\n";
  return out;
}

std::string unescape(const std::string& s) {
  std::string o;
  o.reserve(s.size());
  for (std::size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '\\' && i + 1 < s.size()) {
      char c = s[++i];
      o.push_back(c == 't' ? '\t' : c == 'n' ? '\n' : c == 'r' ? '\r' : c);
    } else {
      o.push_back(s[i]);
    }
  }
  return o;
}

PackageHost::PackageHost(PackageOptions opt)
    : opt_(std::move(opt)),
      backend_(std::make_shared<script_host::MemoryStorageBackend>()),
      storage_(backend_) {
  script_host::ModEnvironmentSpec spec;
  spec.mod_id = kPackageId;
  spec.granted = opt_.granted;
  spec.memory_cap_bytes = opt_.memory_cap_bytes;
  spec.budget.steps_per_dispatch = opt_.steps_per_dispatch;
  spec.budget.steps_per_slice = opt_.steps_per_slice;
  spec.budget.wall_us_per_dispatch = 0;  // CPU-bound indexing: the step budget is the guard in tests; operators set wall limits
  spec.budget.wall_us_per_tick = 0;
  storage_.set_quota(kPackageId, 64ull << 20);
  sandbox_ = std::make_shared<script_host::ModSandbox>(spec);
  sandbox_->set_storage(&storage_);
  script_host::ModSandbox::Host h;
  h.log = [this](const std::string& line) { log_.push_back(line); };
  h.add_timer = [](std::uint64_t, int) {};
  sandbox_->set_host(h);
  dialogue_ = std::make_unique<script_host::DialogueManager>();
  dialogue_->attach(kPackageId, sandbox_);
  auto r = sandbox_->run("legacy.npc", bundle_package(package_dir(), opt_.with_tests));
  if (r.status != script_host::RunStatus::Ok) throw std::runtime_error("legacy.npc failed to load: " + r.error);
}

script_host::RunResult PackageHost::run(const std::string& name, const std::string& source) {
  return sandbox_->run(name, source);
}

void PackageHost::run_ok(const std::string& name, const std::string& source) {
  auto r = run(name, source);
  if (r.status != script_host::RunStatus::Ok) throw std::runtime_error(name + ": " + r.error);
}

std::vector<std::vector<std::string>> PackageHost::records(const std::string& prefix) const {
  std::vector<std::vector<std::string>> out;
  for (auto& line : log_) {
    if (line.rfind(prefix, 0) != 0) continue;
    std::vector<std::string> f;
    std::size_t p = 0;
    while (true) {
      auto t = line.find('\t', p);
      f.push_back(unescape(line.substr(p, t == std::string::npos ? std::string::npos : t - p)));
      if (t == std::string::npos) break;
      p = t + 1;
    }
    out.push_back(std::move(f));
  }
  return out;
}

PackageHost& shared_host() {
  static PackageHost host;
  return host;
}

}  // namespace legacy_npc
