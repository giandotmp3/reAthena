#include "legacy_npc/events.hpp"
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

PENDING("LNPC-EVT-003", third_event_dropped_under_compat_flag, "Q-J: drop contract unconfirmed (which event is dropped)") {
  PlayerEventQueue q(true);
  q.push("OnA"); q.push("OnB");
  CHECK(!q.push("OnC"));
  CHECK(q.size() == 2);
  throw legacy_npc::NotImplemented("Q-J drop policy not settled");
}

PENDING("LNPC-EVT-004", on_labels_register_bus_hooks_with_legacy_names, "needs loader + script-host bus") {
  throw legacy_npc::NotImplemented("hook registration");
}
