#include "tooling/sim.hpp"

#include <algorithm>
#include <sstream>

namespace tooling::sim {

uint64_t VirtualClock::schedule(int64_t delay_ms, std::function<void()> fn) {
  timers_.push_back({now_ + delay_ms, ++seq_, std::move(fn)});
  return seq_;
}

void VirtualClock::advance(int64_t ms) {
  const int64_t target = now_ + ms;
  for (;;) {
    auto it = std::min_element(timers_.begin(), timers_.end(), [](const T& a, const T& b) {
      return a.due != b.due ? a.due < b.due : a.seq < b.seq;
    });
    if (it == timers_.end() || it->due > target) break;
    T t = std::move(*it);
    timers_.erase(it);
    now_ = std::max(now_, t.due);
    t.fn();
  }
  now_ = target;
}

uint32_t DeterministicRng::next() {
  ++draws_;
  s_ ^= s_ >> 12; s_ ^= s_ << 25; s_ ^= s_ >> 27;
  return static_cast<uint32_t>((s_ * 0x2545F4914F6CDD1DULL) >> 32);
}
int32_t DeterministicRng::rnd_range(int32_t lo, int32_t hi) {
  uint32_t r = next();
  if (hi <= lo) return lo;
  return lo + static_cast<int32_t>(r % static_cast<uint32_t>(hi - lo + 1));
}
bool DeterministicRng::chance(int32_t rate, int32_t denom) {
  int32_t r = rnd_range(0, denom - 1);  // draws even when rate <= 0
  return rate > 0 && r < rate;
}
DeterministicRng DeterministicRng::derive(const std::string& hook_id) const {
  uint64_t h = 1469598103934665603ULL;
  for (unsigned char c : hook_id) { h ^= c; h *= 1099511628211ULL; }
  return DeterministicRng(s_ ^ h);
}

void InMemoryStore::put(const std::string& o, const std::string& k, const std::string& v) { rows_[{o, k}] = v; }
bool InMemoryStore::get(const std::string& o, const std::string& k, std::string* out) const {
  auto it = rows_.find({o, k});
  if (it == rows_.end()) return false;
  if (out) *out = it->second;
  return true;
}

void SimPlayer::send(const Packet&) { throw NotImplemented("SimPlayer::send (netproto codec set)"); }
std::vector<Packet> SimPlayer::drain_received() { throw NotImplemented("SimPlayer::drain_received"); }
void SimPlayer::cast(const std::string&, const std::string&) { throw NotImplemented("SimPlayer::cast"); }
void SimPlayer::select(int) { throw NotImplemented("SimPlayer::select"); }

ScenarioResult run_scenario(const std::string&) { throw NotImplemented("Luau scenario runner"); }

void CoverageReport::record(const std::string& id, RuleStatus s) { by_rule_[id] = s; }
RuleStatus CoverageReport::status(const std::string& id) const {
  auto it = by_rule_.find(id);
  return it == by_rule_.end() ? RuleStatus::Uncovered : it->second;
}
std::string CoverageReport::render() const {
  static const char* names[] = {"passing", "pending", "failing", "uncovered"};
  std::ostringstream o;
  for (const auto& [id, s] : by_rule_) o << id << "\t" << names[static_cast<int>(s)] << "\n";
  return o.str();
}

std::vector<GoldenTraceRecord> load_golden_trace(const std::string&) { throw NotImplemented("golden trace loader"); }
bool replay_golden_trace(const std::vector<GoldenTraceRecord>&, DbSet) { throw NotImplemented("golden trace replay"); }
void reload_ruleset_generation(const std::string&) { throw NotImplemented("reload generation in sim"); }

}  // namespace tooling::sim
