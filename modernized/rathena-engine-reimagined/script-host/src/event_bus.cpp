#include "script_host/event_bus.hpp"

#include "script_host/not_implemented.hpp"

namespace script_host {

void EventBus::add(HookSpec spec, HookFn fn) {
  entries_.push_back({std::move(spec), std::move(fn), entries_.size()});
}
void EventBus::validate() const { not_implemented("EventBus::validate"); }
std::vector<std::string> EventBus::order(const std::string&) const { not_implemented("EventBus::order"); }
DispatchResult EventBus::dispatch(const std::string&, HookContext&) { not_implemented("EventBus::dispatch"); }
void EventBus::mark_protected(const std::string&) { not_implemented("EventBus::mark_protected"); }
void EventBus::disable_hook(const std::string&) { not_implemented("EventBus::disable_hook"); }

}  // namespace script_host
