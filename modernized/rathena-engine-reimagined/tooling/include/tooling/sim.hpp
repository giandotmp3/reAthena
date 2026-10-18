#pragma once
// rathena-sim: headless harness. Links the engine with a virtual clock,
// deterministic RNG and in-memory persistence. SimPlayer speaks codec-level
// packets (no sockets). Scenarios are Luau. (Architecture section 8.)
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "tooling/common.hpp"

namespace tooling::sim {

// Time only moves when the test advances it. Timers fire in (due, seq) order.
class VirtualClock {
 public:
  int64_t now_ms() const { return now_; }
  uint64_t schedule(int64_t delay_ms, std::function<void()> fn);
  void advance(int64_t ms);  // fires due timers, in order
 private:
  struct T { int64_t due; uint64_t seq; std::function<void()> fn; };
  int64_t now_ = 0;
  uint64_t seq_ = 0;
  std::vector<T> timers_;
};

// Seeded stream that counts draws so tests can assert legacy draw counts
// (BR-RULESET-115). xorshift64*; the legacy generator is swapped in by the
// engine integration, this one is the harness default.
class DeterministicRng {
 public:
  explicit DeterministicRng(uint64_t seed) : s_(seed ? seed : 0x9E3779B97F4A7C15ULL) {}
  uint32_t next();
  int32_t rnd_range(int32_t lo, int32_t hi);   // inclusive; always draws once
  bool chance(int32_t rate, int32_t denom);    // always draws once, even if rate <= 0
  uint64_t draws() const { return draws_; }
  // Per-hook derived stream (architecture section 5, RNG).
  DeterministicRng derive(const std::string& hook_id) const;
 private:
  uint64_t s_;
  uint64_t draws_ = 0;
};

// In-memory persistence: opaque records keyed by (owner, kind).
class InMemoryStore {
 public:
  void put(const std::string& owner, const std::string& kind, const std::string& value);
  bool get(const std::string& owner, const std::string& kind, std::string* out) const;
  size_t size() const { return rows_.size(); }
 private:
  std::map<std::pair<std::string, std::string>, std::string> rows_;
};

struct PlayerSpec { std::string job; int level = 1; };
struct Packet { std::string name; std::vector<uint8_t> bytes; };

// Speaks codec-level packets to the engine under test.
class SimPlayer {
 public:
  explicit SimPlayer(PlayerSpec spec) : spec_(std::move(spec)) {}
  const PlayerSpec& spec() const { return spec_; }
  void send(const Packet& p);                         // stub
  std::vector<Packet> drain_received();               // stub
  void cast(const std::string& skill_id, const std::string& target);  // stub
  void select(int choice);                            // stub: NPC dialogue select
 private:
  PlayerSpec spec_;
};

// Loads and runs a Luau scenario file; returns the BR-ID tags it declared.
struct ScenarioResult { bool passed = false; std::vector<std::string> rule_tags; std::string message; };
ScenarioResult run_scenario(const std::string& luau_path);  // stub

// Coverage per behavior-contract rule ID.
enum class RuleStatus { Passing, Pending, Failing, Uncovered };
class CoverageReport {
 public:
  void record(const std::string& rule_id, RuleStatus s);
  RuleStatus status(const std::string& rule_id) const;
  // One line per rule: "<id>\t<status>", sorted by id.
  std::string render() const;
 private:
  std::map<std::string, RuleStatus> by_rule_;
};

// Golden-trace conformance (BR-RULESET-119).
struct GoldenTraceRecord { std::string rule_id; std::string inputs; std::string expected; };
std::vector<GoldenTraceRecord> load_golden_trace(const std::string& path);   // stub
bool replay_golden_trace(const std::vector<GoldenTraceRecord>& recs, DbSet set);  // stub

// Reload scenarios (architecture section 5, hot reload generations).
void reload_ruleset_generation(const std::string& mod_id);  // stub

}  // namespace tooling::sim
