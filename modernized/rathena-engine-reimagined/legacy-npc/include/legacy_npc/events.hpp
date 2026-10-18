#pragma once
#include <cstddef>
#include <deque>
#include <optional>
#include <string>
#include <string_view>

namespace legacy_npc {

enum class EventKind {
  Init, Timer, Clock, PlayerEvent, Touch, Whisper, UserDefined,
  // appended (the first seven keep their values): the rest of the legacy label table, script.cpp script_config
  Minute, Hour, Day, Weekday, TimerQuit, TouchNpc, Command, Shop, Agit, InterIf, Guild, Instance, Navi
};

struct EventLabel {
  EventKind kind = EventKind::UserDefined;
  std::string label;
  std::optional<int> arg;  // OnTimer<ms>, OnClock<hhmm>
};

// nullopt if the label does not begin with "On".
std::optional<EventLabel> classify_event_label(std::string_view label);
// Bus event name for a label, keeping the legacy name: "legacy:<label>".
std::string bus_event_name(const EventLabel& l);

// 2-slot player event queue with 100 ms dequeue, behind a compatibility flag. Q-J (answered from npc.cpp, see luau/events.luau): an event
// aimed at a player inside a script is queued; with both slots taken the NEW event is dropped and nothing older is displaced; the
// dequeue (script end) turns the oldest event into a 100 ms player timer. With the flag off the queue is unbounded.
class PlayerEventQueue {
 public:
  static constexpr std::size_t kSlots = 2;
  static constexpr int kDequeueMs = 100;
  explicit PlayerEventQueue(bool two_slot_compat) : compat_(two_slot_compat) {}
  // false when rejected because the slots are full.
  bool push(std::string label);
  std::optional<std::string> pop();
  std::size_t size() const { return q_.size(); }
  std::size_t dropped() const { return dropped_; }

 private:
  bool compat_;
  std::size_t dropped_ = 0;
  std::deque<std::string> q_;
};

}  // namespace legacy_npc
