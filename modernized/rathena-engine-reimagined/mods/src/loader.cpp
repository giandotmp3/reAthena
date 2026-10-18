#include "mods/loader.hpp"

#include <algorithm>

#include "mods/common.hpp"
#include "mods/sha256.hpp"

namespace mods {
namespace {
using Clock = std::chrono::steady_clock;
std::chrono::microseconds since(Clock::time_point t) {
  return std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - t);
}
}  // namespace

std::string manifest_fingerprint(const Manifest& m) {
  std::string s = "manifest-fingerprint-v1\n" + m.id + "\n" + to_string(m.version) + "\n" + m.engine_api + "\n";
  for (const auto& [d, r] : m.depends) s += "dep " + d + " " + r + "\n";
  for (const auto& c : m.conflicts) s += "conflict " + c + "\n";
  for (const auto& c : m.provides) s += "provides " + c + "\n";
  for (const auto& c : m.load_after) s += "after " + c + "\n";
  for (const auto& c : m.load_before) s += "before " + c + "\n";
  std::vector<std::string> perms;
  for (const auto& p : m.permissions) perms.push_back(p.str());
  std::sort(perms.begin(), perms.end());
  for (const auto& p : perms) s += "perm " + p + "\n";
  return "sha256:" + sha256_hex(s);
}

ModLoader::ModLoader(IdMap& ids, ModObserver* observer) : ids_(ids), observer_(observer) {}

void ModLoader::emit(ModEvent::Kind k, const std::string& mod, const std::string& detail,
                     std::chrono::microseconds el) const {
  if (observer_) observer_->on_event(ModEvent{k, mod, detail, el});
}

void ModLoader::load(const LoadRequest& req) {
  auto refuse = [&](const std::string& mod, const LoadError& e) {
    emit(ModEvent::Kind::Refused, mod, e.what());
  };
  const auto t_total = Clock::now();
  std::vector<Manifest> order;
  try {
    order = resolve_load_order(req.manifests, req.resolve);
  } catch (const LoadError& e) {
    refuse({}, e);
    throw;
  }

  // authorization (signatures, pins, permissions) for every mod before anything runs
  std::map<std::string, std::string> digest;
  std::map<std::string, std::set<Permission>> grants;
  for (const auto& m : order) {
    auto d = req.digests.find(m.id);
    digest[m.id] = d != req.digests.end() ? d->second : manifest_fingerprint(m);
    try {
      authorize(m, req.lock, req.verifier, digest[m.id]);
    } catch (const LoadError& e) {
      refuse(m.id, e);
      throw;
    }
    grants[m.id] = effective_grants(m, req.lock);
  }

  // ids: all explicit claims first (so allocation cannot take a number another
  // mod declares), then allocations, on a scratch copy.
  IdMap scratch = ids_;
  scratch.set_exclusivity([&req](const std::string& a, const std::string& b) {
    // a present mod's `conflicts` is enough: the other side may be absent (it was in an earlier generation)
    for (const auto& m : req.manifests) {
      const bool is_a = m.id == a, is_b = m.id == b;
      if (is_a && std::find(m.conflicts.begin(), m.conflicts.end(), b) != m.conflicts.end()) return true;
      if (is_b && std::find(m.conflicts.begin(), m.conflicts.end(), a) != m.conflicts.end()) return true;
    }
    return false;
  });
  try {
    std::set<std::string> present;
    for (const auto& m : order) present.insert(m.id);
    for (const auto& d : req.definitions) {
      if (!present.count(d.id.mod))
        throw LoadError("id.unknown_mod", "definition '" + d.id.str() + "' belongs to mod '" + d.id.mod + "', which is not part of this generation");
    }
    for (const auto& d : req.definitions) if (d.client_id) scratch.claim(d.id, *d.client_id);
    for (const auto& d : req.definitions) if (!d.client_id) scratch.allocate(d.id);
  } catch (const LoadError& e) {
    refuse({}, e);
    throw;
  }

  // mods that stay switched off are not run
  std::set<std::string> off = disabled_;
  for (bool grew = true; grew;) {
    grew = false;
    for (const auto& m : order) {
      if (off.count(m.id)) continue;
      for (const auto& [dep, r] : m.depends) { (void)r; if (off.count(dep)) { off.insert(m.id); grew = true; break; } }
    }
  }

  std::map<std::string, ModStats> new_stats;
  const unsigned gen = generation_ + 1;
  if (activator_) {
    std::unique_ptr<ActivationSession> session;
    std::string current;
    try {
      session = activator_->begin();
      for (const auto& m : order) {
        current = m.id;
        ActivationInfo info;
        info.manifest = &m;
        info.granted = grants[m.id];
        if (auto p = req.packages.find(m.id); p != req.packages.end()) info.package_dir = p->second;
        info.digest = digest[m.id];
        if (off.count(m.id)) { session->skip(info); continue; }
        const auto t0 = Clock::now();
        session->activate(info);
        new_stats[m.id].load_time = since(t0);
      }
      current.clear();
      session->commit();
    } catch (const LoadError& e) {
      refuse(current, e);
      throw;
    } catch (const std::exception& e) {
      LoadError le("activate.failed", "mod '" + current + "' failed to activate: " + e.what());
      refuse(current, le);
      throw le;
    }
  }

  // commit
  scratch.set_exclusivity({});
  ids_ = std::move(scratch);
  active_ = std::move(order);
  ++generation_;
  for (auto& [id, s] : new_stats) s.loaded_in_generation = gen;
  stats_ = std::move(new_stats);
  recompute_cascade();
  for (const auto& m : active_) {
    if (disabled_.count(m.id) || cascaded_.count(m.id)) continue;
    emit(ModEvent::Kind::Loaded, m.id, {}, stats_[m.id].load_time);
  }
  emit(ModEvent::Kind::GenerationSwapped, {}, "generation " + std::to_string(generation_), since(t_total));
}

void ModLoader::recompute_cascade() {
  cascaded_.clear();
  for (bool grew = true; grew;) {
    grew = false;
    for (const auto& m : active_) {
      if (disabled_.count(m.id) || cascaded_.count(m.id)) continue;
      for (const auto& [dep, r] : m.depends) {
        (void)r;
        if (disabled_.count(dep) || cascaded_.count(dep)) { cascaded_[m.id] = dep; grew = true; break; }
      }
    }
  }
}

void ModLoader::disable(const std::string& mod_id) {
  bool known = std::any_of(active_.begin(), active_.end(), [&](const Manifest& m) { return m.id == mod_id; });
  if (!known) throw LoadError("ops.unknown_mod", "cannot disable '" + mod_id + "': no such mod in the active generation");
  auto before = [&](const std::string& id) { return disabled_.count(id) || cascaded_.count(id); };
  std::vector<std::string> was;
  for (const auto& m : active_) if (before(m.id)) was.push_back(m.id);
  disabled_.insert(mod_id);
  recompute_cascade();
  for (const auto& m : active_) {
    if (std::find(was.begin(), was.end(), m.id) != was.end()) continue;
    if (!before(m.id)) continue;
    std::string note = activator_ ? activator_->deactivate(m.id) : std::string();
    std::string why = m.id == mod_id ? "operator kill switch" : "dependency '" + cascaded_[m.id] + "' is disabled";
    emit(ModEvent::Kind::Disabled, m.id, why + (note.empty() ? std::string() : "; " + note));
  }
}

void ModLoader::enable(const std::string& mod_id) {
  bool known = std::any_of(active_.begin(), active_.end(), [&](const Manifest& m) { return m.id == mod_id; });
  if (!known) throw LoadError("ops.unknown_mod", "cannot enable '" + mod_id + "': no such mod in the active generation");
  std::vector<std::string> was;
  for (const auto& m : active_) if (disabled_.count(m.id) || cascaded_.count(m.id)) was.push_back(m.id);
  disabled_.erase(mod_id);
  recompute_cascade();
  // re-activate in load order so dependencies come back first
  for (const auto& m : active_) {
    if (std::find(was.begin(), was.end(), m.id) == was.end()) continue;
    if (disabled_.count(m.id) || cascaded_.count(m.id)) continue;
    if (activator_) activator_->reactivate(m.id);
    emit(ModEvent::Kind::Enabled, m.id);
  }
}

ModState ModLoader::state(const std::string& mod_id) const {
  bool known = std::any_of(active_.begin(), active_.end(), [&](const Manifest& m) { return m.id == mod_id; });
  if (!known) return ModState::Invalid;
  return disabled_.count(mod_id) || cascaded_.count(mod_id) ? ModState::Disabled : ModState::Active;
}

std::string ModLoader::disabled_reason(const std::string& mod_id) const {
  if (disabled_.count(mod_id)) return "operator";
  if (auto it = cascaded_.find(mod_id); it != cascaded_.end()) return "dependency " + it->second + " is disabled";
  return {};
}

std::vector<std::string> ModLoader::load_order() const {
  std::vector<std::string> out;
  for (const auto& m : active_) out.push_back(m.id);
  return out;
}

ModStats ModLoader::stats(const std::string& mod_id) const {
  ModStats s;
  if (auto it = stats_.find(mod_id); it != stats_.end()) s = it->second;
  if (activator_) {
    ActivatorStats a = activator_->stats(mod_id);
    s.memory_used = a.memory_used;
    s.memory_peak = a.memory_peak;
    s.hook_calls = a.hook_calls;
    s.hook_errors = a.hook_errors;
    s.hook_time = a.hook_time;
  }
  return s;
}

}  // namespace mods
