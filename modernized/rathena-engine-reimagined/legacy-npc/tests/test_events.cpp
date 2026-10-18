#include "legacy_npc/events.hpp"
#include "legacy_npc/package.hpp"
#include "legacy_npc/types.hpp"

#include "mini_test.hpp"

using namespace legacy_npc;

ACCEPT("LNPC-EVT-001", event_label_classification) {
  CHECK(classify_event_label("OnInit")->kind == EventKind::Init);
  auto t = classify_event_label("OnTimer5000");
  CHECK(t && t->kind == EventKind::Timer && t->arg == 5000);
  auto c = classify_event_label("OnClock0830");
  CHECK(c && c->kind == EventKind::Clock && c->arg == 830);
  CHECK(classify_event_label("OnPCLoginEvent")->kind == EventKind::PlayerEvent);
  CHECK(classify_event_label("OnMyMobDead")->kind == EventKind::UserDefined);
  CHECK(!classify_event_label("L_Start"));
  CHECK(bus_event_name(*classify_event_label("OnInit")) == "legacy:OnInit");
}

ACCEPT("LNPC-EVT-002", two_slot_queue_accepts_two_and_dequeues_in_order) {
  PlayerEventQueue q(true);
  CHECK(PlayerEventQueue::kSlots == 2 && PlayerEventQueue::kDequeueMs == 100);
  CHECK(q.push("OnA") && q.push("OnB"));
  CHECK(*q.pop() == "OnA" && *q.pop() == "OnB" && !q.pop());
}

// Q-J, answered from npc.cpp (npc_event_sub :1751-1758, npc_event :5110-5116): with both slots taken the NEW event is dropped; the older
// two keep their order. The dequeue (npc.cpp:1176-1204) is exercised end to end in luau/tests/t_events.luau (LNPC-EVT-006).
ACCEPT("LNPC-EVT-003", third_event_dropped_under_compat_flag) {
  PlayerEventQueue q(true);
  q.push("OnA"); q.push("OnB");
  CHECK(!q.push("OnC"));
  CHECK(q.size() == 2 && q.dropped() == 1);
  CHECK(*q.pop() == "OnA" && *q.pop() == "OnB" && !q.pop());  // C is gone, A and B are intact and in order
  PlayerEventQueue unbounded(false);
  for (int i = 0; i < 5; ++i) CHECK(unbounded.push("e"));
  CHECK(unbounded.size() == 5 && unbounded.dropped() == 0);
}

// The C++ and the Luau classification tables are the same table.
ACCEPT_LUAU("LNPC-EVT-001", cpp_and_luau_classify_the_same_labels) {
  const char* labels[] = {"OnInit", "OnTouch", "OnTouch_", "OnTouchNPC", "OnTimer5000", "OnTimerQuit", "OnClock0830", "OnClock83", "OnMinute30",
                          "OnHour07", "OnDay1225", "OnSun2359", "OnPCLoginEvent", "OnPCKillEvent", "OnNPCKillEvent", "OnWhisperGlobal",
                          "OnAgitStart2", "OnInterIfInit", "OnMyMobDead", "OnBuyItem", "OnInstanceInit", "OnCommand", "NotAnEvent"};
  auto& host = shared_host();
  host.clear_log();
  std::string list;
  for (auto l : labels) list += std::string("'") + l + "',";
  host.run_ok("classify", "for _, l in {" + list + "} do local c = LEGACY.events.classify(l); print('@c\\t' .. l .. '\\t' .. (c and c.kind or 'nil') .. '\\t' .. tostring(c and c.arg or '')) end");
  auto rec = host.records("@c");
  CHECK(rec.size() == sizeof(labels) / sizeof(labels[0]));
  auto name = [](EventKind k) {
    switch (k) {
      case EventKind::Init: return "init"; case EventKind::Timer: return "timer"; case EventKind::Clock: return "clock";
      case EventKind::PlayerEvent: return "player"; case EventKind::Touch: return "touch"; case EventKind::Whisper: return "whisper";
      case EventKind::UserDefined: return "user"; case EventKind::Minute: return "minute"; case EventKind::Hour: return "hour";
      case EventKind::Day: return "day"; case EventKind::Weekday: return "weekday"; case EventKind::TimerQuit: return "timerquit";
      case EventKind::TouchNpc: return "touchnpc"; case EventKind::Command: return "command"; case EventKind::Shop: return "shop";
      case EventKind::Agit: return "agit"; case EventKind::InterIf: return "interif"; case EventKind::Guild: return "guild";
      case EventKind::Instance: return "instance"; case EventKind::Navi: return "navi";
    }
    return "?";
  };
  for (auto& r : rec) {
    auto c = classify_event_label(r[1]);
    if (r[2] == "nil") { CHECK_MSG(!c, r[1]); continue; }
    CHECK_MSG(c.has_value(), r[1]);
    CHECK_MSG(r[2] == name(c->kind), r[1] + ": luau " + r[2] + " cpp " + name(c->kind));
    if (!r[3].empty()) CHECK_MSG(c->arg && std::to_string(*c->arg) == r[3], r[1]);
  }
}

// On* labels register one engine-bus hook each under the legacy event name "legacy:<Label>" (the hook id is "<npc>::<Label>"), through
// the real script-host reload manager and event bus; dispatching the event runs the NPC's label.
ACCEPT_LUAU("LNPC-EVT-004", on_labels_register_bus_hooks_with_legacy_names) {
  script_host::StorageApi storage;
  script_host::ReloadManager rm;
  script_host::ModEnvironmentSpec spec;
  spec.mod_id = kPackageId;
  spec.memory_cap_bytes = 256ull << 20;
  spec.budget.steps_per_dispatch = 500000000;
  spec.budget.wall_us_per_dispatch = 0;
  spec.budget.wall_us_per_tick = 0;
  rm.set_environment(spec);
  rm.set_storage(&storage);
  storage.set_quota(kPackageId, 64ull << 20);
  std::string npcs =
      "prontera,150,150,4\tscript\tHooks\t111,{\n end;\nOnInit:\n $ran$ = $ran$ + \"I\"; end;\nOnTouch:\n end;\nOnTimer5000:\n end;\n"
      "OnClock0830:\n end;\nOnMinute30:\n end;\nOnPCLoginEvent:\n $ran$ = $ran$ + \"L\"; end;\nOnMyMobDead:\n end;\n}\n"
      "prontera,152,150,4\tduplicate(Hooks)\tHooks2\t111\n";
  std::string boot = bundle_package(package_dir()) +
      "\nlocal rt = LEGACY.new{ host = { storage = storage, timers = timers, events = events } }\n"
      "rt:addFile('hooks.txt', " + long_string(npcs) + ")\nLEGACY.registered = rt:registerEvents()\nLEGACY.rt = rt\n";
  auto r = rm.reload(kPackageId, boot);
  CHECK_MSG(r.ok, r.error);
  auto order = rm.bus().order("legacy:OnInit");
  CHECK(order.size() == 2);
  CHECK(order[0] == "legacy.npc/Hooks::OnInit" && order[1] == "legacy.npc/Hooks2::OnInit");
  for (auto ev : {"legacy:OnTouch", "legacy:OnTimer5000", "legacy:OnClock0830", "legacy:OnMinute30", "legacy:OnPCLoginEvent", "legacy:OnMyMobDead"})
    CHECK_MSG(rm.bus().order(ev).size() == 2, ev);
  CHECK(rm.bus().size() == 14);
  // the engine dispatches OnInit; the context number carries the attached player (0: none)
  script_host::HookContext ctx;
  ctx.num.set(0);
  auto d = rm.bus().dispatch("legacy:OnInit", ctx);
  CHECK(d.errors.empty() && d.ran.size() == 2);
  auto v = storage.get(script_host::Scope::World, "world", kPackageId, kPackageId, "legacy:world/ran$");
  CHECK(v && std::get<std::string>(*v) == "II");
}
