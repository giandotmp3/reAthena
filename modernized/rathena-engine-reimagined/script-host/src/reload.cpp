#include "script_host/reload.hpp"

#include <algorithm>
#include <tuple>

#include "script_host/numeric.hpp"

namespace script_host {

struct ReloadManager::Generation {
  std::uint64_t number = 0;
  std::shared_ptr<ModSandbox> sandbox;
  std::uint64_t retired_at = 0;
  std::map<std::uint64_t, std::function<void()>> leases;  // coroutine leases -> cancel callbacks
  std::vector<std::string> shape;
};

struct ReloadManager::ModEntry {
  std::uint64_t active = 0;
  std::map<std::uint64_t, std::unique_ptr<Generation>> gens;  // active + retired still draining
};

ReloadManager::ReloadManager(ReloadConfig cfg) : cfg_(cfg) {}
ReloadManager::~ReloadManager() {
  // Sandboxes may outlive the manager (held by dialogues); detach their callbacks.
  for (auto& [m, e] : mods_)
    for (auto& [n, g] : e->gens)
      if (g->sandbox) g->sandbox->set_host({});
}

void ReloadManager::set_environment(ModEnvironmentSpec spec) { envs_[spec.mod_id] = std::move(spec); }

std::uint64_t ReloadManager::active_generation(const std::string& mod) const {
  auto it = mods_.find(mod);
  return it == mods_.end() ? 0 : it->second->active;
}

std::shared_ptr<ModSandbox> ReloadManager::active_sandbox(const std::string& mod) const {
  auto it = mods_.find(mod);
  if (it == mods_.end() || it->second->active == 0) return nullptr;
  return it->second->gens.at(it->second->active)->sandbox;
}

std::size_t ReloadManager::live_registrations(const std::string& mod, std::uint64_t generation) const {
  std::size_t n = bus_.count(mod, generation);
  for (auto& t : timers_)
    if (t.mod == mod && t.gen == generation) ++n;
  auto it = mods_.find(mod);
  if (it != mods_.end()) {
    auto g = it->second->gens.find(generation);
    if (g != it->second->gens.end()) n += g->second->leases.size();
  }
  return n;
}

std::uint64_t ReloadManager::schedule(const std::string& mod, std::uint64_t delay_ms, std::function<void()> fn) {
  std::uint64_t gen = active_generation(mod);
  if (gen == 0) return 0;
  std::uint64_t id = next_timer_++;
  timers_.push_back({id, now_ms_ + delay_ms, mod, gen, std::move(fn)});
  return id;
}

ReloadResult ReloadManager::reload(const std::string& mod, const std::string& new_source) {
  ReloadResult res;
  auto& entry = mods_[mod];
  if (!entry) entry = std::make_unique<ModEntry>();
  const std::uint64_t old_gen = entry->active;
  const std::uint64_t gen = old_gen + 1;
  res.active_generation = old_gen;

  ModEnvironmentSpec spec;
  if (auto e = envs_.find(mod); e != envs_.end()) spec = e->second;
  spec.mod_id = mod;

  auto sb = std::make_shared<ModSandbox>(spec);
  sb->set_storage(storage_);
  sb->set_rng(rng_);
  struct LoadState {
    std::vector<std::pair<std::uint64_t, int>> pending_timers;  // timers.after during the load
    bool committed = false;
  };
  auto ls = std::make_shared<LoadState>();
  sb->set_host({[this, mod, gen, ls, wsb = std::weak_ptr<ModSandbox>(sb)](std::uint64_t ms, int ref) {
                  if (!ls->committed) { ls->pending_timers.emplace_back(ms, ref); return; }
                  if (active_generation(mod) != gen) return;  // retired generations cannot start timers
                  auto sbp = wsb.lock();
                  if (!sbp) return;
                  schedule(mod, ms, [sbp, mod, ref] { sbp->call_ref(ref, mod + "/timer"); });
                },
                {}});

  RunResult r = sb->run(mod + ".luau", new_source);
  if (r.status != RunStatus::Ok) {
    res.error = r.error;
    sb->set_host({});
    return res;
  }
  auto hooks = sb->take_hooks();
  auto migrates = sb->take_migrates();
  auto shape = sb->take_shapes();

  // Definition shape changes need a drain / restart, reported precisely.
  if (old_gen != 0) {
    const auto& prev = entry->gens.at(old_gen)->shape;
    if (prev != shape) {
      std::string diff;
      for (auto& s : shape)
        if (std::find(prev.begin(), prev.end(), s) == prev.end()) diff += (diff.empty() ? "" : "; ") + std::string("changed/added: ") + s;
      for (auto& s : prev)
        if (std::find(shape.begin(), shape.end(), s) == shape.end()) diff += (diff.empty() ? "" : "; ") + std::string("removed/changed: ") + s;
      res.needs_restart = true;
      res.error = "definition shape of mod '" + mod + "' changed (" + diff + "); drain or restart required";
      sb->set_host({});
      return res;
    }
  }

  // Candidate bus: drop the old generation's hooks, add the new ones, validate.
  EventBus candidate = bus_;
  if (old_gen != 0) candidate.remove_generation(mod, old_gen);
  for (auto& h : hooks) {
    h.spec.generation = gen;
    int ref = h.ref;
    candidate.add(h.spec, [sb, ref](HookContext& c) { sb->call_hook(ref, c); });
  }
  if (rng_) candidate.set_strict_parity(rng_->strict_parity());  // one switch for RNG and load-time checks
  try {
    candidate.validate();
  } catch (const LoadError& e) {
    res.error = e.what();
    sb->set_host({});
    return res;
  }

  // ---- commit (atomic from the caller's view; nothing below can fail) --------
  bus_ = std::move(candidate);
  if (storage_)
    for (auto& m : migrates) {
      int ref = m.ref;
      storage_->declare_migrate(mod, m.current_version, [sb, ref, mod](std::uint32_t from, const Row& row) {
        Row out;
        RunResult rr = sb->call_migrate(ref, from, row, out);
        if (rr.status != RunStatus::Ok) throw StorageError(StorageError::Code::Corrupt, rr.error);
        return out;
      });
    }
  if (old_gen != 0) {
    auto& og = entry->gens.at(old_gen);
    og->retired_at = now_ms_;
    timers_.erase(std::remove_if(timers_.begin(), timers_.end(), [&](const Timer& t) { return t.mod == mod && t.gen == old_gen; }), timers_.end());
    if (og->leases.empty()) entry->gens.erase(old_gen);  // nothing left to drain
  }
  auto g = std::make_unique<Generation>();
  g->number = gen;
  g->sandbox = sb;
  g->shape = shape;
  entry->gens[gen] = std::move(g);
  entry->active = gen;
  ls->committed = true;
  for (auto& [ms, ref] : ls->pending_timers) {
    int rf = ref;
    schedule(mod, ms, [sb, mod, rf] { sb->call_ref(rf, mod + "/timer"); });
  }
  res.ok = true;
  res.active_generation = gen;
  return res;
}

std::optional<ReloadManager::Lease> ReloadManager::acquire(const std::string& mod, std::function<void()> on_cancel) {
  auto it = mods_.find(mod);
  if (it == mods_.end() || it->second->active == 0) return std::nullopt;
  auto& g = it->second->gens.at(it->second->active);
  Lease l{g->sandbox, g->number, next_lease_++};
  g->leases[l.id] = std::move(on_cancel);
  return l;
}

void ReloadManager::release(const std::string& mod, const Lease& l) {
  auto it = mods_.find(mod);
  if (it == mods_.end()) return;
  auto g = it->second->gens.find(l.generation);
  if (g == it->second->gens.end()) return;
  g->second->leases.erase(l.id);
  if (l.generation != it->second->active && g->second->leases.empty()) it->second->gens.erase(g);
}

void ReloadManager::advance_game_time(std::uint64_t ms) {
  now_ms_ += ms;
  // Timers: due ones fire in (due, id) order; a timer may schedule more.
  for (int guard = 0; guard < 100000; ++guard) {
    auto it = std::min_element(timers_.begin(), timers_.end(), [](const Timer& a, const Timer& b) {
      return std::tie(a.due_ms, a.id) < std::tie(b.due_ms, b.id);
    });
    if (it == timers_.end() || it->due_ms > now_ms_) break;
    auto fn = std::move(it->fn);
    timers_.erase(it);
    fn();
  }
  // Drain timeout: cancel coroutines still running in retired generations.
  for (auto& [m, e] : mods_) {
    std::vector<std::uint64_t> expired;
    for (auto& [n, g] : e->gens)
      if (n != e->active && now_ms_ - g->retired_at >= cfg_.drain_timeout_ms) expired.push_back(n);
    for (auto n : expired) {
      auto it = e->gens.find(n);
      if (it == e->gens.end()) continue;
      auto leases = std::move(it->second->leases);
      it->second->leases.clear();
      for (auto& [id, cancel] : leases)
        if (cancel) cancel();
      e->gens.erase(n);
    }
  }
}

}  // namespace script_host
