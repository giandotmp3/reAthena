#include "engine/tick/tick.hpp"

#include <algorithm>

#include "engine/common.hpp"

namespace engine {

Tick clamp_poll_delay(Tick d) { return std::clamp(d, kTickFloorMs, kPollCeilingMs); }

TimerId TimerQueue::insert(Tick due, Tick period, TimerCallback cb) {
  TimerId id = next_id_++;
  Entry& e = timers_[id];
  e.cb = std::move(cb);
  e.due = due;
  e.period = period;
  e.seq = next_seq_++;
  e.queued = true;
  queue_.emplace(due, e.seq, id);
  return id;
}

TimerId TimerQueue::add_once(Tick delay_ms, TimerCallback cb) {
  return insert(clock_.now() + delay_ms, 0, std::move(cb));
}

TimerId TimerQueue::add_interval(Tick period_ms, TimerCallback cb) {
  if (period_ms < 1) return kNoTimer;
  return insert(clock_.now() + period_ms, period_ms, std::move(cb));
}

bool TimerQueue::cancel(TimerId id) {
  auto it = timers_.find(id);
  if (it == timers_.end()) return false;
  if (it->second.queued) queue_.erase({it->second.due, it->second.seq, id});
  timers_.erase(it);
  return true;
}

size_t TimerQueue::run_due() {
  if (running_) return 0;  // not re-entrant: a callback must not drive the loop
  running_ = true;
  struct Reset { bool& f; ~Reset() { f = false; } } reset{running_};

  const Tick now = clock_.now();
  size_t fired = 0;
  while (!queue_.empty()) {
    auto [due, seq, id] = *queue_.begin();
    if (due > now) break;
    queue_.erase(queue_.begin());

    Entry& e = timers_.at(id);
    e.queued = false;
    const Tick scheduled = (due - now < -kLateTimerMs) ? now : due;
    const bool interval = e.period > 0;
    TimerCallback cb;
    if (interval) {
      cb = e.cb;  // entry must outlive the call so cancel() from inside works
    } else {
      cb = std::move(e.cb);
      timers_.erase(id);  // one-shot: gone before the call, cancel(id) -> false
    }
    ++fired;
    if (cb) cb(id, scheduled, now);

    if (interval) {
      auto it = timers_.find(id);
      if (it != timers_.end()) {  // not cancelled by its own callback
        Entry& r = it->second;
        r.due = (due - now < -kLateTimerMs) ? now + r.period : due + r.period;
        r.seq = next_seq_++;
        r.queued = true;
        queue_.emplace(r.due, r.seq, id);
      }
    }
  }
  return fired;
}

std::optional<Tick> TimerQueue::next_due() const {
  if (queue_.empty()) return std::nullopt;
  return std::get<0>(*queue_.begin());
}

Tick TimerQueue::poll_delay() const {
  if (queue_.empty()) return kPollCeilingMs;
  return clamp_poll_delay(std::get<0>(*queue_.begin()) - clock_.now());
}

}  // namespace engine
