#include "legacy_npc/events.hpp"

#include <array>

namespace legacy_npc {

static std::optional<int> parse_digits(std::string_view s) {
  if (s.empty() || s.size() > 9) return std::nullopt;
  int n = 0;
  for (char c : s) {
    if (c < '0' || c > '9') return std::nullopt;
    n = n * 10 + (c - '0');
  }
  return n;
}

std::optional<EventLabel> classify_event_label(std::string_view l) {
  if (l.substr(0, 2) != "On") return std::nullopt;
  EventLabel e;
  e.label = std::string(l);
  static const std::array<std::string_view, 9> player = {
      "OnPCLoginEvent", "OnPCLogoutEvent", "OnPCDieEvent", "OnPCKillPCEvent",
      "OnPCKillNPCEvent", "OnPCBaseLvUpEvent", "OnPCJobLvUpEvent", "OnPCStatCalcEvent",
      "OnPCLoadMapEvent"};
  if (l == "OnInit") { e.kind = EventKind::Init; return e; }
  if (l == "OnTouch" || l == "OnTouch_") { e.kind = EventKind::Touch; return e; }
  if (l == "OnWhisperGlobal") { e.kind = EventKind::Whisper; return e; }
  for (auto p : player)
    if (l == p) { e.kind = EventKind::PlayerEvent; return e; }
  if (l.substr(0, 7) == "OnTimer") {
    if (auto n = parse_digits(l.substr(7))) { e.kind = EventKind::Timer; e.arg = n; return e; }
  }
  if (l.substr(0, 7) == "OnClock") {
    if (auto n = parse_digits(l.substr(7))) { e.kind = EventKind::Clock; e.arg = n; return e; }
  }
  e.kind = EventKind::UserDefined;
  return e;
}

std::string bus_event_name(const EventLabel& l) { return "legacy:" + l.label; }

bool PlayerEventQueue::push(std::string label) {
  if (compat_ && q_.size() >= kSlots) return false;
  q_.push_back(std::move(label));
  return true;
}

std::optional<std::string> PlayerEventQueue::pop() {
  if (q_.empty()) return std::nullopt;
  auto v = std::move(q_.front());
  q_.pop_front();
  return v;
}

}  // namespace legacy_npc
