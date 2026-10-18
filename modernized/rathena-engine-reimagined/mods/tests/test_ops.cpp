// Kill switch, observability, all-or-nothing reload. Tags MOD-OPS-*.
#include "harness.hpp"
#include "mods/loader.hpp"

using namespace mods;

static Manifest mk(std::string id) {
  Manifest m;
  m.id = std::move(id);
  m.version = {1, 0, 0};
  m.engine_api = "^1";
  return m;
}
static LoadRequest req(std::vector<Manifest> ms) {
  LoadRequest r;
  r.manifests = std::move(ms);
  r.verifier = [](auto&, auto&, auto&) { return true; };
  return r;
}
struct Recorder : ModObserver {
  std::vector<ModEvent> events;
  void on_event(const ModEvent& e) override { events.push_back(e); }
};
static IdMap ids() { return IdMap({{"skill", {10000, 10999}}}); }

TEST("MOD-OPS-001", load_activates_mods_in_order) {
  auto im = ids();
  ModLoader l(im);
  auto a = mk("a"), b = mk("b");
  b.depends["a"] = "^1";
  l.load(req({b, a}));
  CHECK(l.generation() == 1);
  auto o = l.load_order();
  CHECK(o.size() == 2 && o[0] == "a" && o[1] == "b");
  CHECK(l.state("a") == ModState::Active);
}

TEST("MOD-OPS-002", kill_switch_disables_mod_and_dependents) {
  auto im = ids();
  ModLoader l(im);
  auto a = mk("a"), b = mk("b"), c = mk("c");
  b.depends["a"] = "^1";
  l.load(req({a, b, c}));
  l.disable("a");
  l.disable("a");  // idempotent
  CHECK(l.state("a") == ModState::Disabled);
  CHECK(l.state("b") == ModState::Disabled);
  CHECK(l.state("c") == ModState::Active);
  l.enable("a");
  CHECK(l.state("a") == ModState::Active);
}

TEST("MOD-OPS-003", observer_receives_load_and_disable_events) {
  auto im = ids();
  Recorder rec;
  ModLoader l(im, &rec);
  l.load(req({mk("a")}));
  l.disable("a");
  bool loaded = false, disabled = false;
  for (auto& e : rec.events) {
    if (e.kind == ModEvent::Kind::Loaded && e.mod_id == "a") loaded = true;
    if (e.kind == ModEvent::Kind::Disabled && e.mod_id == "a") disabled = true;
  }
  CHECK(loaded && disabled);
}

TEST("MOD-OPS-004", failed_reload_keeps_previous_generation) {
  auto im = ids();
  Recorder rec;
  ModLoader l(im, &rec);
  l.load(req({mk("a")}));
  auto bad = mk("bad");
  bad.depends["ghost"] = "^1";
  bool threw = false;
  try { l.load(req({mk("a"), bad})); } catch (const LoadError&) { threw = true; }
  CHECK(threw);
  CHECK(l.generation() == 1);
  CHECK(l.load_order().size() == 1 && l.load_order()[0] == "a");
  bool refused = false;
  for (auto& e : rec.events) if (e.kind == ModEvent::Kind::Refused) refused = true;
  CHECK(refused);
}

TEST("MOD-OPS-005", ungranted_permission_blocks_whole_generation) {
  auto im = ids();
  ModLoader l(im);
  auto m = mk("a");
  m.permissions.push_back(*parse_permission("net.raw"));
  bool threw = false;
  try { l.load(req({m})); } catch (const LoadError& e) { threw = e.code() == "permission.ungranted"; }
  CHECK(threw);
  CHECK(l.generation() == 0);
}

// ---- added with the implementation ---------------------------------------------
namespace {
struct FakeActivator : ModActivator {
  struct Session : ActivationSession {
    FakeActivator& a;
    std::vector<std::string> mine;
    explicit Session(FakeActivator& x) : a(x) {}
    void activate(const ActivationInfo& i) override {
      if (a.fail_on == i.manifest->id) throw LoadError("activate.failed", "boom in " + i.manifest->id);
      mine.push_back(i.manifest->id);
      a.grants[i.manifest->id] = i.granted;
    }
    void commit() override { a.live = mine; ++a.commits; }
  };
  std::vector<std::string> live, deactivated, reactivated;
  std::map<std::string, std::set<Permission>> grants;
  std::string fail_on;
  int commits = 0;
  std::unique_ptr<ActivationSession> begin() override { return std::make_unique<Session>(*this); }
  std::string deactivate(const std::string& m) override { deactivated.push_back(m); return {}; }
  void reactivate(const std::string& m) override { reactivated.push_back(m); }
  ActivatorStats stats(const std::string& m) const override {
    ActivatorStats s;
    s.memory_used = 1000 + m.size();
    s.hook_calls = 3;
    return s;
  }
};
}  // namespace

TEST("MOD-OPS-006", activation_failure_abandons_the_generation_and_keeps_ids_untouched) {
  auto im = ids();
  FakeActivator act;
  Recorder rec;
  ModLoader l(im, &rec);
  l.set_activator(&act);
  l.load(req({mk("a")}));
  CHECK(act.live == std::vector<std::string>{"a"});
  auto b = mk("b"), c = mk("c");
  b.depends["a"] = "^1";
  auto r = req({mk("a"), b, c});
  r.definitions.push_back({*parse_namespaced_id("b:skill/s1"), std::nullopt});
  act.fail_on = "c";
  bool threw = false;
  try { l.load(r); } catch (const LoadError& e) { threw = e.code() == "activate.failed" && std::string(e.what()).find("boom in c") != std::string::npos; }
  CHECK(threw);
  CHECK(act.commits == 1 && act.live == std::vector<std::string>{"a"});
  CHECK(l.generation() == 1 && l.load_order().size() == 1);
  CHECK(im.live_count() == 0);  // the allocation made for the failed generation was rolled back
  CHECK(rec.events.back().kind == ModEvent::Kind::Refused && rec.events.back().mod_id == "c");
  act.fail_on.clear();
  l.load(r);
  CHECK(l.generation() == 2 && im.live_count() == 1 && act.live.size() == 3);
}

TEST("MOD-OPS-007", only_effective_grants_reach_the_activator) {
  auto im = ids();
  FakeActivator act;
  ModLoader l(im);
  l.set_activator(&act);
  auto m = mk("a");
  m.permissions = {*parse_permission("storage.account")};
  auto r = req({m});
  r.lock.entries["a"].granted = {*parse_permission("storage.account"), *parse_permission("net.raw")};
  l.load(r);
  CHECK(act.grants.at("a") == std::set<Permission>{*parse_permission("storage.account")});
}

TEST("MOD-OPS-008", kill_switch_survives_reload_and_does_not_run_the_mods_code) {
  auto im = ids();
  FakeActivator act;
  Recorder rec;
  ModLoader l(im, &rec);
  l.set_activator(&act);
  auto a = mk("a"), b = mk("b"), c = mk("c");
  b.depends["a"] = "^1";
  l.load(req({a, b, c}));
  l.disable("a");
  CHECK(act.deactivated == (std::vector<std::string>{"a", "b"}));
  CHECK(l.disabled_reason("a") == "operator" && l.disabled_reason("b") == "dependency a is disabled" && l.disabled_reason("c").empty());
  act.live.clear();
  l.load(req({a, b, c}));  // generation 2: a and b stay off
  CHECK(act.live == std::vector<std::string>{"c"});
  CHECK(l.state("a") == ModState::Disabled && l.state("b") == ModState::Disabled && l.state("c") == ModState::Active);
  l.enable("a");
  CHECK(act.reactivated == (std::vector<std::string>{"a", "b"}));
  CHECK(l.state("b") == ModState::Active);
  CHECK(l.state("nope") == ModState::Invalid);
  bool threw = false;
  try { l.disable("nope"); } catch (const LoadError& e) { threw = e.code() == "ops.unknown_mod"; }
  CHECK(threw);
}

TEST("MOD-OPS-009", stats_combine_load_time_and_activator_counters) {
  auto im = ids();
  FakeActivator act;
  ModLoader l(im);
  l.set_activator(&act);
  l.load(req({mk("a")}));
  auto s = l.stats("a");
  CHECK(s.loaded_in_generation == 1 && s.memory_used == 1001 && s.hook_calls == 3);
  CHECK(s.load_time.count() >= 0);
  CHECK(l.stats("unknown").memory_used == 1007);  // activator answers; loader has no load record
}

TEST("MOD-OPS-010", definitions_are_registered_atomically_with_namespace_ownership) {
  auto im = IdMap({{"skill", {100, 199}}});
  ModLoader l(im);
  auto a = mk("a"), b = mk("b");
  auto r = req({a, b});
  r.definitions = {{*parse_namespaced_id("a:skill/one"), 5u}, {*parse_namespaced_id("b:skill/two"), 5u}};
  bool threw = false;
  try { l.load(r); } catch (const LoadError& e) { threw = e.code() == "id.collision" && std::string(e.what()).find("'a'") != std::string::npos && std::string(e.what()).find("'b'") != std::string::npos; }
  CHECK(threw && im.live_count() == 0 && l.generation() == 0);
  r.definitions = {{*parse_namespaced_id("zzz:skill/one"), std::nullopt}};
  threw = false;
  try { l.load(r); } catch (const LoadError& e) { threw = e.code() == "id.unknown_mod"; }
  CHECK(threw);
  // explicit claims are placed before allocations, so a later explicit number is never taken by an earlier allocation
  r.definitions = {{*parse_namespaced_id("a:skill/auto"), std::nullopt}, {*parse_namespaced_id("b:skill/legacy"), 100u}};
  l.load(r);
  CHECK(im.client_id_of(*parse_namespaced_id("b:skill/legacy")) == 100u);
  CHECK(im.client_id_of(*parse_namespaced_id("a:skill/auto")) == 101u);
}
