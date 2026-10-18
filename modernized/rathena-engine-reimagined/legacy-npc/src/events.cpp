#include "legacy_npc/events.hpp"

#include <array>
#include <cctype>
#include <map>

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

static std::string lower(std::string_view s) {
  std::string o(s);
  for (auto& c : o) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return o;
}

// Mirrors luau/events.luau `classify` (the table of script.cpp script_config); a test compares both on the same labels.
std::optional<EventLabel> classify_event_label(std::string_view label) {
  auto l = lower(label);
  if (l.substr(0, 2) != "on") return std::nullopt;
  EventLabel e;
  e.label = std::string(label);
  static const std::map<std::string, EventKind> fixed = {
      {"oninit", EventKind::Init}, {"oninterifinit", EventKind::InterIf}, {"oninterifinitonce", EventKind::InterIf},
      {"ontouch", EventKind::Touch}, {"ontouch_", EventKind::Touch}, {"ontouchnpc", EventKind::TouchNpc},
      {"onwhisperglobal", EventKind::Whisper}, {"oncommand", EventKind::Command}, {"onbuyitem", EventKind::Shop},
      {"onsellitem", EventKind::Shop}, {"onguildbreak", EventKind::Guild}, {"ontimerquit", EventKind::TimerQuit},
      {"oninstanceinit", EventKind::Instance}, {"oninstancedestroy", EventKind::Instance}, {"onnavigenerate", EventKind::Navi},
      {"onagitstart", EventKind::Agit}, {"onagitinit", EventKind::Agit}, {"onagitend", EventKind::Agit},
      {"onagitstart2", EventKind::Agit}, {"onagitinit2", EventKind::Agit}, {"onagitend2", EventKind::Agit},
      {"onagitstart3", EventKind::Agit}, {"onagitinit3", EventKind::Agit}, {"onagitend3", EventKind::Agit},
      {"onpcdieevent", EventKind::PlayerEvent}, {"onpckillevent", EventKind::PlayerEvent}, {"onnpckillevent", EventKind::PlayerEvent},
      {"onpcloginevent", EventKind::PlayerEvent}, {"onpclogoutevent", EventKind::PlayerEvent},
      {"onpcloadmapevent", EventKind::PlayerEvent}, {"onpcbaselvupevent", EventKind::PlayerEvent},
      {"onpcjoblvupevent", EventKind::PlayerEvent}, {"onpcidentifyevent", EventKind::PlayerEvent}};
  if (auto it = fixed.find(l); it != fixed.end()) { e.kind = it->second; return e; }
  struct Fam { const char* prefix; EventKind kind; int digits; };  // digits < 0: any count
  static const Fam fams[] = {{"ontimer", EventKind::Timer, -1}, {"onclock", EventKind::Clock, 4}, {"onminute", EventKind::Minute, 2},
                             {"onhour", EventKind::Hour, 2}, {"onday", EventKind::Day, 4}, {"onsun", EventKind::Weekday, 4},
                             {"onmon", EventKind::Weekday, 4}, {"ontue", EventKind::Weekday, 4}, {"onwed", EventKind::Weekday, 4},
                             {"onthu", EventKind::Weekday, 4}, {"onfri", EventKind::Weekday, 4}, {"onsat", EventKind::Weekday, 4}};
  for (auto& f : fams) {
    std::string_view p = f.prefix;
    if (l.size() > p.size() && std::string_view(l).substr(0, p.size()) == p) {
      auto rest = std::string_view(l).substr(p.size());
      if ((f.digits < 0 || static_cast<int>(rest.size()) == f.digits))
        if (auto n = parse_digits(rest)) { e.kind = f.kind; e.arg = n; return e; }
    }
  }
  e.kind = EventKind::UserDefined;
  return e;
}

std::string bus_event_name(const EventLabel& l) { return "legacy:" + l.label; }

bool PlayerEventQueue::push(std::string label) {
  if (compat_ && q_.size() >= kSlots) { ++dropped_; return false; }
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
