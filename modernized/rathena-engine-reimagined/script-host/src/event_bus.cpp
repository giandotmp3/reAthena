#include "script_host/event_bus.hpp"

#include <algorithm>
#include <functional>
#include <queue>
#include <tuple>

#include "script_host/numeric.hpp"

namespace script_host {

namespace {

int phase_rank(Phase p) { return p == Phase::Pre ? 0 : p == Phase::On ? 1 : 2; }
const char* phase_name(Phase p) { return p == Phase::Pre ? "pre" : p == Phase::On ? "on" : "post"; }

std::string qualify(const HookSpec& owner, const std::string& ref) {
  return ref.find('/') == std::string::npos ? owner.mod + "/" + ref : ref;
}

}  // namespace

EventBus::EventBus(BudgetLimits l) : strikes_(l) {}

void EventBus::add(HookSpec spec, HookFn fn) {
  entries_.push_back(std::make_shared<const Entry>(Entry{std::move(spec), std::move(fn), next_seq_++}));
  cache_.clear();
}

void EventBus::mark_protected(const std::string& event) { protected_.insert(event); cache_.clear(); }
void EventBus::disable_hook(const std::string& id) { disabled_.insert(id); cache_.clear(); }
void EventBus::enable_hook(const std::string& id) { disabled_.erase(id); cache_.clear(); }

std::size_t EventBus::remove_generation(const std::string& mod, std::uint64_t generation) {
  auto before = entries_.size();
  entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                [&](const EntryPtr& e) { return e->spec.mod == mod && e->spec.generation == generation; }),
                 entries_.end());
  cache_.clear();
  return before - entries_.size();
}

std::size_t EventBus::count(const std::string& mod, std::uint64_t generation) const {
  return static_cast<std::size_t>(std::count_if(entries_.begin(), entries_.end(), [&](const EntryPtr& e) {
    return e->spec.mod == mod && e->spec.generation == generation;
  }));
}

// Builds the full resolved order (all hooks of the event, including disabled
// ones for conflict detection), then returns only the enabled ones.
std::vector<EventBus::EntryPtr> EventBus::plan(const std::string& event) const {
  auto cached = cache_.find(event);
  if (cached != cache_.end()) return cached->second;

  std::vector<EntryPtr> all;
  for (auto& e : entries_)
    if (e->spec.event == event) all.push_back(e);

  // Implicit load order = index of the mod's first registration on the bus.
  std::map<std::string, int> first_seen;
  for (auto& e : entries_) first_seen.emplace(e->spec.mod, static_cast<int>(first_seen.size()));
  auto lo = [&](const std::string& mod) {
    auto it = load_order_.find(mod);
    return it != load_order_.end() ? it->second : (1 << 20) + first_seen[mod];
  };

  std::map<std::string, std::size_t> by_id;  // qualified -> index in `all`
  for (std::size_t i = 0; i < all.size(); ++i) by_id.emplace(all[i]->spec.qualified(), i);

  auto key = [&](std::size_t i) { return std::make_pair(phase_rank(all[i]->spec.phase), all[i]->spec.priority); };
  std::vector<std::vector<std::size_t>> succ(all.size());
  std::vector<int> indeg(all.size(), 0);
  auto add_edge = [&](std::size_t from, std::size_t to) {  // from runs before to
    succ[from].push_back(to);
    ++indeg[to];
  };
  auto contradiction = [&](std::size_t a, std::size_t b, const char* rel) {
    const auto& A = all[a]->spec;
    const auto& B = all[b]->spec;
    throw LoadError("ordering conflict on " + event + ": hook " + A.qualified() + " (" + phase_name(A.phase) + ", priority " +
                    std::to_string(A.priority) + ") declares '" + rel + " " + B.qualified() + "' (" + phase_name(B.phase) +
                    ", priority " + std::to_string(B.priority) + ") which contradicts priority order");
  };
  for (std::size_t i = 0; i < all.size(); ++i) {
    const auto& s = all[i]->spec;
    for (auto& r : s.after) {
      auto it = by_id.find(qualify(s, r));
      if (it == by_id.end() || it->second == i) continue;
      if (key(it->second) > key(i)) contradiction(i, it->second, "after");
      if (key(it->second) == key(i)) add_edge(it->second, i);
    }
    for (auto& r : s.before) {
      auto it = by_id.find(qualify(s, r));
      if (it == by_id.end() || it->second == i) continue;
      if (key(it->second) < key(i)) contradiction(i, it->second, "before");
      if (key(it->second) == key(i)) add_edge(i, it->second);
    }
  }

  // Kahn per (phase, priority) group; ties broken by (load order, seq).
  std::vector<std::size_t> idx(all.size());
  for (std::size_t i = 0; i < idx.size(); ++i) idx[i] = i;
  auto tie = [&](std::size_t a, std::size_t b) {
    return std::make_tuple(lo(all[a]->spec.mod), all[a]->seq) < std::make_tuple(lo(all[b]->spec.mod), all[b]->seq);
  };
  std::vector<std::size_t> ordered;
  std::vector<bool> done(all.size(), false);
  std::vector<std::size_t> sorted = idx;
  std::stable_sort(sorted.begin(), sorted.end(), [&](std::size_t a, std::size_t b) {
    if (key(a) != key(b)) return key(a) < key(b);
    return tie(a, b);
  });
  for (std::size_t g = 0; g < sorted.size();) {
    std::size_t h = g;
    while (h < sorted.size() && key(sorted[h]) == key(sorted[g])) ++h;
    auto cmp = [&](std::size_t a, std::size_t b) { return tie(b, a); };  // min-heap on tie order
    std::priority_queue<std::size_t, std::vector<std::size_t>, decltype(cmp)> ready(cmp);
    for (std::size_t k = g; k < h; ++k)
      if (indeg[sorted[k]] == 0) ready.push(sorted[k]);
    std::size_t emitted = 0;
    while (!ready.empty()) {
      auto n = ready.top();
      ready.pop();
      ordered.push_back(n);
      done[n] = true;
      ++emitted;
      for (auto m : succ[n])
        if (--indeg[m] == 0) ready.push(m);
    }
    if (emitted != h - g) {
      // Cycle: report the members of the first stuck node's cycle.
      std::size_t start = 0;
      for (std::size_t k = g; k < h; ++k)
        if (!done[sorted[k]]) { start = sorted[k]; break; }
      // Walk predecessor links among unfinished nodes until a repeat.
      std::vector<std::size_t> path;
      std::map<std::size_t, std::size_t> seen;
      std::size_t cur = start;
      while (!seen.count(cur)) {
        seen[cur] = path.size();
        path.push_back(cur);
        std::size_t next = cur;
        for (std::size_t k = g; k < h; ++k) {
          std::size_t p = sorted[k];
          if (done[p]) continue;
          if (std::find(succ[p].begin(), succ[p].end(), cur) != succ[p].end()) { next = p; break; }
        }
        if (next == cur) break;
        cur = next;
      }
      std::string names;
      for (std::size_t k = seen.count(cur) ? seen[cur] : 0; k < path.size(); ++k)
        names += (names.empty() ? "" : " <- ") + all[path[k]]->spec.qualified();
      throw LoadError("ordering cycle on " + event + " among hooks: " + names);
    }
    g = h;
  }

  std::vector<EntryPtr> out;
  for (auto i : ordered)
    if (!disabled_.count(all[i]->spec.qualified())) out.push_back(all[i]);
  cache_[event] = out;
  return out;
}

void EventBus::validate() const {
  std::set<std::string> events;
  for (auto& e : entries_) events.insert(e->spec.event);
  for (auto& ev : events) {
    plan(ev);  // ordering conflicts and cycles throw here
    std::map<std::string, const HookSpec*> ids;
    std::map<int, std::vector<const HookSpec*>> replacers;  // by phase rank
    for (auto& e : entries_) {
      const auto& s = e->spec;
      if (s.event != ev) continue;
      auto ins = ids.emplace(s.qualified(), &s);
      if (!ins.second) throw LoadError("duplicate hook " + s.qualified() + " registered twice on " + ev);
      if (s.draws_main_rng && strict_parity_)
        throw LoadError("strict_parity: hook " + s.qualified() + " on " + ev + " draws from the main RNG stream");
      if (s.mode == Mode::Replace) {
        if (protected_.count(ev) && !s.permit_protected)
          throw LoadError("hook " + s.qualified() + " cannot replace protected stage " + ev + " without permission");
        replacers[phase_rank(s.phase)].push_back(&s);
      }
    }
    for (auto& [rank, v] : replacers) {
      if (v.size() < 2) continue;
      std::vector<const HookSpec*> with_prec;
      for (auto* s : v)
        if (s->precedence) with_prec.push_back(s);
      if (with_prec.size() == 1) continue;
      const HookSpec* a = with_prec.size() >= 2 ? with_prec[0] : v[0];
      const HookSpec* b = with_prec.size() >= 2 ? with_prec[1] : v[1];
      throw LoadError("replace conflict on " + ev + " (" + phase_name(a->phase) + "): " + a->qualified() + " and " +
                      b->qualified() + " both replace this stage and " +
                      (with_prec.size() >= 2 ? "both declare precedence" : "neither declares precedence"));
    }
  }
}

std::vector<std::string> EventBus::order(const std::string& event) const {
  std::vector<std::string> out;
  for (auto& e : plan(event)) out.push_back(e->spec.qualified());
  return out;
}

DispatchResult EventBus::dispatch(const std::string& event, HookContext& ctx) {
  DispatchResult res;
  auto hooks = plan(event);

  EffectQueue local;
  EffectQueue* saved_queue = ctx.effects;
  if (!ctx.effects) ctx.effects = &local;
  EffectQueue& q = *ctx.effects;
  const std::size_t base = q.size();
  const bool is_protected = protected_.count(event) != 0;
  ctx.cancel_reason.reset();

  // Replace winner per phase: the only replacer, or the one declaring precedence.
  std::map<int, const Entry*> winner;
  for (auto& e : hooks) {
    if (e->spec.mode != Mode::Replace) continue;
    if (is_protected && !e->spec.permit_protected) continue;
    auto& w = winner[phase_rank(e->spec.phase)];
    if (!w || (e->spec.precedence && !w->spec.precedence)) w = e.get();
  }

  auto restore = [&](const HookSpec* saved_hook, bool saved_ro) {
    ctx.hook = saved_hook;
    ctx.read_only = saved_ro;
    ctx.effects = saved_queue;
  };
  const HookSpec* saved_hook = ctx.hook;
  bool saved_ro = ctx.read_only;

  for (int rank = 0; rank < 3 && !res.cancelled; ++rank) {
    for (auto& e : hooks) {
      const HookSpec& s = e->spec;
      if (phase_rank(s.phase) != rank) continue;
      if (s.mode == Mode::Replace) {
        if (is_protected && !s.permit_protected) {
          res.diagnostics.push_back("replace by " + s.qualified() + " ignored: " + event + " is protected");
          continue;
        }
        if (winner[rank] != e.get()) {
          res.diagnostics.push_back("replace by " + s.qualified() + " skipped: " + winner[rank]->spec.qualified() + " has precedence");
          continue;
        }
        res.replaced_by = s.qualified();
      }

      const std::size_t eff_before = q.size();
      const std::int64_t num_before = ctx.num.value();
      ctx.hook = &s;
      ctx.read_only = s.mode == Mode::Observe;
      res.ran.push_back(s.qualified());
      bool failed = false;
      try {
        e->fn(ctx);
      } catch (const BudgetExceeded& ex) {
        failed = true;
        res.errors.push_back({s.qualified(), "budget", ex.what()});
        if (strikes_.strike(s.qualified(), tick_)) {
          disable_hook(s.qualified());
          res.auto_disabled.push_back(s.qualified());
        }
      } catch (const HookFailure& ex) {
        failed = true;
        const char* k = ex.kind == HookFailure::Kind::Budget ? "budget"
                        : ex.kind == HookFailure::Kind::Memory ? "memory"
                        : ex.kind == HookFailure::Kind::Permission ? "permission" : "error";
        res.errors.push_back({s.qualified(), k, ex.what()});
        if (ex.kind == HookFailure::Kind::Budget && strikes_.strike(s.qualified(), tick_)) {
          disable_hook(s.qualified());
          res.auto_disabled.push_back(s.qualified());
        }
        if (ex.kind == HookFailure::Kind::Memory) {
          disable_hook(s.qualified());
          res.auto_disabled.push_back(s.qualified());
        }
      } catch (const std::exception& ex) {
        failed = true;
        res.errors.push_back({s.qualified(), "error", ex.what()});
      }
      ctx.read_only = false;
      if (failed) {
        q.truncate(eff_before);  // a failed hook is rolled back
        ctx.num.set(num_before);
        ctx.cancel_reason.reset();
        continue;
      }
      if (s.mode == Mode::Observe && ctx.num.value() != num_before) {
        ctx.num.set(num_before);
        res.diagnostics.push_back("observe hook " + s.qualified() + " changed ctx; change discarded");
      }
      if (ctx.cancel_reason) {
        if (s.phase != Phase::Pre) {
          res.diagnostics.push_back("cancel by " + s.qualified() + " ignored: only pre hooks may cancel");
          ctx.cancel_reason.reset();
        } else if (is_protected && !s.permit_protected) {
          res.diagnostics.push_back("cancel by " + s.qualified() + " ignored: " + event + " is protected");
          ctx.cancel_reason.reset();
        } else {
          res.cancelled = true;
          res.cancel_reason = *ctx.cancel_reason;
          res.cancelled_by = s.qualified();
          q.truncate(base);  // transactional: this dispatch's effects vanish
          break;
        }
      }
    }
  }

  if (!res.cancelled) res.committed_effects = q.take_from(base);
  restore(saved_hook, saved_ro);
  return res;
}

}  // namespace script_host
