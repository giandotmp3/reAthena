#pragma once
#include <cstddef>
#include <deque>
#include <optional>
#include <string>
#include <string_view>

namespace legacy_npc {

enum class EventKind { Init, Timer, Clock, PlayerEvent, Touch, Whisper, UserDefined };

struct EventLabel {
  EventKind kind = EventKind::UserDefined;
  std::string label;
  std::optional<int> arg;  // OnTimer<ms>, OnClock<hhmm>
};

// nullopt if the label does not begin with "On".
std::optional<EventLabel> classify_event_label(std::string_view label);
// Bus event name for a label, keeping the legacy name: "legacy:<label>".
std::string bus_event_name(const EventLabel& l);

// 2-slot player event queue with 100 ms dequeue, behind a compatibility flag.
class PlayerEventQueue {
 public:
  static constexpr std::size_t kSlots = 2;
  static constexpr int kDequeueMs = 100;
  explicit PlayerEventQueue(bool two_slot_compat) : compat_(two_slot_compat) {}
  // false when rejected because the slots are full.
  bool push(std::string label);
  std::optional<std::string> pop();
  std::size_t size() const { return q_.size(); }

 private:
  bool compat_;
  std::deque<std::string> q_;
};

}  // namespace legacy_npc
