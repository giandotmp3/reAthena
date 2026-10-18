#include "engine/tick/tick.hpp"

#include <algorithm>

#include "engine/common.hpp"

namespace engine {

Tick clamp_poll_delay(Tick d) { return std::clamp(d, kTickFloorMs, kPollCeilingMs); }

TimerId TimerQueue::add_once(Tick, TimerCallback) { throw NotImplemented("TimerQueue::add_once"); }
TimerId TimerQueue::add_interval(Tick, TimerCallback) { throw NotImplemented("TimerQueue::add_interval"); }
bool TimerQueue::cancel(TimerId) { throw NotImplemented("TimerQueue::cancel"); }
size_t TimerQueue::run_due() { throw NotImplemented("TimerQueue::run_due"); }
std::optional<Tick> TimerQueue::next_due() const { throw NotImplemented("TimerQueue::next_due"); }

}  // namespace engine
