// Host integration of the pipeline runtime: the manifest's [ruleset] table as the read-only `config` table, the
// protected-stage permission (manifest -> mods.lock -> script-host Permission::ProtectedStages) and the opt-in
// register(engine) call (`[entry] register = true`). Everything runs the real ruleset packages through the real
// loader, HostRuntime, ModSandbox and pipeline runtime. Tags MOD-INT-013...020. Tests that need the Luau VM report SKIP.
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>

#include "harness.hpp"
#include "mods/host_runtime.hpp"

namespace fs = std::filesystem;
using namespace mods;
using script_host::RunStatus;

namespace {

fs::path engine_root() { return MODS_ENGINE_ROOT; }
fs::path base_dir() { return fs::path(MODS_SOURCE_DIR) / "base" / "rathena.base"; }
fs::path pre_dir() { return engine_root() / "ruleset-prerenewal"; }
fs::path re_dir() { return engine_root() / "ruleset-renewal"; }

void need_vm() { if (!HostRuntime::vm_available()) SKIP_TEST("built without the Luau VM"); }

std::string slurp(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  if (!in) throw CheckFailure("cannot read " + p.string());
  std::ostringstream o;
  o << in.rdbuf();
  return o.str();
}
fs::path tmp(const std::string& name) {
  fs::path p = fs::temp_directory_path() / ("mods_ph_" + name);
  fs::remove_all(p);
  fs::create_directories(p);
  return p;
}
void write_file(const fs::path& p, const std::string& s) {
  fs::create_directories(p.parent_path());
  std::ofstream(p) << s;
}

struct Pkg { Manifest m; fs::path dir; };
Pkg pkg(const std::string& id, const std::string& extra_manifest, const std::string& script) {
  Pkg p;
  p.dir = tmp(id);
  p.m = parse_manifest("id = \"" + id + "\"\nversion = \"1.0.0\"\nengine_api = \"^1\"\n" + extra_manifest);
  write_file(p.dir / "scripts" / "init.luau", script);
  return p;
}
Pkg real(const fs::path& dir) { return Pkg{load_manifest_file(dir / "mod.toml"), dir}; }
// The real manifest with `key = <old>` rewritten (a temp copy; the package on disk is untouched).
Pkg real_with(const fs::path& dir, const std::string& from, const std::string& to) {
  std::string text = slurp(dir / "mod.toml");
  auto at = text.find(from);
  if (at == std::string::npos) throw CheckFailure("manifest of " + dir.string() + " has no '" + from + "'");
  text.replace(at, from.size(), to);
  return Pkg{parse_manifest(text, (dir / "mod.toml").string()), dir};
}
LoadRequest request(const std::vector<Pkg>& pkgs) {
  LoadRequest r;
  for (const auto& p : pkgs) { r.manifests.push_back(p.m); r.packages[p.m.id] = p.dir; }
  r.resolve.required_provides = {};
  return r;
}
void grant(LoadRequest& r, const std::string& mod, std::initializer_list<const char*> perms) {
  for (auto p : perms) r.lock.entries[mod].granted.insert(*parse_permission(p));
}

IdMap new_ids() { return IdMap({{"skill", {100000, 109999}}, {"status", {100000, 109999}}, {"item", {500000, 599999}}}); }

// One loaded world: ids + runtime + loader.
struct World {
  IdMap ids = new_ids();
  HostRuntime rt;
  ModLoader loader{ids};
  World() { loader.set_activator(&rt); }
};

// Runs Luau in a mod's live sandbox and returns the lines it print()ed.
std::vector<std::string> eval(HostRuntime& rt, const std::string& mod, const std::string& src) {
  auto sb = rt.sandbox(mod);
  if (!sb) throw CheckFailure("mod not loaded: " + mod);
  std::vector<std::string> lines;
  sb->set_host({{}, [&](const std::string& l) { lines.push_back(l); }});
  auto r = sb->run("eval", src);
  sb->set_host({});
  if (r.status != RunStatus::Ok) throw CheckFailure("eval failed: " + r.error);
  return lines;
}

// Reloads a package's own sandbox with its modules plus tests/support, and leaves SH_IMPORT(name) -> module defined, so the
// test can call the package's modules (weapon.calc_attack -> pipeline.run) inside the package's own state. The environment
// (grants, `config` from the manifest) is the one the loader built.
void expose_modules(World& w, const Pkg& p) {
  BundleOptions bo;
  bo.module_dirs = {"scripts", "data", "tests/support"};
  std::string chunk = bundle_package(p.dir, bo) + "\nSH_IMPORT = function(name) return __load(name) end\n";
  auto r = w.rt.reload_mod(p.m.id, chunk);
  if (!r.ok) throw CheckFailure("reload of " + p.m.id + " failed: " + r.error);
}

// Real ruleset (+ rathena.base) loaded through the loader.
std::unique_ptr<World> load_ruleset(const Pkg& ruleset) {
  auto w = std::make_unique<World>();
  w->loader.load(request({real(base_dir()), ruleset}));
  return w;
}

// Renewal golden case 2520 (conformance/golden/weapon_re.jsonl.gz, generated from the real server code): a monster crit.
// legacy: 1009 -> DEF 766 -> x1.4f 1072 (the recorded value). Corrected (one x1.4f): 740. The stage probes are left out.
const char* kCase2520 = R"LUA({a={agi=95,batk=86,cri=1000,def=36,def2=35,dex=20,ele_lv=2,element=2,flee=169,hit=500,int=24,kind="mob",level=65,luk=47,race=8,res=0,rhw={atk=369,atk2=635,ele=4},size=0,str=43,vit=113},cat="crit_mob",cfg={},id=2520,map="normal",out={damage=1072,damage2=0,div=1,dmg_lv=5,draws={940931460},flag=529,type=10},seed=1009553231,t={agi=115,batk=156,crate=50,cri=100,def=97,def2=65,dex=25,eatk=0,ele_lv=2,element=1,flee=0,hit=228,int=39,kind="pc",level=4,luk=2,mres=0,patk=30,race=11,res=0,rhw={atk=533,atk2=40,ele=1,wlv=2},size=1,str=2,vit=8,wtype1=12}})LUA";

// docs/pipeline_diff.json (pretty printed): "api": {"ops": [["cancel", "x"], ["insert_after", "a", "b"], ["replace", "y"], ...]}
// and "order": {"re": [...]}.
std::vector<std::vector<std::string>> doc_ops() {
  std::string j = slurp(re_dir() / "docs" / "pipeline_diff.json");
  auto api = j.find("\"api\"");
  auto ops = j.find("\"ops\"", api);
  if (api == std::string::npos || ops == std::string::npos) throw CheckFailure("pipeline_diff.json has no api.ops");
  std::vector<std::vector<std::string>> out;
  std::vector<std::string> cur;
  int depth = 0;
  for (std::size_t i = j.find('[', ops); i < j.size(); ++i) {
    char c = j[i];
    if (c == '[') { if (++depth == 2) cur.clear(); }
    else if (c == ']') { if (depth == 2) out.push_back(cur); if (--depth == 0) break; }
    else if (c == '"' && depth == 2) {
      std::size_t e = j.find('"', i + 1);
      cur.push_back(j.substr(i + 1, e - i - 1));
      i = e;
    }
  }
  return out;
}
std::vector<std::string> doc_re_order() {
  std::string j = slurp(re_dir() / "docs" / "pipeline_diff.json");
  auto ord = j.find("\"order\"");
  auto re = j.find("\"re\"", ord);
  if (ord == std::string::npos || re == std::string::npos) throw CheckFailure("pipeline_diff.json has no order.re");
  std::vector<std::string> out;
  for (std::size_t i = j.find('[', re); i < j.size(); ++i) {
    if (j[i] == ']') break;
    if (j[i] == '"') {
      std::size_t e = j.find('"', i + 1);
      out.push_back(j.substr(i + 1, e - i - 1));
      i = e;
    }
  }
  return out;
}

std::string q(const std::string& s) { return "\"" + s + "\""; }

// The 23 operations as a mod body. `me` is the editing mod: inserted/replacing ids are `<me>:<name>`.
std::string delta_script(const std::vector<std::vector<std::string>>& ops, const std::string& me) {
  auto ns = [&](std::string s) {
    const std::string from = "ruleset.renewal:";
    if (s.compare(0, from.size(), from) == 0) s = me + ":" + s.substr(from.size());
    return s;
  };
  std::string s;
  for (const auto& o : ops) {
    const std::string& k = o.at(0);
    if (k == "cancel") s += "pipeline.cancel('weapon', " + q(o.at(1)) + ")\n";
    else if (k == "insert_after") s += "pipeline.insert_after('weapon', " + q(ns(o.at(1))) + ", " + q(ns(o.at(2))) + ", function(ctx) end, { mode = 'modify' })\n";
    else if (k == "replace") s += "pipeline.replace('weapon', " + q(o.at(1)) + ", " + q(me + ":" + o.at(1)) + ", function(ctx) end)\n";
    else throw CheckFailure("unknown op " + k);
  }
  return s;
}

Pkg editor(const std::string& id, const std::string& permissions, const std::string& script, const std::string& ruleset = "ruleset.prerenewal") {
  return pkg(id, "depends = { \"" + ruleset + "\" = \"^0.1\" }\npermissions = [" + permissions + "]\n", script);
}

}  // namespace

// ---- 1. the manifest's [ruleset] table is the read-only `config` table -------------------------------------------------------
TEST("BR-RULESET-118", manifest_ruleset_table_is_the_read_only_config_table_of_the_package) {
  need_vm();
  const char* show = "print(config.legacy_quirks, config.strict_parity, config.mode, config.db_set, config.renewal, "
                     "config.subflag_cast, config.subflag_aspd, config.subflag_edp)";
  auto re = load_ruleset(real(re_dir()));
  CHECK(eval(re->rt, "ruleset.renewal", show)[0] == "true\ttrue\trenewal\tdb/re\ttrue\ttrue\ttrue\ttrue");
  auto pre = load_ruleset(real(pre_dir()));
  CHECK(eval(pre->rt, "ruleset.prerenewal", show)[0] == "true\ttrue\tpre-renewal\tdb/pre-re\tfalse\tfalse\tfalse\tfalse");
  // read-only: a script cannot change what the manifest said
  CHECK(eval(pre->rt, "ruleset.prerenewal", "print(pcall(function() config.legacy_quirks = false end))")[0].rfind("false", 0) == 0);
  CHECK(eval(pre->rt, "ruleset.prerenewal", "print(config.legacy_quirks)")[0] == "true");
  // each key follows the manifest: a temp copy with the values flipped
  auto flipped = load_ruleset(real_with(re_dir(), "strict_parity = true", "strict_parity = false"));
  CHECK(eval(flipped->rt, "ruleset.renewal", "print(config.strict_parity, config.legacy_quirks)")[0] == "false\ttrue");
  auto off = load_ruleset(real_with(re_dir(), "legacy_quirks = true", "legacy_quirks = false"));
  CHECK(eval(off->rt, "ruleset.renewal", "print(config.legacy_quirks, config.strict_parity)")[0] == "false\ttrue");
  // a mod without a [ruleset] table keeps script-host's default (legacy_quirks on) and sees no other key
  World w;
  w.loader.load(request({pkg("plain.mod", "", "return nil\n")}));
  CHECK(eval(w.rt, "plain.mod", "print(config.legacy_quirks, config.mode, config.strict_parity)")[0] == "true\tnil\tnil");
}

// ---- 2. legacy_quirks reaches the package: golden behaviour with true, corrected with false -----------------------------------
TEST("BR-RULESET-118", renewal_golden_case_2520_through_the_real_pipeline_is_1072_with_legacy_quirks_and_740_without) {
  need_vm();
  const std::string src = std::string(R"LUA(
    local weapon = SH_IMPORT("scripts/damage/weapon")
    local unit = SH_IMPORT("scripts/damage/unit")
    local defaults = SH_IMPORT("data/damage_cfg")
    local fake_ctx = SH_IMPORT("tests/support/fake_ctx")
    local scripted = SH_IMPORT("tests/support/scripted_rng").scripted
    local c = )LUA") + kCase2520 + R"LUA(
    local cfg = {}
    for k, v in pairs(defaults) do cfg[k] = v end  -- the package default: engine.config.legacy_quirks
    local rng = scripted(c.out.draws)
    local res = weapon.calc_attack(fake_ctx.new(rng), cfg, unit.normalize(c.a), unit.normalize(c.t), { map = c.map })
    print(config.legacy_quirks, defaults.legacy_quirks, res.damage, c.out.damage, res.type, rng.used == rng.total, #pipeline.order("weapon"))
  )LUA";
  Pkg legacy = real(re_dir());
  auto a = load_ruleset(legacy);
  expose_modules(*a, legacy);
  CHECK(eval(a->rt, "ruleset.renewal", src)[0] == "true\ttrue\t1072\t1072\t10\ttrue\t24");
  // a temp copy of the real manifest with legacy_quirks = false: the corrected behaviour (one x1.4f), same RNG draws
  Pkg fixed = real_with(re_dir(), "legacy_quirks = true", "legacy_quirks = false");
  auto b = load_ruleset(fixed);
  expose_modules(*b, fixed);
  CHECK(eval(b->rt, "ruleset.renewal", src)[0] == "false\tfalse\t740\t1072\t10\ttrue\t24");
}

TEST("BR-RULESET-118", prerenewal_rifle_weapon_damage_rate_is_legacy_120_with_legacy_quirks_and_90_without) {
  need_vm();
  const char* src = R"LUA(
    local weapon = SH_IMPORT("scripts/damage/weapon")
    local unit = SH_IMPORT("scripts/damage/unit")
    local defaults = SH_IMPORT("data/damage_cfg")
    local fake_ctx = SH_IMPORT("tests/support/fake_ctx")
    local scripted = SH_IMPORT("tests/support/scripted_rng").scripted
    local src = unit.normalize({ kind = "pc", wtype1 = 18, level = 1, no_sizefix = true, rhw = { atk = 100, atk2 = 0, ele = 0, wlv = 1 },
                                 bonus = { aadd_ele = { [2] = 20 }, wdr_r = -10 } })
    print(config.legacy_quirks, defaults.legacy_quirks, weapon.base_damage(fake_ctx.new(scripted({})), src, "r", 1, true, false, true, { cfg = defaults }), #pipeline.order("weapon"))
  )LUA";
  Pkg legacy = real(pre_dir());
  auto a = load_ruleset(legacy);
  expose_modules(*a, legacy);
  CHECK(eval(a->rt, "ruleset.prerenewal", src)[0] == "true\ttrue\t120\t15");
  Pkg fixed = real_with(pre_dir(), "legacy_quirks = true", "legacy_quirks = false");
  auto b = load_ruleset(fixed);
  expose_modules(*b, fixed);
  CHECK(eval(b->rt, "ruleset.prerenewal", src)[0] == "false\tfalse\t90\t15");
}

// ---- 3. protected stages: the permission through manifest, mods.lock and the sandbox -------------------------------------------
TEST("MOD-INT-015", to_host_permission_maps_protected_stages) {
  CHECK(to_host_permission(PermissionKind::ProtectedStages) == script_host::Permission::ProtectedStages);
  CHECK(to_host_permission(parse_permission("pipeline.protected_stages")->kind) == script_host::Permission::ProtectedStages);
}

TEST("MOD-INT-016", cancelling_or_replacing_a_protected_stage_is_refused_without_the_grant_and_allowed_with_it) {
  need_vm();
  const std::string cancel_final = "pipeline.cancel('weapon', 'final')\n";
  const std::string replace_init = "pipeline.replace('weapon', 'init', 'edit.mod:init', function(ctx) end)\n";
  const std::string move_final = "pipeline.move('weapon', 'final', { before = 'map_rates' })\n";

  // generation 1: the real pre-renewal ruleset alone
  World w;
  w.loader.load(request({real(base_dir()), real(pre_dir())}));
  CHECK(w.loader.generation() == 1);
  auto active = [&](const std::string& stage) {
    for (auto& s : w.rt.reload_manager().pipelines().describe("weapon").steps) if (s.id == stage) return s.active;
    throw CheckFailure("no stage " + stage);
  };
  CHECK(active("final") && active("init"));

  // (a) the editor asks for nothing: script-host refuses each edit, naming the permission; the old generation stays
  for (const std::string& body : {cancel_final, replace_init, move_final}) {
    auto r = request({real(base_dir()), real(pre_dir()), editor("edit.mod", "", body)});
    try { w.loader.load(r); CHECK(false); }
    catch (const LoadError& e) {
      CHECK(e.code() == "activate.failed");
      std::string m = e.what();
      CHECK(m.find("edit.mod") != std::string::npos && m.find("protected stage weapon/") != std::string::npos &&
            m.find("without the protected-stage permission") != std::string::npos);
    }
    CHECK(w.loader.generation() == 1 && active("final") && active("init"));
  }
  // an insert next to a protected stage needs nothing
  {
    auto r = request({real(base_dir()), real(pre_dir()), editor("edit.mod", "", "pipeline.insert_after('weapon', 'init', 'edit.mod:probe', function(ctx) end)\n")});
    w.loader.load(r);
    CHECK(w.loader.generation() == 2 && w.rt.granted_of("edit.mod").empty());
  }

  // (b) requested but not in mods.lock: the generation is blocked before any code runs (and the old one stays)
  {
    auto r = request({real(base_dir()), real(pre_dir()), editor("edit.mod", "\"pipeline.protected_stages\"", "EDIT_RAN = true\n" + cancel_final)});
    try { w.loader.load(r); CHECK(false); }
    catch (const LoadError& e) {
      CHECK(e.code() == "permission.ungranted");
      std::string m = e.what();
      CHECK(m.find("edit.mod") != std::string::npos && m.find("pipeline.protected_stages") != std::string::npos);
    }
    CHECK(w.loader.generation() == 2 && active("final"));
  }

  // (c) granted in mods.lock but not requested by the manifest: the grant is dropped (least privilege), the edit is refused
  {
    auto r = request({real(base_dir()), real(pre_dir()), editor("edit.mod", "", cancel_final)});
    grant(r, "edit.mod", {"pipeline.protected_stages"});
    try { w.loader.load(r); CHECK(false); }
    catch (const LoadError& e) { CHECK(e.code() == "activate.failed" && std::string(e.what()).find("protected-stage permission") != std::string::npos); }
    CHECK(w.loader.generation() == 2 && active("final"));
  }

  // (d) requested and granted: cancel, replace and move all go through, each recorded with its source mod
  {
    auto r = request({real(base_dir()), real(pre_dir()), editor("edit.mod", "\"pipeline.protected_stages\"", cancel_final + replace_init)});
    grant(r, "edit.mod", {"pipeline.protected_stages"});
    w.loader.load(r);
    CHECK(w.loader.generation() == 3);
    CHECK(w.rt.granted_of("edit.mod") == (std::set<script_host::Permission>{script_host::Permission::ProtectedStages}));
    CHECK(!active("final"));                                        // cancelled: kept in the list, inactive
    std::string init_body;
    for (auto& s : w.rt.reload_manager().pipelines().describe("weapon").steps)
      if (s.id == "init") init_body = s.body_mod;
    CHECK(init_body == "edit.mod");                                  // replaced
    // the grant is the mod's own: the ruleset itself never got it
    CHECK(w.rt.granted_of("ruleset.prerenewal").empty());
  }
  {
    World m;
    auto r = request({real(base_dir()), real(pre_dir()), editor("edit.mod", "\"pipeline.protected_stages\"", move_final)});
    grant(r, "edit.mod", {"pipeline.protected_stages"});
    m.loader.load(r);
    auto order = m.rt.reload_manager().pipelines().order("weapon");
    CHECK(order.size() == 15 && order[order.size() - 2] == "final");
  }
}

TEST("MOD-INT-017", the_23_operation_pre_to_re_edit_set_needs_the_protected_stages_grant) {
  need_vm();
  auto ops = doc_ops();
  CHECK(ops.size() == 23);
  std::map<std::string, int> counts;
  for (auto& o : ops) ++counts[o[0]];
  CHECK(counts["cancel"] == 3 && counts["insert_after"] == 12 && counts["replace"] == 8);
  auto re_order = doc_re_order();
  CHECK(re_order.size() == 24);
  const std::string body = delta_script(ops, "delta.renewal");

  // a delta mod that does not request the permission: replace('init') is refused
  {
    World w;
    auto r = request({real(base_dir()), real(pre_dir()), editor("delta.renewal", "", body)});
    try { w.loader.load(r); CHECK(false); }
    catch (const LoadError& e) {
      CHECK(e.code() == "activate.failed");
      CHECK(std::string(e.what()).find("cannot replace protected stage weapon/init without the protected-stage permission") != std::string::npos);
    }
    CHECK(!w.rt.live() && w.loader.generation() == 0);
  }
  // requested, not granted by the operator: blocked as a whole
  {
    World w;
    auto r = request({real(base_dir()), real(pre_dir()), editor("delta.renewal", "\"pipeline.protected_stages\"", body)});
    try { w.loader.load(r); CHECK(false); }
    catch (const LoadError& e) { CHECK(e.code() == "permission.ungranted"); }
    CHECK(!w.rt.live());
  }
  // requested and granted: the edits turn the real PRE order into the RE order, every edit attributed to the delta mod
  World w;
  auto r = request({real(base_dir()), real(pre_dir()), editor("delta.renewal", "\"pipeline.protected_stages\"", body)});
  grant(r, "delta.renewal", {"pipeline.protected_stages"});
  w.loader.load(r);
  CHECK(w.loader.generation() == 1);
  auto info = w.rt.reload_manager().pipelines().describe("weapon");
  std::vector<std::string> got;
  const std::string prefix = "delta.renewal:";
  for (auto& s : info.steps)
    if (s.active) got.push_back(s.id.rfind(prefix, 0) == 0 ? s.id.substr(prefix.size()) : s.id);
  CHECK(got == re_order);
  std::size_t edits = 0;
  for (auto& s : info.steps)
    for (auto& e : s.edits) { CHECK(e.mod == "delta.renewal"); ++edits; }
  CHECK(edits == 23);
  // the PRE ruleset's own defined order is untouched
  CHECK(info.owner_mod == "ruleset.prerenewal");
}

// ---- 4. register(engine) is opt-in per mod ----------------------------------------------------------------------------------
TEST("MOD-INT-018", register_engine_is_called_only_for_a_mod_that_opts_in_and_a_registering_and_a_plain_mod_load_together) {
  need_vm();
  // opts in: [entry] register = true; its entry module takes the engine table
  Pkg reg = pkg("reg.mod", "[entry]\nmodules = [\"scripts/init.luau\"]\nregister = true\n", R"LUA(
local M = {}
function M.register(engine)
  assert(type(engine) == "table" and type(engine.pipeline) == "table" and type(engine.num) == "table", "no engine table")
  assert(engine.config.legacy_quirks == true)
  REGISTERED = (REGISTERED or 0) + 1
end
return M
)LUA");
  // does not opt in; its module has a register(name, handler) with another meaning, exactly like rathena.base's bonus.register
  Pkg plain = pkg("plain.mod", "", R"LUA(
local M = {}
function M.register(name, handler)
  assert(type(name) == "string" and type(handler) == "function", "host called register with an engine table")
  HOST_CALLED_REGISTER = true
end
return M
)LUA");
  write_file(plain.dir / "scripts" / "second.luau", "local M = {}\nfunction M.register(name, handler) error('second.register called by the host') end\nreturn M\n");
  // not listing entry modules: every module is an entry (scripts/init and scripts/second) and still nothing is registered
  World w;
  LoadRequest r = request({real(base_dir()), reg, plain});
  w.loader.load(r);   // the real rathena.base loads in the same generation
  CHECK(w.loader.state("reg.mod") == ModState::Active && w.loader.state("plain.mod") == ModState::Active && w.loader.state("rathena.base") == ModState::Active);
  eval(w.rt, "reg.mod", "assert(REGISTERED == 1, 'register ran ' .. tostring(REGISTERED)); assert(type(engine) == 'table' and engine.config == config)");
  eval(w.rt, "plain.mod", "assert(engine == nil and HOST_CALLED_REGISTER == nil)");
  eval(w.rt, "rathena.base", "assert(engine == nil)");
  // the bundle text: the opted-in chunk has the call, the others do not
  CHECK(w.rt.chunk_of("reg.mod").find("m.register(engine)") != std::string::npos);
  CHECK(w.rt.chunk_of("plain.mod").find("m.register(engine)") == std::string::npos);
  CHECK(w.rt.chunk_of("rathena.base").find("m.register(engine)") == std::string::npos);
  // base's own bonus.register(name, handler) still works afterwards (a table name would have been accepted by the old call, silently)
  auto lines = eval(w.rt, "rathena.base", "print(engine == nil, config.mode)");
  CHECK(lines[0] == "true\tnil");
}

TEST("MOD-INT-019", a_package_that_wants_the_engine_global_gets_it_and_the_ruleset_packages_keep_registering_themselves) {
  need_vm();
  // a registering mod beside a real ruleset: the ruleset (no [entry]) registers itself from its own module, the editor
  // uses the engine table handed to it
  Pkg reg = editor("reg.mod", "", R"LUA(
local M = {}
function M.register(engine)
  engine.pipeline.insert_after('weapon', 'init', 'reg.mod:probe', function(ctx) end)
  SAW = engine.pipeline == pipeline and engine.num == num
end
return M
)LUA");
  reg.m = parse_manifest("id = \"reg.mod\"\nversion = \"1.0.0\"\nengine_api = \"^1\"\ndepends = { \"ruleset.renewal\" = \"^0.1\" }\n[entry]\nregister = true\n");
  World w;
  w.loader.load(request({real(base_dir()), real(re_dir()), reg}));
  eval(w.rt, "reg.mod", "assert(SAW == true)");
  auto order = w.rt.reload_manager().pipelines().order("weapon");
  CHECK(order.size() == 25 && order[1] == "reg.mod:probe");          // 24 renewal stages + the probe after init
  CHECK(eval(w.rt, "ruleset.renewal", "print(engine)")[0] == "nil");  // the ruleset did not ask for the global
}

TEST("MOD-INT-020", register_opt_in_only_matters_with_the_manifest_flag_not_with_an_engine_global_alone) {
  need_vm();
  // the bundler: an entry that returns { register } is called only when BundleOptions::register_entries is set
  Pkg p = pkg("flag.mod", "", "local M = {}\nfunction M.register(e) CALLED = type(e) end\nreturn M\n");
  const std::string off = bundle_package(p.dir);
  BundleOptions on_opt;
  on_opt.register_entries = true;
  const std::string on = bundle_package(p.dir, on_opt);
  CHECK(off.find("m.register(engine)") == std::string::npos);
  CHECK(on.find("m.register(engine)") != std::string::npos);
  // a host config override replaces the manifest's choice (module list and flag together), and the engine global follows the flag
  HostConfig cfg;
  cfg.bundle_for["flag.mod"].register_entries = true;
  HostRuntime rt(cfg);
  IdMap ids = new_ids();
  ModLoader loader(ids);
  loader.set_activator(&rt);
  loader.load(request({p}));
  eval(rt, "flag.mod", "assert(CALLED == 'table')");
}
