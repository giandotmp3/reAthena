#include "script_host/effect_queue.hpp"

#include <stdexcept>

namespace script_host {

void EffectApplier::apply(EffectQueue& q, const ApplyFn& fn) {
  if (applying_) throw std::logic_error("re-entrant effect application");
  applying_ = true;
  try {
    for (auto& e : q.drain()) fn(e);
  } catch (...) {
    applying_ = false;
    throw;
  }
  applying_ = false;
}

}  // namespace script_host
