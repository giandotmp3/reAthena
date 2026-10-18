// Loader integration with script-host (ReloadManager / ModSandbox): the real
// mods of this repository go through the real resolver, grants become sandbox
// permissions, the kill switch and the observability counters work.
// Tags MOD-INT-*. Tests that need the Luau VM report SKIP without it.
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>

#include "harness.hpp"
#include "mods/host_runtime.hpp"

namespace fs = std::filesystem;
using namespace mods;

namespace {

fs::path engine_root() { return MODS_ENGINE_ROOT; }
fs::path base_dir() { return fs::path(MODS_SOURCE_DIR) / "base" / "rathena.base"; }
fs::path pre_dir() { return engine_root() / "ruleset-prerenewal"; }
fs::path re_dir() { return engine_root() / "ruleset-renewal"; }
fs::path npc_dir() { return engine_root() / "legacy-npc"; }

Manifest m_of(const fs::path& dir) { return load_manifest_file(dir / "mod.toml"); }

// The shipped ruleset-prerenewal manifest says engine_api ">=0.1, <1" while
// legacy-npc, ruleset-renewal and rathena.base say "^1": no single engine
// version admits all of them (reported in the README). Tests that need them
// together use this explicitly repaired copy.
Manifest pre_repaired() {
  Manifest m = m_of(pre_dir());
  m.engine_api = "^1";
  return m;
}

fs::path tmp(const std::string& name) {
  fs::path p = fs::temp_directory_path() / ("mods_int_" + name);
  fs::remove_all(p);
  fs::create_directories(p);
  return p;
}
void write_file(const fs::path& p, const std::string& s) {
  fs::create_directories(p.parent_path());
  std::ofstream(p) << s;
}
// A synthetic package: manifest text + scripts/init.luau.
struct Pkg { Manifest m; fs::path dir; };
Pkg pkg(const std::string& id, const std::string& extra_manifest, const std::string& script) {
  fs::path d = tmp(id);
  Pkg p;
  p.m = parse_manifest("id = \"" + id + "\"\nversion = \"1.0.0\"\nengine_api = \"^1\"\n" + extra_manifest);
  write_file(d / "scripts" / "init.luau", script);
  p.dir = d;
  return p;
}
LoadRequest request(const std::vector<Pkg>& pkgs) {
  LoadRequest r;
  for (const auto& p : pkgs) { r.manifests.push_back(p.m); r.packages[p.m.id] = p.dir; }
  return r;
}
void need_vm() { if (!HostRuntime::vm_available()) SKIP_TEST("built without the Luau VM"); }
IdMap new_ids() { return IdMap({{"skill", {100000, 109999}}, {"status", {100000, 109999}}, {"item", {500000, 599999}}}); }

HostConfig real_config() {
  HostConfig c;
  c.memory_cap["legacy.npc"] = 256ull << 20;
  c.bundle_for["legacy.npc"].module_dirs = {"luau"};
  c.bundle_for["legacy.npc"].entry = {"luau/legacy"};
  return c;
}
LoadRequest real_request(bool with_npc = true) {
  LoadRequest r;
  r.manifests = {m_of(base_dir()), pre_repaired()};
  r.packages = {{"rathena.base", base_dir()}, {"ruleset.prerenewal", pre_dir()}};
  if (with_npc) { r.manifests.push_back(m_of(npc_dir())); r.packages["legacy.npc"] = npc_dir(); }
  r.resolve.required_provides = {"ruleset"};
  return r;
}

std::string order_str(const std::vector<Manifest>& v) {
  std::string s;
  for (auto& m : v) s += (s.empty() ? "" : ",") + m.id;
  return s;
}

}  // namespace

TEST("MOD-INT-001", real_manifests_resolve_together_and_a_too_old_engine_is_refused_precisely) {
  std::vector<Manifest> all = {m_of(npc_dir()), m_of(base_dir()), m_of(pre_dir())};
  ResolveOptions ro;
  ro.required_provides = {"ruleset"};
  // all shipped manifests now declare the same engine_api and resolve together (the old ">=0.1, <1" of ruleset.prerenewal was fixed)
  CHECK(order_str(resolve_load_order(all, ro)) == "legacy.npc,rathena.base,ruleset.prerenewal");
  ro.engine_api_version = {0, 5, 0};  // an engine older than the declared "^1" refuses, naming the mod
  try { resolve_load_order(all, ro); CHECK(false); }
  catch (const LoadError& e) { CHECK(e.code() == "resolve.engine_api" && std::string(e.what()).find("'legacy.npc'") != std::string::npos); }
}

TEST("MOD-INT-002", repaired_real_set_resolves_in_dependency_order_and_rulesets_exclude_each_other) {
  ResolveOptions ro;
  ro.required_provides = {"ruleset"};
  auto order = resolve_load_order({m_of(npc_dir()), pre_repaired(), m_of(base_dir())}, ro);
  CHECK(order_str(order) == "legacy.npc,rathena.base,ruleset.prerenewal");
  // renewal has "^1" and no [ruleset] table; same shape
  CHECK(order_str(resolve_load_order({m_of(base_dir()), m_of(re_dir())}, ro)) == "rathena.base,ruleset.renewal");
  try { resolve_load_order({m_of(base_dir()), m_of(re_dir()), pre_repaired()}, ro); CHECK(false); }
  catch (const LoadError& e) {
    CHECK(e.code() == "resolve.conflict");
    std::string w = e.what();
    CHECK(w.find("ruleset.prerenewal") != std::string::npos && w.find("ruleset.renewal") != std::string::npos);
  }
  // without a ruleset the required capability is missing
  try { resolve_load_order({m_of(base_dir()), m_of(npc_dir())}, ro); CHECK(false); }
  catch (const LoadError& e) { CHECK(e.code() == "resolve.missing_provider"); }
  // the real base is "0.1.0": a "^1" dependency on it (the architecture's wording) would not be satisfied
  Manifest strict = pre_repaired();
  strict.depends["rathena.base"] = "^1";
  try { resolve_load_order({m_of(base_dir()), strict}, ro); CHECK(false); }
  catch (const LoadError& e) { CHECK(e.code() == "resolve.version_mismatch"); }
}

TEST("MOD-INT-003", real_mods_load_through_loader_resolver_and_script_host) {
  need_vm();
  auto ids = new_ids();
  HostRuntime rt(real_config());
  ModLoader loader(ids);
  loader.set_activator(&rt);
  auto t0 = std::chrono::steady_clock::now();
  loader.load(real_request());
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
  CHECK(loader.generation() == 1);
  CHECK(loader.load_order() == (std::vector<std::string>{"legacy.npc", "rathena.base", "ruleset.prerenewal"}));
  for (const char* id : {"legacy.npc", "rathena.base", "ruleset.prerenewal"}) {
    CHECK(loader.state(id) == ModState::Active);
    CHECK(rt.sandbox(id) != nullptr);
    auto st = loader.stats(id);
    CHECK(st.memory_used > 0 && st.memory_peak >= st.memory_used && st.loaded_in_generation == 1);
    std::cout << "      " << id << ": load " << st.load_time.count() << " us, memory " << st.memory_used << " B (peak " << st.memory_peak << ")\n";
  }
  std::cout << "      whole generation: " << ms << " ms\n";
  CHECK(rt.chunk_of("rathena.base").find("__defs[\"scripts/bonus\"]") != std::string::npos);
  CHECK(rt.chunk_of("legacy.npc").size() > 100000);
  // each mod has its own state: a global set in one is invisible in another
  CHECK(rt.run("rathena.base", "t", "MARK = 1").status == script_host::RunStatus::Ok);
  CHECK(rt.run("legacy.npc", "t", "assert(MARK == nil)").status == script_host::RunStatus::Ok);
}

TEST("MOD-INT-004", bundled_base_and_prerenewal_modules_actually_execute_under_the_sandbox) {
  need_vm();
  // base: call the mechanism modules the chunk defines
  {
    script_host::ModEnvironmentSpec spec;
    spec.mod_id = "rathena.base";
    spec.budget.wall_us_per_dispatch = 0; spec.budget.wall_us_per_tick = 0;
    script_host::ModSandbox sb(spec);
    std::string chunk = bundle_package(base_dir()) + R"(
local T, B = __load("scripts/tables"), __load("scripts/bonus")
local idx = T.index_by({{k = "a", v = 1}, {k = "b", v = 2}}, "k")
assert(idx.b.v == 2 and table.isfrozen(idx))
B.register("x", function() return 1 end)
assert(B.lookup("x")() == 1 and B.lookup("y") == nil)
assert(not pcall(B.register, "x", function() end))
)";
    auto r = sb.run("base", chunk);
    if (r.status != script_host::RunStatus::Ok) throw CheckFailure(r.error);
  }
  // prerenewal: register(engine) with a recording engine validates every starter definition
  {
    script_host::ModEnvironmentSpec spec;
    spec.mod_id = "ruleset.prerenewal";
    spec.memory_cap_bytes = 64ull << 20;
    spec.budget.steps_per_dispatch = 50'000'000;
    spec.budget.wall_us_per_dispatch = 0; spec.budget.wall_us_per_tick = 0;
    script_host::ModSandbox sb(spec);
    std::string chunk = bundle_package(pre_dir()) + R"(
COUNTS = {}
local function sink(kind)
  return setmetatable({}, {
    __index = function(_, name)
      if name == "define" then return function(id, def) COUNTS[kind] = (COUNTS[kind] or 0) + 1 end end
      return function(...) COUNTS[kind .. "." .. name] = (COUNTS[kind .. "." .. name] or 0) + 1 end
    end })
end
local fake = setmetatable({}, {__index = function(t, k) local s = sink(k); rawset(t, k, s); return s end})
__load("scripts/init").register(fake)
assert(COUNTS.skill == 6 and COUNTS.item == 5, "skills=" .. tostring(COUNTS.skill) .. " items=" .. tostring(COUNTS.item))
assert((COUNTS.status or 0) >= 2)
)";
    auto r = sb.run("pre", chunk);
    if (r.status != script_host::RunStatus::Ok) throw CheckFailure(r.error);
  }
}

TEST("MOD-INT-005", granted_permissions_are_enforced_at_runtime_and_ungranted_apis_are_refused_precisely) {
  need_vm();
  // a mod that asks for storage.account and one other cross-read, and one that asks for nothing
  auto asks = pkg("asks.mod", "permissions = [\"storage.account\", \"storage.read:other.mod\", \"unbounded_loop\"]\n", "return nil\n");
  auto none = pkg("none.mod", "", "return nil\n");
  auto other = pkg("other.mod", "", "return nil\n");
  auto third = pkg("third.mod", "", "return nil\n");
  auto greedy = pkg("greedy.mod", "permissions = [\"storage.account\"]\n", "return nil\n");
  auto r = request({asks, none, other, third, greedy});
  auto grant = [&](const char* mod, std::initializer_list<const char*> ps) { for (auto p : ps) r.lock.entries[mod].granted.insert(*parse_permission(p)); };
  grant("asks.mod", {"storage.account", "storage.read:other.mod", "unbounded_loop"});
  grant("greedy.mod", {"storage.account", "unbounded_loop"});       // unbounded_loop granted but never requested: ignored
  grant("none.mod", {"storage.account", "net.raw", "atcommand"});    // granted but never requested: ignored
  auto ids = new_ids();
  HostConfig cfg;
  for (const char* m : {"asks.mod", "greedy.mod"}) { cfg.budget_for[m] = cfg.budget; cfg.budget_for[m].steps_per_dispatch = 100000; }
  HostRuntime rt(cfg);
  ModLoader loader(ids);
  loader.set_activator(&rt);
  loader.load(r);
  using script_host::RunStatus;
  using script_host::Permission;

  CHECK(rt.granted_of("asks.mod") == (std::set<Permission>{Permission::StorageAccount, Permission::StorageReadOther, Permission::UnboundedLoop}));
  CHECK(rt.granted_of("none.mod").empty());                     // least privilege: grants without a request are dropped
  CHECK(rt.granted_of("greedy.mod") == (std::set<Permission>{Permission::StorageAccount}));   // the unrequested unbounded_loop grant is dropped

  // storage.account
  CHECK(rt.run("asks.mod", "a", "storage.account(1):set('k', 7)").status == RunStatus::Ok);
  auto denied = rt.run("none.mod", "a", "storage.account(1)");
  CHECK(denied.status == RunStatus::PermissionDenied);
  CHECK(denied.error.find("[none.mod]") != std::string::npos && denied.error.find("storage.account permission") != std::string::npos);
  // storage.read:<mod> is per target mod
  CHECK(rt.run("asks.mod", "b", "assert(storage.world():mod('other.mod'):get('k') == nil)").status == RunStatus::Ok);
  auto wrong = rt.run("asks.mod", "c", "storage.world():mod('third.mod'):get('k')");
  CHECK(wrong.status != RunStatus::Ok && wrong.error.find("storage.read:third.mod") != std::string::npos);
  auto noread = rt.run("none.mod", "d", "storage.world():mod('other.mod')");
  CHECK(noread.status == RunStatus::PermissionDenied && noread.error.find("storage.read:<mod>") != std::string::npos);
  // unbounded_loop lifts the step limit only for the mod that requested it and was granted it
  const char* spin = "local n = 0 for i = 1, 1000000 do n = n + 1 end";
  CHECK(rt.run("asks.mod", "e", spin).status == RunStatus::Ok);
  auto spun = rt.run("greedy.mod", "e", spin);
  CHECK(spun.status == RunStatus::BudgetExceeded && spun.error.find("[greedy.mod]") != std::string::npos);
  // legacy-npc style: the real mod asks for nothing; even a lock entry cannot give it storage.account
  auto ids2 = new_ids();
  HostRuntime rt2(real_config());
  ModLoader l2(ids2);
  l2.set_activator(&rt2);
  auto rr = real_request();
  rr.lock.entries["legacy.npc"].granted.insert(*parse_permission("storage.account"));
  l2.load(rr);
  auto lnpc = rt2.run("legacy.npc", "x", "storage.account(5)");
  CHECK(lnpc.status == RunStatus::PermissionDenied && lnpc.error.find("[legacy.npc]") != std::string::npos);
}

TEST("MOD-INT-006", a_request_for_an_ungranted_permission_stops_the_whole_generation_before_any_code_runs) {
  need_vm();
  auto asks = pkg("asks.mod", "permissions = [\"net.raw\"]\n", "MARK_SHOULD_NOT_RUN = 1\nreturn nil\n");
  auto fine = pkg("fine.mod", "", "return nil\n");
  auto r = request({asks, fine});
  auto ids = new_ids();
  HostRuntime rt;
  ModLoader loader(ids);
  loader.set_activator(&rt);
  try { loader.load(r); CHECK(false); }
  catch (const LoadError& e) {
    CHECK(e.code() == "permission.ungranted");
    std::string w = e.what();
    CHECK(w.find("asks.mod") != std::string::npos && w.find("net.raw") != std::string::npos && w.find("mods.lock") != std::string::npos);
  }
  CHECK(!rt.live() && loader.generation() == 0);
}

TEST("MOD-INT-007", kill_switch_stops_hooks_of_the_mod_and_its_dependents_and_enable_restores_them) {
  need_vm();
  auto hook = [](const std::string& id, const std::string& hook_id) {
    return pkg(id, id == "hook.b" ? "[depends]\n\"hook.a\" = \"^1\"\n" : "",
               "events.on('test:evt', {id = '" + hook_id + "'}, function(ctx) end)\n");
  };
  auto a = hook("hook.a", "a1"), b = hook("hook.b", "b1"), c = hook("hook.c", "c1");
  auto r = request({a, b, c});
  auto ids = new_ids();
  struct Rec : ModObserver { std::vector<ModEvent> ev; void on_event(const ModEvent& e) override { ev.push_back(e); } } rec;
  HostRuntime rt;
  ModLoader loader(ids, &rec);
  loader.set_activator(&rt);
  loader.load(r);
  auto run_evt = [&] { script_host::HookContext ctx; return rt.dispatch("test:evt", ctx).ran; };
  CHECK(run_evt() == (std::vector<std::string>{"hook.a/a1", "hook.b/b1", "hook.c/c1"}));

  loader.disable("hook.a");
  CHECK(loader.state("hook.a") == ModState::Disabled && loader.state("hook.b") == ModState::Disabled && loader.state("hook.c") == ModState::Active);
  CHECK(run_evt() == (std::vector<std::string>{"hook.c/c1"}));
  bool saw_disabled_b = false;
  for (auto& e : rec.ev) if (e.kind == ModEvent::Kind::Disabled && e.mod_id == "hook.b") saw_disabled_b = e.detail.find("dependency 'hook.a'") != std::string::npos;
  CHECK(saw_disabled_b);

  // a reload of the whole set keeps them off and does not run their code
  loader.load(r);
  CHECK(run_evt() == (std::vector<std::string>{"hook.c/c1"}));
  CHECK(loader.generation() == 2);

  loader.enable("hook.a");
  CHECK(run_evt() == (std::vector<std::string>{"hook.a/a1", "hook.b/b1", "hook.c/c1"}));
  CHECK(loader.state("hook.b") == ModState::Active);
}

TEST("MOD-INT-008", kill_switch_on_the_real_mods) {
  need_vm();
  auto ids = new_ids();
  HostRuntime rt(real_config());
  ModLoader loader(ids);
  loader.set_activator(&rt);
  loader.load(real_request());
  loader.disable("rathena.base");  // the ruleset depends on it
  CHECK(loader.state("rathena.base") == ModState::Disabled && loader.state("ruleset.prerenewal") == ModState::Disabled);
  CHECK(loader.state("legacy.npc") == ModState::Active);
  CHECK(loader.disabled_reason("ruleset.prerenewal") == "dependency rathena.base is disabled");
  CHECK(rt.run("legacy.npc", "x", "return 1").status == script_host::RunStatus::Ok);
  loader.enable("rathena.base");
  CHECK(loader.state("ruleset.prerenewal") == ModState::Active);
}

TEST("MOD-INT-009", per_mod_counters_load_time_memory_and_hook_time) {
  need_vm();
  auto a = pkg("count.a", "", "events.on('test:evt', {id = 'h1'}, function(ctx) local n = 0 for i = 1, 20000 do n = n + i end end)\n"
                              "events.on('test:evt', {id = 'h2', phase = 'post'}, function(ctx) end)\n");
  auto b = pkg("count.b", "", "events.on('test:evt', {id = 'h'}, function(ctx) error('boom') end)\n");
  auto r = request({a, b});
  auto ids = new_ids();
  HostRuntime rt;
  ModLoader loader(ids);
  loader.set_activator(&rt);
  loader.load(r);
  for (int i = 0; i < 5; ++i) { script_host::HookContext ctx; rt.dispatch("test:evt", ctx); }
  auto sa = loader.stats("count.a"), sb = loader.stats("count.b");
  CHECK(sa.hook_calls == 10 && sa.hook_errors == 0 && sa.hook_time.count() > 0);
  CHECK(sb.hook_calls == 5 && sb.hook_errors == 5);        // the failing hook is counted, the dispatch continued
  CHECK(sa.memory_used > 0 && sb.memory_used > 0 && sa.memory_peak >= sa.memory_used);
  CHECK(sa.load_time.count() >= 0 && sa.loaded_in_generation == 1);
  CHECK(loader.stats("nobody").memory_used == 0);
}

TEST("MOD-INT-010", failed_generation_leaves_the_running_one_untouched) {
  need_vm();
  auto good = pkg("good.mod", "", "events.on('test:evt', {id = 'g'}, function(ctx) end)\n");
  auto ids = new_ids();
  HostRuntime rt;
  ModLoader loader(ids);
  loader.set_activator(&rt);
  loader.load(request({good}));
  auto before = rt.sandbox("good.mod");
  auto bad = pkg("bad.mod", "", "this is not luau (\n");
  try { loader.load(request({good, bad})); CHECK(false); }
  catch (const LoadError& e) { CHECK(e.code() == "activate.failed" && std::string(e.what()).find("bad.mod") != std::string::npos); }
  CHECK(loader.generation() == 1 && rt.sandbox("good.mod") == before);
  script_host::HookContext ctx;
  CHECK(rt.dispatch("test:evt", ctx).ran == (std::vector<std::string>{"good.mod/g"}));
  // a runtime error at load time is also reported with the mod id
  auto err = pkg("err.mod", "", "error('nope')\n");
  try { loader.load(request({good, err})); CHECK(false); }
  catch (const LoadError& e) { CHECK(std::string(e.what()).find("err.mod") != std::string::npos && std::string(e.what()).find("nope") != std::string::npos); }
  CHECK(loader.generation() == 1);
}

TEST("MOD-INT-011", hot_reload_of_one_mod_uses_script_host_generations) {
  need_vm();
  auto m = pkg("hot.mod", "", "events.on('test:evt', {id = 'v1'}, function(ctx) end)\n");
  auto ids = new_ids();
  HostRuntime rt;
  ModLoader loader(ids);
  loader.set_activator(&rt);
  loader.load(request({m}));
  CHECK(rt.reload_manager().active_generation("hot.mod") == 1);
  auto res = rt.reload_mod("hot.mod", "events.on('test:evt', {id = 'v2'}, function(ctx) end)\n");
  CHECK(res.ok && res.active_generation == 2);
  script_host::HookContext ctx;
  CHECK(rt.dispatch("test:evt", ctx).ran == (std::vector<std::string>{"hot.mod/v2"}));
  auto bad = rt.reload_mod("hot.mod", "syntax error here (");
  CHECK(!bad.ok && rt.reload_manager().active_generation("hot.mod") == 2);
}

namespace {
// definitions of a shipped ruleset package: `local P = "mod:kind/"` ... `{ id = P .. "name", client_id = N`
std::vector<DefinitionDecl> package_definitions(const fs::path& dir) {
  std::vector<DefinitionDecl> out;
  std::vector<fs::path> files;
  for (const auto& e : fs::directory_iterator(dir / "data"))
    if (e.path().extension() == ".luau") files.push_back(e.path());
  std::sort(files.begin(), files.end());
  for (const auto& f : files) {
    std::ifstream in(f);
    std::stringstream ss;
    ss << in.rdbuf();
    std::string text = ss.str();
    std::smatch pm;
    static const std::regex prefix("local P = \"([a-z0-9._-]+):([a-z_]+)/\"");
    if (!std::regex_search(text, pm, prefix)) continue;
    static const std::regex def("\\{ id = P \\.\\. \"([a-z0-9_]+)\", client_id = ([0-9]+)");
    for (std::sregex_iterator it(text.begin(), text.end(), def), end; it != end; ++it) {
      NamespacedId id{pm[1], pm[2], (*it)[1]};
      out.push_back({id, static_cast<std::uint32_t>(std::stoul((*it)[2]))});
    }
  }
  return out;
}
}  // namespace

TEST("MOD-INT-012", real_ruleset_definitions_register_against_the_range_registry_and_swapping_rulesets_works) {
  auto defs = package_definitions(pre_dir());
  CHECK(defs.size() >= 15);
  auto reg = RangeRegistry::load_file(fs::path(MODS_SOURCE_DIR) / "config" / "id_ranges.example.toml");
  reg.validate({m_of(pre_dir()), m_of(re_dir())});
  for (auto& d : defs) reg.check_claim(d.id, *d.client_id);   // every shipped number is inside the owner's declared range

  IdMap ids(reg.allocation());
  ModLoader loader(ids);
  LoadRequest r;
  r.manifests = {m_of(base_dir()), pre_repaired()};
  r.definitions = defs;
  r.definitions.push_back({NamespacedId{"ruleset.prerenewal", "skill", "brand_new"}, std::nullopt});   // allocated
  loader.load(r);
  auto bash = ids.client_id_of(NamespacedId{"ruleset.prerenewal", "skill", "sm_bash"});
  CHECK(bash == 5u);
  auto fresh = ids.client_id_of(NamespacedId{"ruleset.prerenewal", "skill", "brand_new"});
  CHECK(fresh.has_value() && *fresh >= 100000u);

  // switch to the renewal ruleset: same legacy numbers, different ids. Allowed because the two conflict.
  LoadRequest r2;
  r2.manifests = {m_of(base_dir()), m_of(re_dir())};
  for (auto d : defs) { d.id.mod = "ruleset.renewal"; r2.definitions.push_back(d); }
  loader.load(r2);
  CHECK(ids.client_id_of(NamespacedId{"ruleset.renewal", "skill", "sm_bash"}) == 5u);
  CHECK(ids.id_of("skill", 5, {"rathena.base", "ruleset.renewal"})->str() == "ruleset.renewal:skill/sm_bash");
  CHECK(ids.id_of("skill", 5, {"rathena.base", "ruleset.prerenewal"})->str() == "ruleset.prerenewal:skill/sm_bash");

  // a third mod taking the same legacy number while a ruleset owns it is a load error naming both
  LoadRequest r3;
  r3.manifests = {m_of(base_dir()), m_of(re_dir()), pkg("intruder", "", "").m};
  r3.definitions = {{NamespacedId{"intruder", "skill", "mine"}, 5u}};
  try { loader.load(r3); CHECK(false); }
  catch (const LoadError& e) {
    CHECK(e.code() == "id.collision" && std::string(e.what()).find("'intruder'") != std::string::npos);
    CHECK(std::string(e.what()).find("ruleset.") != std::string::npos);
  }
  CHECK(loader.generation() == 2);
}
