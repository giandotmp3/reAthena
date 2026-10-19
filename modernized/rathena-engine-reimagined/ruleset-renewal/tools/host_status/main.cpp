// usage: host_status <ruleset-renewal dir>
// Runs the status layer of ruleset-renewal inside script-host's real mod sandbox (same module system as the mods loader) and replays the status golden.
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <algorithm>
#include "script_host/reload.hpp"
#include "script_host/sandbox.hpp"
using namespace script_host;
namespace fs = std::filesystem;
static std::string slurp(const fs::path& p) { std::ifstream in(p, std::ios::binary); std::ostringstream o; o << in.rdbuf(); return o.str(); }
static std::string q(const std::string& s) { std::string o = "\""; for (unsigned char c : s) { if (c == '"' || c == '\\') { o += '\\'; o += (char)c; } else if (c < 0x20 || c == 0x7F) { char b[8]; std::snprintf(b, sizeof b, "\\%03d", c); o += b; } else o += (char)c; } return o + "\""; }
const char* kPrelude = R"LUA(--!nonstrict
local __defs, __loaded, __stems = {}, {}, {}
local function __norm(p) local out = {} for seg in string.gmatch(p, "[^/]+") do if seg == ".." then table.remove(out) elseif seg ~= "." then table.insert(out, seg) end end return table.concat(out, "/") end
local function __find(path, from)
  path = string.gsub(path, "%.luau$", "")
  if string.sub(path, 1, 1) == "." then local n = __norm(from .. "/" .. path) if __defs[n] then return n end
  else local n = __norm(path) if __defs[n] then return n end n = __norm(from .. "/" .. path) if __defs[n] then return n end end
  local stem = string.match(path, "([^/]+)$") local list = stem and __stems[stem]
  if list and #list == 1 then return list[1] end
  return nil
end
local __load
local function __mk(dir) return function(path) local n = __find(path, dir) if not n then error("module not found: " .. tostring(path) .. " (from '" .. dir .. "')", 2) end return __load(n) end end
function __load(name)
  local m = __loaded[name]
  if m ~= nil then if m == false then error("cyclic require of " .. name, 2) end return m end
  __loaded[name] = false
  local dir = string.match(name, "^(.*)/[^/]*$") or ""
  local f = __mk(dir)
  m = __defs[name](f, f)
  if m == nil then m = true end
  __loaded[name] = m
  return m
end
)LUA";
int main(int argc, char** argv) {
  fs::path pkg = argv[1];
  std::vector<std::pair<std::string, fs::path>> mods;
  for (auto d : {"scripts", "data", "tests/support"})
    for (auto it = fs::recursive_directory_iterator(pkg / d); it != fs::recursive_directory_iterator(); ++it) {
      if (!it->is_regular_file() || it->path().extension() != ".luau") continue;
      std::string rel = fs::relative(it->path(), pkg).generic_string();
      mods.emplace_back(rel.substr(0, rel.size() - 5), it->path());
    }
  mods.emplace_back("build/status_golden_re", pkg / "build" / "status_golden_re.luau");
  std::sort(mods.begin(), mods.end());
  std::string out = kPrelude;
  std::map<std::string, std::vector<std::string>> stems;
  for (auto& [name, f] : mods) { auto s = name.rfind('/'); stems[s == std::string::npos ? name : name.substr(s + 1)].push_back(name); }
  for (auto& [name, f] : mods) out += "__defs[" + q(name) + "] = function(require, import)\n" + slurp(f) + "\nend\n";
  for (auto& [stem, list] : stems) { out += "__stems[" + q(stem) + "] = {"; for (auto& n : list) out += q(n) + ","; out += "}\n"; }
  out += "__load(\"scripts/init\")\nSH_IMPORT = __mk(\"\")\n";
  ReloadManager rm; RngStreams rng{1}; rm.set_rng(&rng); rng.set_strict_parity(false);
  ModEnvironmentSpec e; e.mod_id = "ruleset.renewal"; e.memory_cap_bytes = 2048ull << 20;
  e.budget.wall_us_per_dispatch = 0; e.budget.wall_us_per_tick = 0; e.budget.steps_per_dispatch = 4000000000ull; e.budget.steps_per_slice = 4000000000ull;
  e.config["legacy_quirks"] = true;
  rm.set_environment(e);
  auto r = rm.reload("ruleset.renewal", out);
  if (!r.ok) { std::printf("LOAD FAILED: %s\n", r.error.c_str()); return 1; }
  auto sb = rm.active_sandbox("ruleset.renewal");
  std::vector<std::string> lines;
  sb->set_host({{}, [&](const std::string& l) { lines.push_back(l); }});
  const char* code = R"LUA(
    
    local runner = SH_IMPORT("tests/support/status_runner")
    local golden = runner.load()
    local rep = runner.run_all(golden.cases)
    print(string.format("status golden in the real sandbox: %d / %d cases matched, %d outputs, overflow %d, %ds", rep.matched, rep.total, rep.comparisons, rep.overflow, 0))
    local S = SH_IMPORT("scripts/status/init")
    print("statuses defined", S.status.count())
  )LUA";
  auto t0 = std::chrono::steady_clock::now();
  auto rr = sb->run("eval", code);
  double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  if (rr.status != RunStatus::Ok) { std::printf("RUN FAILED: %s\n", rr.error.c_str()); return 2; }
  for (auto& l : lines) std::printf("%s\n", l.c_str());
  std::printf("wall %.0f ms\n", ms);
  return 0;
}
