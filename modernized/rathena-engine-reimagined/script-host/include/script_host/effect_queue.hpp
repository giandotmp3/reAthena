#pragma once
// Ordered effect queue (BR-RULESET-116). Scripts never mutate the world during
// a calculation; they enqueue effects that native code applies afterwards.
#include <algorithm>
#include <functional>
#include <utility>
#include <string>
#include <vector>

namespace script_host {

struct Effect {
  std::string kind;          // opaque to the host, e.g. "mymod:apply_status"
  std::string target;        // opaque entity handle string
  std::string payload_json;  // opaque payload
  bool operator==(const Effect&) const = default;
};

class EffectQueue {
 public:
  void push(Effect e) { items_.push_back(std::move(e)); }
  std::size_t size() const { return items_.size(); }
  bool empty() const { return items_.empty(); }
  const std::vector<Effect>& items() const { return items_; }
  void discard() { items_.clear(); }  // transactional rollback on cancel()
  // Rolls back to `n` effects (used per dispatch so effects queued by an outer
  // scope survive a cancel inside this dispatch).
  void truncate(std::size_t n) { if (n < items_.size()) items_.resize(n); }
  // Removes and returns the effects queued after the first `n`.
  std::vector<Effect> take_from(std::size_t n) {
    std::vector<Effect> out;
    if (n < items_.size()) {
      out.assign(std::make_move_iterator(items_.begin() + static_cast<std::ptrdiff_t>(n)), std::make_move_iterator(items_.end()));
      items_.resize(n);
    }
    return out;
  }
  std::vector<Effect> drain() { return std::exchange(items_, {}); }

 private:
  std::vector<Effect> items_;
};

// Applies a settled queue through a native applier. Effects queued by the
// applier itself (re-entrancy) are rejected by the guard.
class EffectApplier {
 public:
  using ApplyFn = std::function<void(const Effect&)>;
  // Applies in order. Throws std::logic_error if called while already applying.
  void apply(EffectQueue& q, const ApplyFn& fn);
  bool applying() const { return applying_; }

 private:
  bool applying_ = false;
};

}  // namespace script_host
