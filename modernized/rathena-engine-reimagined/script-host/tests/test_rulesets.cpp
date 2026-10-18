// The two ruleset packages (../../ruleset-prerenewal, ../../ruleset-renewal) running on the REAL pipeline runtime.
//   * both golden suites replayed through pipeline.run (5,424 and 8,858 cases), with timings
//   * a mod inserts a stage into the renewal weapon pipeline; legacy_quirks from the environment config
//   * the PRE -> RE edit delta of ruleset-renewal/docs/PIPELINE_DIFF.md (23 operations, and the 21 that `move` allows)
//   * the packages do not depend on each other; the Luau fake host the CLI suites use agrees with the real runtime
// The packages are loaded as the mod loader does: modules bundled behind `import`, entry scripts/init (which registers the package
// with the globals pipeline / num / config).
// Tests skip (with the reason) when a package directory or its expanded golden data (tools/run_tests.py writes build/golden_*.luau)
// is missing.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <tuple>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>

#include "fixtures.hpp"
#include "script_host/pipeline.hpp"

using namespace script_host;
namespace fs = std::filesystem;

namespace {

fs::path root() { return fs::path(ENGINE_ROOT); }
fs::path pre_dir() { return root() / "ruleset-prerenewal"; }
fs::path re_dir() { return root() / "ruleset-renewal"; }

std::string slurp_path(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  if (!in) throw mt::Failure("cannot read " + p.string());
  std::ostringstream o;
  o << in.rdbuf();
  return o.str();
}

std::string lua_quote(const std::string& s) {
  std::string o = "\"";
  for (unsigned char c : s) {
    if (c == '"' || c == '\\') { o += '\\'; o += static_cast<char>(c); }
    else if (c < 0x20 || c == 0x7F) { char b[8]; std::snprintf(b, sizeof b, "\\%03d", c); o += b; }
    else o += static_cast<char>(c);
  }
  return o + "\"";
}

// Same module system as mods::bundle_package (host_runtime.cpp): package-relative or file-relative names, one function per module.
const char* kPrelude = R"LUA(--!nonstrict
local __defs, __loaded, __stems = {}, {}, {}
local function __norm(p)
  local out = {}
  for seg in string.gmatch(p, "[^/]+") do
    if seg == ".." then table.remove(out) elseif seg ~= "." then table.insert(out, seg) end
  end
  return table.concat(out, "/")
end
local function __find(path, from)
  path = string.gsub(path, "%.luau$", "")
  if string.sub(path, 1, 1) == "." then
    local n = __norm(from .. "/" .. path)
    if __defs[n] then return n end
  else
    local n = __norm(path)
    if __defs[n] then return n end
    n = __norm(from .. "/" .. path)
    if __defs[n] then return n end
  end
  local stem = string.match(path, "([^/]+)$")
  local list = stem and __stems[stem]
  if list and #list == 1 then return list[1] end
  return nil
end
local __load
local function __mk(dir)
  return function(path)
    local n = __find(path, dir)
    if not n then error("module not found: " .. tostring(path) .. " (from '" .. dir .. "')", 2) end
    return __load(n)
  end
end
function __load(name)
  local m = __loaded[name]
  if m ~= nil then
    if m == false then error("cyclic require of " .. name, 2) end
    return m
  end
  __loaded[name] = false
  local dir = string.match(name, "^(.*)/[^/]*$") or ""
  local f = __mk(dir)
  m = __defs[name](f, f)
  if m == nil then m = true end
  __loaded[name] = m
  return m
end
)LUA";

// Modules: every *.luau under the given directories (relative to the package), except `.d.luau`.
std::string bundle(const fs::path& pkg, const std::vector<std::string>& dirs, bool with_golden) {
  std::vector<std::pair<std::string, fs::path>> mods;
  for (auto& d : dirs) {
    fs::path base = pkg / d;
    if (!fs::is_directory(base)) continue;
    for (auto it = fs::recursive_directory_iterator(base); it != fs::recursive_directory_iterator(); ++it) {
      if (!it->is_regular_file() || it->path().extension() != ".luau") continue;
      std::string fn = it->path().filename().string();
      if (fn.size() > 7 && fn.compare(fn.size() - 7, 7, ".d.luau") == 0) continue;
      std::string rel = fs::relative(it->path(), pkg).generic_string();
      if (rel.rfind("tests/acceptance/", 0) == 0) continue;  // the CLI suites; this file replaces their runner
      mods.emplace_back(rel.substr(0, rel.size() - 5), it->path());
    }
  }
  if (with_golden && fs::is_directory(pkg / "build"))
    for (auto& e : fs::directory_iterator(pkg / "build"))
      if (e.path().extension() == ".luau" && e.path().filename().string().rfind("golden_", 0) == 0)
        mods.emplace_back("build/" + e.path().stem().string(), e.path());
  std::sort(mods.begin(), mods.end());
  std::string out = kPrelude;
  std::map<std::string, std::vector<std::string>> stems;
  for (auto& [name, file] : mods) {
    (void)file;
    auto slash = name.rfind('/');
    stems[slash == std::string::npos ? name : name.substr(slash + 1)].push_back(name);
  }
  for (auto& [name, file] : mods) out += "__defs[" + lua_quote(name) + "] = function(require, import)\n" + slurp_path(file) + "\nend\n";
  for (auto& [stem, list] : stems) {
    out += "__stems[" + lua_quote(stem) + "] = {";
    for (auto& n : list) out += lua_quote(n) + ",";
    out += "}\n";
  }
  // the entry module registers the package itself under script-host (see scripts/init.luau)
  out += "__load(\"scripts/init\")\nSH_IMPORT = __mk(\"\")\n";
  return out;
}

std::vector<std::string> eval(ReloadManager& rm, const std::string& mod, const std::string& src) {
  auto sb = rm.active_sandbox(mod);
  if (!sb) throw mt::Failure("mod not loaded: " + mod);
  std::vector<std::string> lines;
  sb->set_host({{}, [&](const std::string& l) { lines.push_back(l); }});
  auto r = sb->run("eval", src);
  sb->set_host({});
  if (r.status != RunStatus::Ok) throw mt::Failure("eval failed: " + r.error);
  return lines;
}

ModEnvironmentSpec big_env(const std::string& mod) {
  ModEnvironmentSpec e = fx::env(mod, 2048ull << 20);
  e.budget.steps_per_dispatch = 4000000000ull;
  e.budget.steps_per_slice = 4000000000ull;
  return e;
}

struct Pkg {
  fs::path dir;
  std::string id;
  explicit Pkg(const fs::path& d, const std::string& i) : dir(d), id(i) {}
};

struct Loaded {
  std::unique_ptr<ReloadManager> rm = std::make_unique<ReloadManager>();
  RngStreams rng{1};
  Loaded() { rm->set_rng(&rng); rng.set_strict_parity(false); }
};

void need_pkg(const Pkg& p, bool golden) {
  if (!fs::is_directory(p.dir / "scripts")) throw mt::Skip("package not found: " + p.dir.string());
  if (golden) {
    bool any = false;
    if (fs::is_directory(p.dir / "build"))
      for (auto& e : fs::directory_iterator(p.dir / "build")) any = any || e.path().filename().string().rfind("golden_", 0) == 0;
    if (!any) throw mt::Skip("expanded golden data missing: run python3 -I tools/run_tests.py in " + p.dir.string());
  }
}

std::unique_ptr<Loaded> load_pkg(const Pkg& p, bool golden, bool legacy_quirks = true) {
  need_pkg(p, golden);
  auto w = std::make_unique<Loaded>();
  ModEnvironmentSpec e = big_env(p.id);
  e.config["legacy_quirks"] = legacy_quirks;
  w->rm->set_environment(e);
  auto r = w->rm->reload(p.id, bundle(p.dir, {"scripts", "data", "tests/support"}, golden));
  if (!r.ok) throw mt::Failure("package " + p.id + " failed to load: " + r.error);
  return w;
}

using Clock = std::chrono::steady_clock;
double ms_since(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }

// Lua that replays every golden case through weapon.calc_attack (and so pipeline.run) and prints matched / total
const char* kReplay = R"LUA(
  local runner = SH_IMPORT("tests/support/golden_runner")
  local rep = runner.run_all(runner.load())
  print(rep.matched, rep.total, rep.stage_mismatch)
)LUA";

// Replays the golden cases N times and measures calc_attack alone (like tests/bench_weapon.luau, without the comparison)
const char* kBenchSetup = R"LUA(
  local runner = SH_IMPORT("tests/support/golden_runner")
  local weapon = SH_IMPORT("scripts/damage/weapon")
  local unit = SH_IMPORT("scripts/damage/unit")
  local defaults = SH_IMPORT("data/damage_cfg")
  local fake_ctx = SH_IMPORT("tests/support/fake_ctx")
  PREP = {}
  for _, c in ipairs(runner.load()) do
    local cfg = {}
    for k, v in pairs(defaults) do cfg[k] = v end
    for k, v in pairs(c.cfg) do cfg[k] = v end
    PREP[#PREP + 1] = { cfg = cfg, a = unit.normalize(c.a), t = unit.normalize(c.t), map = c.map, draws = c.out.draws }
  end
  local cur, i = nil, 0
  local rng = {
    rnd = function() i = i + 1; return cur[i] end,
    value = function(lo, hi) i = i + 1; return cur[i][3] end,
  }
  local ctx = fake_ctx.new(rng)
  function PASS()
    for _, p in ipairs(PREP) do
      cur, i = p.draws, 0
      weapon.calc_attack(ctx, p.cfg, p.a, p.t, { map = p.map })
    end
  end
  print(#PREP)
)LUA";

void replay_and_time(const Pkg& p, std::size_t expect_cases) {
  auto w = load_pkg(p, true);
  auto t0 = Clock::now();
  auto out = eval(*w->rm, p.id, kReplay);
  double replay_ms = ms_since(t0);
  CHECK_EQ(out.size(), std::size_t(1));
  std::istringstream is(out[0]);
  std::size_t matched = 0, total = 0, stage_mismatch = 99;
  is >> matched >> total >> stage_mismatch;
  CHECK_EQ(total, expect_cases);
  CHECK_EQ(matched, total);          // every golden case: damage, damage2, type, div, flag, dmg_lv, draws
  CHECK_EQ(stage_mismatch, std::size_t(0));  // and every stage probe, in order
  auto setup = eval(*w->rm, p.id, kBenchSetup);
  CHECK_EQ(std::stoull(setup[0]), expect_cases);
  eval(*w->rm, p.id, "PASS()");  // warm-up
  const int passes = 10;
  t0 = Clock::now();
  for (int i = 0; i < passes; ++i) eval(*w->rm, p.id, "PASS()");
  double us = ms_since(t0) * 1000.0 / (static_cast<double>(passes) * static_cast<double>(expect_cases));
  std::printf("[ PERF  ] %s on the real pipeline runtime: %zu / %zu golden cases match; replay incl. compare %.1f us/case, calc_attack alone %.2f us/case\n",
              p.id.c_str(), matched, total, replay_ms * 1000.0 / static_cast<double>(total), us);
}

}  // namespace

// Optional baseline: RATHENA_BASELINE_ENGINE_ROOT points at a checkout of the packages as they were before the pipeline runtime
// (their private stage loops); the same measurement is printed so the cost of the engine-driven loop can be compared.
LUAU_TEST("BR-RULESET-119", "baseline timing: the packages before the pipeline runtime, same measurement (only when RATHENA_BASELINE_ENGINE_ROOT is set)") {
  const char* env = std::getenv("RATHENA_BASELINE_ENGINE_ROOT");
  if (!env) throw mt::Skip("RATHENA_BASELINE_ENGINE_ROOT not set");
  for (auto [name, id, n] : {std::tuple{std::string("ruleset-prerenewal"), std::string("ruleset.prerenewal"), 5424}, std::tuple{std::string("ruleset-renewal"), std::string("ruleset.renewal"), 8858}}) {
    fs::path dir = fs::path(env) / name;
    Loaded w;
    w.rm->set_environment(big_env(id));
    auto r = w.rm->reload(id, bundle(dir, {"scripts", "data", "tests/support"}, true));
    if (!r.ok) throw mt::Failure("baseline " + id + " failed to load: " + r.error);
    eval(*w.rm, id, kBenchSetup);
    eval(*w.rm, id, "PASS()");
    const int passes = 10;
    auto t0 = Clock::now();
    for (int i = 0; i < passes; ++i) eval(*w.rm, id, "PASS()");
    double us = ms_since(t0) * 1000.0 / (static_cast<double>(passes) * n);
    t0 = Clock::now();
    eval(*w.rm, id, kReplay);
    double rep = ms_since(t0) * 1000.0 / n;
    std::printf("[ PERF  ] BASELINE %s (private stage loop): replay incl. compare %.1f us/case, calc_attack alone %.2f us/case\n", id.c_str(), rep, us);
  }
}

LUAU_TEST("BR-RULESET-119", "renewal golden replay through the real pipeline runtime: 8,858 / 8,858 cases and every stage probe match") {
  replay_and_time(Pkg(re_dir(), "ruleset.renewal"), 8858);
}

LUAU_TEST("BR-RULESET-119", "pre-renewal golden replay through the real pipeline runtime: 5,424 / 5,424 cases and every stage probe match") {
  replay_and_time(Pkg(pre_dir(), "ruleset.prerenewal"), 5424);
}

LUAU_TEST("BR-RULESET-019", "both packages register their orders and one body per weapon stage with the real runtime; init and final are protected") {
  for (auto [dir, id, n] : {std::tuple{pre_dir(), std::string("ruleset.prerenewal"), 15}, std::tuple{re_dir(), std::string("ruleset.renewal"), 24}}) {
    Pkg p(dir, id);
    auto w = load_pkg(p, false);
    auto info = w->rm->pipelines().describe("weapon");
    CHECK_EQ(info.owner_mod, id);
    CHECK_EQ(info.steps.size(), static_cast<std::size_t>(n));
    for (auto& s : info.steps) {
      CHECK(s.has_body);
      CHECK_EQ(s.kind, std::string("base"));
      CHECK_EQ(s.mode, std::string("own"));
      CHECK_EQ(s.protected_stage, s.id == "init" || s.id == "final");
    }
    // magic (and for pre-renewal misc) are declared without bodies
    CHECK(w->rm->pipelines().defined("magic"));
    for (auto& s : w->rm->pipelines().describe("magic").steps) CHECK(!s.has_body);
    auto names = w->rm->pipelines().names();
    CHECK_EQ(names.size(), std::size_t(id == "ruleset.prerenewal" ? 3 : 2));
  }
}

LUAU_TEST("BR-RULESET-019", "a mod inserts a stage into the renewal weapon pipeline and changes a worked example (RES 840 -> +10% after element = 924)") {
  auto w = load_pkg(Pkg(re_dir(), "ruleset.renewal"), true);
  // golden worked example E-2: 1000 ATK against RES 100 -> 840 (the 3rd worked example)
  const char* run_case = R"LUA(
    local runner = SH_IMPORT("tests/support/golden_runner")
    local weapon = SH_IMPORT("scripts/damage/weapon")
    local unit = SH_IMPORT("scripts/damage/unit")
    local defaults = SH_IMPORT("data/damage_cfg")
    local fake_ctx = SH_IMPORT("tests/support/fake_ctx")
    local scripted = SH_IMPORT("tests/support/scripted_rng").scripted
    function CASE()
      local n = 0
      for _, c in ipairs(runner.load()) do
        if c.cat == "worked_example" then
          n = n + 1
          if n == 3 then return c end
        end
      end
    end
    function ATTACK()
      local c = CASE()
      local cfg = {}
      for k, v in pairs(defaults) do cfg[k] = v end
      for k, v in pairs(c.cfg) do cfg[k] = v end
      local res = weapon.calc_attack(fake_ctx.new(scripted(c.out.draws)), cfg, unit.normalize(c.a), unit.normalize(c.t), { map = c.map })
      print(res.damage, res.damage2, res.dmg_lv, c.out.damage)
    end
  )LUA";
  eval(*w->rm, "ruleset.renewal", run_case);
  CHECK_EQ(eval(*w->rm, "ruleset.renewal", "ATTACK()")[0], std::string("840\t0\t5\t840"));
  // the mod: insert_after("weapon", "element", ...) with the architecture's example name
  ModEnvironmentSpec e = fx::env("mymod");
  w->rm->set_environment(e);
  fx::load(*w->rm, "mymod", R"LUA(
    pipeline.insert_after("weapon", "element", "mymod:fire_brand", function(ctx) ctx:mul_pct(110) end, { mode = "modify", priority = 50 })
  )LUA");
  CHECK_EQ(eval(*w->rm, "ruleset.renewal", "ATTACK()")[0], std::string("924\t0\t5\t840"));
  auto order = w->rm->pipelines().order("weapon");
  auto at = std::find(order.begin(), order.end(), "mymod:fire_brand");
  CHECK(at != order.end());
  CHECK_EQ(*(at - 1), std::string("element"));
  CHECK_EQ(*(at + 1), std::string("crit_multiplier"));
  // the editing mod can be reloaded: its stage disappears with the old generation and the baseline returns
  fx::load(*w->rm, "mymod", "assert(true)");
  CHECK_EQ(eval(*w->rm, "ruleset.renewal", "ATTACK()")[0], std::string("840\t0\t5\t840"));
  CHECK_EQ(w->rm->pipelines().order("weapon").size(), std::size_t(24));
}

LUAU_TEST("BR-RULESET-019", "a mod's cancel / replace / move on the renewal weapon pipeline, protected stages need the permission") {
  auto w = load_pkg(Pkg(re_dir(), "ruleset.renewal"), true);
  eval(*w->rm, "ruleset.renewal", R"LUA(
    local runner = SH_IMPORT("tests/support/golden_runner")
    local weapon = SH_IMPORT("scripts/damage/weapon")
    local unit = SH_IMPORT("scripts/damage/unit")
    local defaults = SH_IMPORT("data/damage_cfg")
    local fake_ctx = SH_IMPORT("tests/support/fake_ctx")
    local scripted = SH_IMPORT("tests/support/scripted_rng").scripted
    function ATTACK()
      local n, c = 0, nil
      for _, k in ipairs(runner.load()) do
        if k.cat == "worked_example" then n = n + 1 if n == 3 then c = k break end end
      end
      local cfg = {}
      for k, v in pairs(defaults) do cfg[k] = v end
      for k, v in pairs(c.cfg) do cfg[k] = v end
      local trace = {}
      local res = weapon.calc_attack(fake_ctx.new(scripted(c.out.draws)), cfg, unit.normalize(c.a), unit.normalize(c.t), { map = c.map, trace = trace })
      local names = {}
      for _, p in ipairs(trace) do names[#names + 1] = p[1] end
      print(res.damage, table.concat(names, ","))
    end
  )LUA");
  auto base = eval(*w->rm, "ruleset.renewal", "ATTACK()")[0];
  CHECK(fx::contains(base, "840\t"));
  // cancel 'res': RES no longer applies (1000), and its probe is gone
  w->rm->set_environment(fx::env("nores"));
  fx::load(*w->rm, "nores", "pipeline.cancel('weapon', 'res')");
  auto nores = eval(*w->rm, "ruleset.renewal", "ATTACK()")[0];
  CHECK(fx::contains(nores, "1000\t") && !fx::contains(nores, ",res,"));
  // replace 'res' with a hook (runs in the editing mod's state, sets the lane): the ruleset's res body is skipped
  fx::load(*w->rm, "nores", "pipeline.replace('weapon', 'res', 'nores:fixed', function(ctx) ctx:set(777) end)");
  CHECK(fx::contains(eval(*w->rm, "ruleset.renewal", "ATTACK()")[0], "777\t"));
  // move 'res' behind 'defense': the same damage here (DEF 0 in this case), but the probe order changes
  fx::load(*w->rm, "nores", "pipeline.move('weapon', 'res', { after = 'defense' })");
  auto moved = eval(*w->rm, "ruleset.renewal", "ATTACK()")[0];
  CHECK(fx::contains(moved, "840\t"));
  CHECK(moved != base);
  CHECK(moved.find(",defred,") < moved.find(",res,"));
  CHECK(base.find(",res,") < base.find(",defs,"));
  // protected stages: init and final
  auto bad = w->rm->reload("nores", "pipeline.cancel('weapon', 'final')");
  CHECK(!bad.ok && fx::contains(bad.error, "cannot cancel protected stage weapon/final without the protected-stage permission"));
  ModEnvironmentSpec trusted = fx::env("nores");
  trusted.granted.insert(Permission::ProtectedStages);
  w->rm->set_environment(trusted);
  CHECK(w->rm->reload("nores", "pipeline.replace('weapon', 'final', 'nores:final', function(ctx) end)").ok);
  CHECK(fx::contains(eval(*w->rm, "ruleset.renewal", "ATTACK()")[0], "777\t") == false);  // the old edits are gone
}

LUAU_TEST("BR-RULESET-118", "legacy_quirks flows from the environment config through engine.config to the package: renewal monster crit 1072 (legacy) vs 740 (corrected)") {
  const char* src = R"LUA(
    local runner = SH_IMPORT("tests/support/golden_runner")
    local weapon = SH_IMPORT("scripts/damage/weapon")
    local unit = SH_IMPORT("scripts/damage/unit")
    local defaults = SH_IMPORT("data/damage_cfg")
    local fake_ctx = SH_IMPORT("tests/support/fake_ctx")
    local scripted = SH_IMPORT("tests/support/scripted_rng").scripted
    for _, c in ipairs(runner.load()) do
      if c.id == 2520 then
        local cfg = {}
        for k, v in pairs(defaults) do cfg[k] = v end  -- the package default: engine.config.legacy_quirks
        local res = weapon.calc_attack(fake_ctx.new(scripted(c.out.draws)), cfg, unit.normalize(c.a), unit.normalize(c.t), { map = c.map })
        print(config.legacy_quirks, defaults.legacy_quirks, res.damage, c.out.damage)
      end
    end
  )LUA";
  auto legacy = load_pkg(Pkg(re_dir(), "ruleset.renewal"), true, true);
  CHECK_EQ(eval(*legacy->rm, "ruleset.renewal", src)[0], std::string("true\ttrue\t1072\t1072"));
  auto fixed = load_pkg(Pkg(re_dir(), "ruleset.renewal"), true, false);
  CHECK_EQ(eval(*fixed->rm, "ruleset.renewal", src)[0], std::string("false\tfalse\t740\t1072"));
}

LUAU_TEST("BR-RULESET-118", "legacy_quirks reaches the pre-renewal package too (weapon_damage_rate of a rifle)") {
  const char* src = R"LUA(
    local weapon = SH_IMPORT("scripts/damage/weapon")
    local unit = SH_IMPORT("scripts/damage/unit")
    local defaults = SH_IMPORT("data/damage_cfg")
    local fake_ctx = SH_IMPORT("tests/support/fake_ctx")
    local scripted = SH_IMPORT("tests/support/scripted_rng").scripted
    local src = unit.normalize({ kind = "pc", wtype1 = 18, level = 1, no_sizefix = true, rhw = { atk = 100, atk2 = 0, ele = 0, wlv = 1 },
                                 bonus = { aadd_ele = { [2] = 20 }, wdr_r = -10 } })
    print(weapon.base_damage(fake_ctx.new(scripted({})), src, "r", 1, true, false, true, { cfg = defaults }))
  )LUA";
  auto legacy = load_pkg(Pkg(pre_dir(), "ruleset.prerenewal"), false, true);
  CHECK_EQ(eval(*legacy->rm, "ruleset.prerenewal", src)[0], std::string("120"));
  auto fixed = load_pkg(Pkg(pre_dir(), "ruleset.prerenewal"), false, false);
  CHECK_EQ(eval(*fixed->rm, "ruleset.prerenewal", src)[0], std::string("90"));
}

// ---- the PRE -> RE edit delta ----------------------------------------------------------------------------------------------
namespace {

struct Op { std::vector<std::string> a; };

// docs/pipeline_diff.json: "api": { ..., "ops": [["cancel", "x"], ["insert_after", "a", "b"], ["replace", "y"], ...] }
std::vector<Op> doc_ops() {
  std::string j = slurp_path(re_dir() / "docs" / "pipeline_diff.json");
  auto api = j.find("\"api\"");
  auto ops = j.find("\"ops\"", api);
  if (api == std::string::npos || ops == std::string::npos) throw mt::Failure("docs/pipeline_diff.json has no api.ops");
  auto p = j.find('[', ops);
  std::vector<Op> out;
  int depth = 0;
  Op cur;
  for (std::size_t i = p; i < j.size(); ++i) {
    char c = j[i];
    if (c == '[') { if (++depth == 2) cur = Op{}; }
    else if (c == ']') { if (depth == 2) out.push_back(cur); if (--depth == 0) break; }
    else if (c == '"' && depth == 2) {
      std::size_t e = j.find('"', i + 1);
      cur.a.push_back(j.substr(i + 1, e - i - 1));
      i = e;
    }
  }
  return out;
}

// ORDER.weapon of a package's scripts/pipeline.luau, read by running the module in a scratch sandbox
std::vector<std::string> package_order(const fs::path& pkg) {
  ReloadManager rm;
  rm.set_environment(fx::env("scratch"));
  std::string src = slurp_path(pkg / "scripts" / "pipeline.luau");
  fx::load(rm, "scratch", "assert(true)");
  std::string wrapped = "local import = function() error('unexpected import') end\nlocal function mod()\n" + src + "\nend\nlocal M = mod()\nprint(table.concat(M.ORDER.weapon, ','))\n";
  auto out = eval(rm, "scratch", wrapped);
  std::vector<std::string> v;
  std::stringstream ss(out.at(0));
  std::string tok;
  while (std::getline(ss, tok, ',')) v.push_back(tok);
  return v;
}

std::string lua_list(const std::vector<std::string>& v) {
  std::string s = "{";
  for (auto& x : v) s += lua_quote(x) + ",";
  return s + "}";
}

std::string J(const std::vector<std::string>& v) {
  std::string s;
  for (auto& x : v) s += (s.empty() ? "" : ",") + x;
  return s;
}

std::string strip_ns(std::string s) {
  const std::string ns = "ruleset.renewal:";
  if (s.compare(0, ns.size(), ns) == 0) s.erase(0, ns.size());
  return s;
}

// A stand-in for the pre-renewal ruleset: the real PRE order, a recording body for every stage (init and final protected)
std::string stand_in_ruleset(const std::vector<std::string>& pre) {
  return "local order = " + lua_list(pre) + "\n" + R"LUA(
    pipeline.define("weapon", order)
    for _, n in ipairs(order) do
      pipeline.stage("weapon", n, function(S) S.log[#S.log + 1] = n end, { protected = n == "init" or n == "final" })
    end
    function RUN()
      local S = { log = {}, v = 0, dmg = { x = 0, get = function(s) return s.x end, set = function(s, v) s.x = v end } }
      local r = pipeline.run("weapon", S, { lane = S.dmg })
      print(table.concat(S.log, ","), r and table.concat(r.ran, ",") or "")
    end
  )LUA";
}

std::string delta_mod(const std::vector<Op>& ops) {
  std::string s;
  for (auto& o : ops) {
    const std::string& k = o.a[0];
    if (k == "cancel") s += "pipeline.cancel('weapon', " + lua_quote(o.a[1]) + ")\n";
    else if (k == "insert_after") s += "pipeline.insert_after('weapon', " + lua_quote(o.a[1]) + ", " + lua_quote(o.a[2]) + ", function(ctx) end, { mode = 'modify' })\n";
    else if (k == "replace") s += "pipeline.replace('weapon', " + lua_quote(o.a[1]) + ", " + lua_quote("ruleset.renewal:" + o.a[1]) + ", function(ctx) end)\n";
    else if (k == "move_after") s += "pipeline.move('weapon', " + lua_quote(o.a[1]) + ", { after = " + lua_quote(o.a[2]) + " })\n";
    else throw mt::Failure("unknown op " + k);
  }
  return s;
}

}  // namespace

LUAU_TEST("BR-RULESET-019", "the 23 operations of PIPELINE_DIFF.md (3 cancel, 12 insert_after, 8 replace) turn the PRE order into the RE order, with the source mod of every edit") {
  if (!fs::is_directory(pre_dir() / "scripts") || !fs::is_directory(re_dir() / "docs")) throw mt::Skip("ruleset packages not found");
  auto pre = package_order(pre_dir()), re = package_order(re_dir());
  CHECK_EQ(pre.size(), std::size_t(15));
  CHECK_EQ(re.size(), std::size_t(24));
  auto ops = doc_ops();
  CHECK_EQ(ops.size(), std::size_t(23));
  std::map<std::string, int> counts;
  for (auto& o : ops) ++counts[o.a[0]];
  CHECK_EQ(counts["cancel"], 3);
  CHECK_EQ(counts["insert_after"], 12);
  CHECK_EQ(counts["replace"], 8);

  Loaded w;
  w.rm->set_environment(fx::env("ruleset.prerenewal"));
  fx::load(*w.rm, "ruleset.prerenewal", stand_in_ruleset(pre));
  // the renewal delta replaces 'init' (protected): refused without the permission, accepted with it
  ModEnvironmentSpec delta = fx::env("ruleset.renewal");
  w.rm->set_environment(delta);
  auto denied = w.rm->reload("ruleset.renewal", delta_mod(ops));
  CHECK(!denied.ok);
  CHECK(fx::contains(denied.error, "mod 'ruleset.renewal' cannot replace protected stage weapon/init without the protected-stage permission"));
  delta.granted.insert(Permission::ProtectedStages);
  w.rm->set_environment(delta);
  fx::load(*w.rm, "ruleset.renewal", delta_mod(ops));

  auto info = w.rm->pipelines().describe("weapon");
  std::vector<std::string> got;
  for (auto& s : info.steps)
    if (s.active) got.push_back(strip_ns(s.id));
  CHECK_EQ(got.size(), re.size());
  for (std::size_t i = 0; i < re.size(); ++i) CHECK_MSG(got[i] == re[i], "position " + std::to_string(i) + ": " + got[i] + " vs " + re[i]);
  // the moved stages come back under new ids; 8 same-named stages carry another body
  std::size_t edits = 0, inserted = 0, replaced = 0, cancelled = 0;
  for (auto& s : info.steps) {
    for (auto& e : s.edits) {
      CHECK_EQ(e.mod, std::string("ruleset.renewal"));  // the source mod of every edit
      ++edits;
      if (e.op == "replace") ++replaced;
      if (e.op == "cancel") ++cancelled;
      if (e.op == "insert_after") ++inserted;
    }
    if (s.kind == "inserted") { CHECK_EQ(s.source, std::string("ruleset.renewal")); CHECK(s.id.rfind("ruleset.renewal:", 0) == 0); }
  }
  CHECK_EQ(edits, std::size_t(23));
  CHECK_EQ(replaced, std::size_t(8));
  CHECK_EQ(cancelled, std::size_t(3));
  CHECK_EQ(inserted, std::size_t(12));
  // the defined order of the ruleset itself was not touched: removing the delta restores the PRE order exactly
  fx::load(*w.rm, "ruleset.renewal", "assert(true)");
  CHECK_EQ(J(w.rm->pipelines().order("weapon")), J(pre));
  // and the effective order actually runs: with the delta back, the bodies execute in the RE order
  fx::load(*w.rm, "ruleset.renewal", delta_mod(ops));
  auto run = eval(*w.rm, "ruleset.prerenewal", "RUN()");
  CHECK_EQ(run.size(), std::size_t(1));
  // own bodies that still run: the 4 stages nobody replaced or cancelled; the hooks: 8 replacements and 12 inserted stages, in effective order
  std::vector<std::string> hooks;
  for (auto& s : w.rm->pipelines().describe("weapon").steps)
    if (s.active && s.body_mod == "ruleset.renewal") hooks.push_back("ruleset.renewal/" + (s.body_id.empty() ? s.id : s.body_id));
  CHECK_EQ(hooks.size(), std::size_t(20));
  std::string joined_hooks;
  for (auto& h : hooks) joined_hooks += (joined_hooks.empty() ? "" : ",") + h;
  CHECK_EQ(run[0], std::string("const_add,div_hands,map_rates,final\t") + joined_hooks);
}

LUAU_TEST("BR-RULESET-019", "with `move` the delta takes 21 operations (1 cancel, 2 move, 10 insert_after, 8 replace) and keeps the original ids") {
  if (!fs::is_directory(pre_dir() / "scripts") || !fs::is_directory(re_dir() / "docs")) throw mt::Skip("ruleset packages not found");
  auto pre = package_order(pre_dir()), re = package_order(re_dir());
  auto ops23 = doc_ops();
  // a moved stage (cancelled and re-inserted under a new id in the 23-op list) becomes one `move`; everything else is unchanged
  std::set<std::string> moved;
  for (auto& o : ops23)
    if (o.a[0] == "insert_after" && o.a[2] == "ruleset.renewal:card_attacker") moved.insert("card_attacker");
    else if (o.a[0] == "insert_after" && o.a[2] == "ruleset.renewal:skill_atk_bonus") moved.insert("skill_atk_bonus");
  CHECK_EQ(moved.size(), std::size_t(2));
  std::vector<Op> ops;
  for (auto& o : ops23) {
    if (o.a[0] == "cancel" && moved.count(o.a[1])) continue;
    if (o.a[0] == "insert_after" && (o.a[2] == "ruleset.renewal:card_attacker" || o.a[2] == "ruleset.renewal:skill_atk_bonus")) {
      std::string st = o.a[2].substr(std::string("ruleset.renewal:").size());
      ops.push_back(Op{{"move_after", st, o.a[1]}});
      continue;
    }
    ops.push_back(o);
  }
  // inserts anchored on the re-inserted ids now anchor on the original stage names
  for (auto& o : ops)
    if (o.a[0] == "insert_after")
      for (auto* a : {&o.a[1]})
        for (const char* m : {"card_attacker", "skill_atk_bonus"})
          if (*a == std::string("ruleset.renewal:") + m) *a = m;
  CHECK_EQ(ops.size(), std::size_t(21));

  Loaded w;
  w.rm->set_environment(fx::env("ruleset.prerenewal"));
  fx::load(*w.rm, "ruleset.prerenewal", stand_in_ruleset(pre));
  ModEnvironmentSpec delta = fx::env("ruleset.renewal");
  delta.granted.insert(Permission::ProtectedStages);
  w.rm->set_environment(delta);
  fx::load(*w.rm, "ruleset.renewal", delta_mod(ops));
  std::vector<std::string> got;
  for (auto& s : w.rm->pipelines().describe("weapon").steps)
    if (s.active) got.push_back(strip_ns(s.id));
  CHECK_EQ(J(got), J(re));
  // the stages kept their original ids and bodies (they were moved, not re-created)
  for (auto& s : w.rm->pipelines().describe("weapon").steps)
    if (s.id == "card_attacker" || s.id == "skill_atk_bonus") CHECK_EQ(s.kind, std::string("base"));
}

LUAU_TEST("BR-RULESET-019", "the fake host the CLI suites use gives the same effective order as the real runtime for the same edits") {
  const fs::path fake = re_dir() / "tests" / "support" / "fake_host.luau";
  if (!fs::exists(fake) || !fs::is_directory(pre_dir() / "scripts")) throw mt::Skip("ruleset packages not found");
  auto pre = package_order(pre_dir());
  auto re = package_order(re_dir());
  auto ops23 = doc_ops();
  // the 23-op list in the real runtime
  Loaded w;
  w.rm->set_environment(fx::env("ruleset.prerenewal"));
  fx::load(*w.rm, "ruleset.prerenewal", stand_in_ruleset(pre));
  ModEnvironmentSpec delta = fx::env("ruleset.renewal");
  delta.granted.insert(Permission::ProtectedStages);
  w.rm->set_environment(delta);
  fx::load(*w.rm, "ruleset.renewal", delta_mod(ops23));
  auto real = w.rm->pipelines().order("weapon");
  // the same calls on the fake host (inside the ruleset's sandbox: the double is a plain Luau module)
  std::string drive = "local fake = (function()\n" + slurp_path(fake) + "\nend)()\nlocal engine = fake()\nlocal pl = engine.pipeline\npl.define('weapon', " + lua_list(pre) + ")\n";
  for (auto& o : ops23) {
    if (o.a[0] == "cancel") drive += "pl.cancel('weapon', " + lua_quote(o.a[1]) + ")\n";
    else if (o.a[0] == "insert_after") drive += "pl.insert_after('weapon', " + lua_quote(o.a[1]) + ", " + lua_quote(o.a[2]) + ", function() end)\n";
    else drive += "pl.replace('weapon', " + lua_quote(o.a[1]) + ", " + lua_quote("ruleset.renewal:" + o.a[1]) + ", function() end)\n";
  }
  drive += "print(table.concat(pl.order('weapon'), ','))\n";
  auto out = eval(*w.rm, "ruleset.prerenewal", drive);
  std::string joined;
  for (auto& s : real) joined += (joined.empty() ? "" : ",") + s;
  CHECK_EQ(out.at(0), joined);
  // a set of single edits, each compared on its own
  struct Case { std::string real, fake; };
  std::vector<Case> cases = {
      {"pipeline.cancel('weapon', 'element')", "pl.cancel('weapon', 'element')"},
      {"pipeline.insert_before('weapon', 'defense', 'm:x', function(c) end)", "pl.insert_before('weapon', 'defense', 'm:x', function() end)"},
      {"pipeline.insert_after('weapon', 'final', 'm:x', function(c) end)", "pl.insert_after('weapon', 'final', 'm:x', function() end)"},
      {"pipeline.move('weapon', 'card_attacker', { after = 'base_attack' })", "pl.move('weapon', 'card_attacker', { after = 'base_attack' })"},
      {"pipeline.move('weapon', 'init', { before = 'card_target' })", "pl.move('weapon', 'init', { before = 'card_target' })"},
      {"pipeline.replace('weapon', 'defense', 'm:d', function(c) end)", "pl.replace('weapon', 'defense', 'm:d', function() end)"},
  };
  for (auto& c : cases) {
    Loaded v;
    v.rm->set_environment(fx::env("ruleset.prerenewal"));
    fx::load(*v.rm, "ruleset.prerenewal", stand_in_ruleset(pre));
    ModEnvironmentSpec m = fx::env("m");
    m.granted.insert(Permission::ProtectedStages);
    v.rm->set_environment(m);
    fx::load(*v.rm, "m", c.real);
    std::string a;
    for (auto& s : v.rm->pipelines().order("weapon")) a += (a.empty() ? "" : ",") + s;
    std::string d2 = "local fake = (function()\n" + slurp_path(fake) + "\nend)()\nlocal engine = fake()\nlocal pl = engine.pipeline\npl.define('weapon', " + lua_list(pre) + ")\n" + c.fake +
                     "\nprint(table.concat(pl.order('weapon'), ','))\n";
    CHECK_EQ(eval(*v.rm, "ruleset.prerenewal", d2).at(0), a);
  }
  (void)re;
}

LUAU_TEST("BR-RULESET-115", "the fake host's pure-Luau f32 equals the native num.f32 bit for bit (normal, denormal, overflow, signed zero, nan)") {
  const fs::path fake = re_dir() / "tests" / "support" / "fake_host.luau";
  if (!fs::exists(fake)) throw mt::Skip("ruleset-renewal not found");
  ReloadManager rm;
  ModEnvironmentSpec e = big_env("m");
  rm.set_environment(e);
  fx::load(rm, "m", "assert(true)");
  std::string src = "local fake = (function()\n" + slurp_path(fake) + "\nend)()\n" + R"LUA(
    local buf = buffer.create(8)
    local state = 12345
    local function rnd32() state = (state * 1103515245 + 12345) % 2147483648; return state end
    local bad, n = 0, 0
    local function same(a, b)
      if a ~= a then return b ~= b end
      return a == b and (a ~= 0 or 1 / a == 1 / b)
    end
    local function check(x)
      n = n + 1
      if not same(fake.f32(x), num.f32(x)) then
        bad = bad + 1
        if bad <= 3 then print("MISMATCH", string.format("%.17g", x), string.format("%.17g", fake.f32(x)), string.format("%.17g", num.f32(x))) end
      end
    end
    for _, x in ipairs({ 0, -0.0, 1, -1, 0.1, 1.4, 16777217, 16777219, 3.4028235677973366e38, 3.4028234663852886e38, 3.4028235e38, -3.5e38,
                         1e-45, 7e-46, 2.3e-45, 1.1754943508222875e-38, 1.17549421e-38, 5.9e-39, 1e300, 1e-300, math.huge, -math.huge, 0 / 0 }) do check(x) end
    for i = 1, 40000 do
      for k = 0, 7 do buffer.writeu8(buf, k, rnd32() % 256) end
      check(buffer.readf64(buf, 0))
      -- magnitudes around the binary32 range: mantissa of a double scaled by 2^-160 .. 2^130
      local m = (rnd32() * 2147483648 + rnd32()) / 4611686018427387904
      check(math.ldexp(m, rnd32() % 291 - 160) * (i % 2 == 0 and 1 or -1))
    end
    print(bad, n)
  )LUA";
  auto out = eval(rm, "m", src);
  CHECK_EQ(out.back(), std::string("0\t80023"));
}

// ---- independence --------------------------------------------------------------------------------------------------------------
namespace {
std::string code_only(const std::string& text) {
  std::string out;
  std::stringstream ss(text);
  std::string line;
  while (std::getline(ss, line)) {
    auto c = line.find("--");
    // a "--" inside a string literal would be cut too; none of the sources has one before code on the same line
    out += (c == std::string::npos ? line : line.substr(0, c)) + "\n";
  }
  return out;
}
}  // namespace

TEST("BR-RULESET-118", "the two ruleset packages do not depend on each other: no code mentions the other package, every import stays inside the package") {
  if (!fs::is_directory(pre_dir() / "scripts") || !fs::is_directory(re_dir() / "scripts")) throw mt::Skip("ruleset packages not found");
  struct Side { fs::path dir; std::regex other; const char* other_name; };
  std::vector<Side> sides = {{pre_dir(), std::regex("ruleset[.-]renewal|ruleset_renewal"), "ruleset.renewal"},
                             {re_dir(), std::regex("prerenewal|pre-renewal package|ruleset[.-]pre"), "ruleset.prerenewal"}};
  for (auto& s : sides) {
    std::size_t files = 0;
    for (const char* sub : {"scripts", "data", "tests"}) {
      if (!fs::is_directory(s.dir / sub)) continue;
      for (auto it = fs::recursive_directory_iterator(s.dir / sub); it != fs::recursive_directory_iterator(); ++it) {
        if (!it->is_regular_file() || it->path().extension() != ".luau") continue;
        const std::string rel = fs::relative(it->path(), s.dir).generic_string();
        if (rel.rfind("build/", 0) == 0) continue;
        ++files;
        std::string code = code_only(slurp_path(it->path()));
        std::smatch m;
        CHECK_MSG(!std::regex_search(code, m, s.other), rel + " mentions " + s.other_name + " (" + m.str() + ")");
        // import("...") targets stay inside the package's own scripts/ data/ tests/ conformance/ build/
        std::regex imp("import\\(\"([^\"]+)\"\\)");
        for (auto i = std::sregex_iterator(code.begin(), code.end(), imp); i != std::sregex_iterator(); ++i) {
          std::string target = (*i)[1];
          bool own = target.rfind("scripts/", 0) == 0 || target.rfind("data/", 0) == 0 || target.rfind("tests/", 0) == 0 ||
                     target.rfind("conformance/", 0) == 0 || target.rfind("build/", 0) == 0;
          CHECK_MSG(own, rel + " imports " + target);
          CHECK_MSG(fs::exists(s.dir / (target + ".luau")) || target.rfind("build/", 0) == 0 || target.find("..") != std::string::npos ||
                        target.back() == '/' || target.find("\" ..") != std::string::npos,
                    rel + " imports a module that does not exist in this package: " + target);
        }
      }
    }
    CHECK(files > 20);
  }
}

LUAU_TEST("BR-RULESET-118", "the two packages cannot be loaded together: both define the same pipelines, the second load is refused naming both mods") {
  if (!fs::is_directory(pre_dir() / "scripts") || !fs::is_directory(re_dir() / "scripts")) throw mt::Skip("ruleset packages not found");
  Loaded w;
  w.rm->set_environment(big_env("ruleset.prerenewal"));
  auto a = w.rm->reload("ruleset.prerenewal", bundle(pre_dir(), {"scripts", "data"}, false));
  CHECK(a.ok);
  w.rm->set_environment(big_env("ruleset.renewal"));
  auto b = w.rm->reload("ruleset.renewal", bundle(re_dir(), {"scripts", "data"}, false));
  CHECK(!b.ok);
  CHECK_MSG(fx::contains(b.error, "' is defined twice: by mod 'ruleset.prerenewal' and by mod 'ruleset.renewal'"), b.error);
  CHECK_EQ(w.rm->active_generation("ruleset.renewal"), std::uint64_t(0));
  CHECK_EQ(w.rm->pipelines().owner_of("weapon"), std::string("ruleset.prerenewal"));
}
