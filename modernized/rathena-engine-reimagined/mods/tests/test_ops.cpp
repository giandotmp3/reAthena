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

PENDING_TEST("MOD-OPS-001", load_activates_mods_in_order) {
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

PENDING_TEST("MOD-OPS-002", kill_switch_disables_mod_and_dependents) {
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

PENDING_TEST("MOD-OPS-003", observer_receives_load_and_disable_events) {
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

PENDING_TEST("MOD-OPS-004", failed_reload_keeps_previous_generation) {
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

PENDING_TEST("MOD-OPS-005", ungranted_permission_blocks_whole_generation) {
  auto im = ids();
  ModLoader l(im);
  auto m = mk("a");
  m.permissions.push_back(*parse_permission("net.raw"));
  bool threw = false;
  try { l.load(req({m})); } catch (const LoadError& e) { threw = e.code() == "permission.ungranted"; }
  CHECK(threw);
  CHECK(l.generation() == 0);
}
