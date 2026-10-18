#pragma once
// Ordered effect queue (BR-RULESET-116). Scripts never mutate the world during
// a calculation; they enqueue effects that native code applies afterwards.
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
