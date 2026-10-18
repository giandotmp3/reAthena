#include "mods/resolver.hpp"

#include <algorithm>
#include <functional>
#include <map>
#include <set>

#include "mods/common.hpp"

namespace mods {
namespace {


std::string q(const std::string& id) { return "'" + id + "'"; }

bool provides_cap(const Manifest& m, const std::string& cap) {
  return std::find(m.provides.begin(), m.provides.end(), cap) != m.provides.end();
}

// Does `a` declare a conflict with `b` (by id or by a capability b provides)?
const std::string* conflict_between(const Manifest& a, const Manifest& b) {
  for (const auto& c : a.conflicts)
    if (c == b.id || provides_cap(b, c)) return &c;
  return nullptr;
}

}  // namespace

bool mutually_exclusive(const Manifest& a, const Manifest& b) {
  return conflict_between(a, b) != nullptr || conflict_between(b, a) != nullptr;
}

std::vector<Manifest> resolve_load_order(const std::vector<Manifest>& mods, const ResolveOptions& opts) {
  // sorted view by id for deterministic diagnostics
  std::map<std::string, const Manifest*> by_id;
  for (const auto& m : mods)
    if (!by_id.emplace(m.id, &m).second)
      throw LoadError("resolve.duplicate_id", "mod " + q(m.id) + " is present more than once (versions " +
                                                  to_string(by_id[m.id]->version) + " and " + to_string(m.version) + ")");

  for (const auto& [id, m] : by_id) {
    if (!satisfies(opts.engine_api_version, m->engine_api))
      throw LoadError("resolve.engine_api", "mod " + q(id) + " requires engine_api \"" + m->engine_api +
                                                "\" but the engine provides " + to_string(opts.engine_api_version));
  }

  for (const auto& [id, m] : by_id) {
    for (const auto& [dep, range] : m->depends) {
      auto it = by_id.find(dep);
      if (it == by_id.end())
        throw LoadError("resolve.missing_dependency",
                        "mod " + q(id) + " depends on " + q(dep) + " (" + range + "), which is not installed or not enabled");
      if (!satisfies(it->second->version, range))
        throw LoadError("resolve.version_mismatch",
                        "mod " + q(id) + " requires " + q(dep) + " " + range + " but " + q(dep) + " " +
                            to_string(it->second->version) + " is installed");
    }
  }

  for (const auto& [id, m] : by_id)
    for (const auto& [oid, o] : by_id)
      if (id != oid)
        if (const std::string* c = conflict_between(*m, *o))
          throw LoadError("resolve.conflict", "mod " + q(id) + " declares a conflict with " + q(*c) +
                                                  (*c == oid ? std::string() : " (provided by " + q(oid) + ")") +
                                                  ", but " + q(oid) + " is enabled");

  for (const auto& cap : opts.exclusive_provides) {
    std::vector<std::string> ps;
    for (const auto& [id, m] : by_id) if (provides_cap(*m, cap)) ps.push_back(id);
    if (ps.size() > 1) {
      std::string list;
      for (std::size_t k = 0; k < ps.size(); ++k) list += (k ? ", " : "") + q(ps[k]);
      throw LoadError("resolve.exclusive_provider", "capability \"" + cap + "\" allows exactly one provider but " +
                                                        std::to_string(ps.size()) + " are enabled: " + list);
    }
  }
  for (const auto& cap : opts.required_provides) {
    bool any = false;
    for (const auto& [id, m] : by_id) any = any || provides_cap(*m, cap);
    if (!any)
      throw LoadError("resolve.missing_provider", "no enabled mod provides the required capability \"" + cap + "\"");
  }

  // Edge u -> v: u must load before v.
  std::map<std::string, std::map<std::string, std::string>> out, in;
  auto edge = [&](const std::string& u, const std::string& v, const std::string& why) {
    if (u == v || !by_id.count(u) || !by_id.count(v)) return;
    if (out[u].emplace(v, why).second) in[v].emplace(u, why);
  };
  for (const auto& [id, m] : by_id) {
    for (const auto& [dep, range] : m->depends) { (void)range; edge(dep, id, q(id) + " depends on " + q(dep)); }
    for (const auto& a : m->load_after) edge(a, id, q(id) + " load_after " + q(a));
    for (const auto& b : m->load_before) edge(id, b, q(id) + " load_before " + q(b));
  }

  std::map<std::string, std::size_t> indeg;
  for (const auto& [id, m] : by_id) { (void)m; indeg[id] = in[id].size(); }
  std::set<std::string> ready;
  for (const auto& [id, d] : indeg) if (d == 0) ready.insert(id);
  std::vector<std::string> order;
  while (!ready.empty()) {
    std::string u = *ready.begin();
    ready.erase(ready.begin());
    order.push_back(u);
    for (const auto& [v, why] : out[u]) { (void)why; if (--indeg[v] == 0) ready.insert(v); }
  }
  if (order.size() != by_id.size()) {
    std::set<std::string> done(order.begin(), order.end());
    std::string start;
    for (const auto& [id, m] : by_id) { (void)m; if (!done.count(id)) { start = id; break; } }
    // walk predecessors inside the unresolved set until a node repeats
    std::vector<std::string> walk;
    std::map<std::string, std::size_t> pos;
    std::string cur = start;
    while (!pos.count(cur)) {
      pos[cur] = walk.size();
      walk.push_back(cur);
      std::string next;
      for (const auto& [p, why] : in[cur]) { (void)why; if (!done.count(p)) { next = p; break; } }
      cur = next;
    }
    std::vector<std::string> cyc(walk.begin() + static_cast<long>(pos[cur]), walk.end());
    std::reverse(cyc.begin(), cyc.end());  // forward direction: each loads before the next
    auto mn = std::min_element(cyc.begin(), cyc.end());
    std::rotate(cyc.begin(), mn, cyc.end());
    std::string path, why;
    for (std::size_t k = 0; k < cyc.size(); ++k) {
      const std::string& u = cyc[k];
      const std::string& v = cyc[(k + 1) % cyc.size()];
      path += u + " -> ";
      why += (k ? "; " : "") + out[u][v];
    }
    path += cyc.front();
    throw LoadError("resolve.cycle", "load order cycle: " + path + " (each mod must load before the next: " + why + ")");
  }

  std::vector<Manifest> result;
  for (const auto& id : order) result.push_back(*by_id[id]);
  return result;
}

// ---- version solving ---------------------------------------------------------
namespace {

struct Solver {
  const std::map<std::string, std::vector<const Manifest*>>& cands;  // sorted by version desc
  const ResolveOptions& opts;
  std::map<std::string, const Manifest*> selected;
  std::map<std::string, std::vector<std::pair<std::string, std::string>>> constraints;  // id -> (requirer, range)
  std::string best_code = "resolve.missing_dependency", best_msg;
  std::size_t best_depth = 0;

  void note(std::size_t depth, const char* code, std::string msg) {
    if (best_msg.empty() || depth >= best_depth) { best_depth = depth; best_code = code; best_msg = std::move(msg); }
  }

  // Returns an empty string if `c` can join the current selection.
  bool admissible(const Manifest& c, std::size_t depth) {
    if (!satisfies(opts.engine_api_version, c.engine_api)) {
      note(depth, "resolve.engine_api", "mod " + q(c.id) + " " + to_string(c.version) + " requires engine_api \"" + c.engine_api +
                                            "\" but the engine provides " + to_string(opts.engine_api_version));
      return false;
    }
    for (const auto& [requirer, range] : constraints[c.id])
      if (!satisfies(c.version, range)) {
        note(depth, "resolve.version_mismatch", "mod " + q(requirer) + " requires " + q(c.id) + " " + range + " but " + q(c.id) +
                                                    " " + to_string(c.version) + " does not satisfy it");
        return false;
      }
    for (const auto& [id, s] : selected) {
      if (const std::string* x = conflict_between(*s, c)) {
        note(depth, "resolve.conflict", "mod " + q(id) + " declares a conflict with " + q(*x) + ", but " + q(c.id) + " is needed");
        return false;
      }
      if (const std::string* x = conflict_between(c, *s)) {
        note(depth, "resolve.conflict", "mod " + q(c.id) + " declares a conflict with " + q(*x) + ", but " + q(id) + " is needed");
        return false;
      }
      for (const auto& cap : opts.exclusive_provides)
        if (provides_cap(c, cap) && provides_cap(*s, cap)) {
          note(depth, "resolve.exclusive_provider", "capability \"" + cap + "\" allows exactly one provider but " + q(id) + " and " +
                                                         q(c.id) + " are both needed");
          return false;
        }
    }
    for (const auto& [dep, range] : c.depends) {
      auto it = selected.find(dep);
      if (it != selected.end() && !satisfies(it->second->version, range)) {
        note(depth, "resolve.version_mismatch", "mod " + q(c.id) + " requires " + q(dep) + " " + range + " but " + q(dep) + " " +
                                                    to_string(it->second->version) + " is already chosen");
        return false;
      }
      if (!cands.count(dep)) {
        note(depth, "resolve.missing_dependency", "mod " + q(c.id) + " depends on " + q(dep) + " (" + range + "), which is not installed");
        return false;
      }
    }
    return true;
  }

  bool solve(std::set<std::string> pending, std::size_t depth) {
    std::string id;
    for (const auto& p : pending) if (!selected.count(p)) { id = p; break; }
    if (id.empty()) return true;
    auto it = cands.find(id);
    if (it == cands.end()) { note(depth, "resolve.missing_dependency", "mod " + q(id) + " is not installed"); return false; }
    for (const Manifest* c : it->second) {
      if (!admissible(*c, depth)) continue;
      selected[id] = c;
      for (const auto& [dep, range] : c->depends) constraints[dep].emplace_back(id, range);
      auto next = pending;
      for (const auto& [dep, range] : c->depends) { (void)range; next.insert(dep); }
      if (solve(next, depth + 1)) return true;
      for (const auto& [dep, range] : c->depends) { (void)range; constraints[dep].pop_back(); }
      selected.erase(id);
    }
    return false;
  }
};

}  // namespace

std::vector<Manifest> select_versions(const std::vector<Manifest>& available, const std::vector<std::string>& roots,
                                      const ResolveOptions& opts) {
  std::map<std::string, std::vector<const Manifest*>> cands;
  for (const auto& m : available) cands[m.id].push_back(&m);
  for (auto& [id, v] : cands) {
    (void)id;
    std::sort(v.begin(), v.end(), [](const Manifest* a, const Manifest* b) { return a->version > b->version; });
  }
  Solver s{cands, opts, {}, {}, "resolve.missing_dependency", {}, 0};
  std::set<std::string> pending(roots.begin(), roots.end());
  if (!s.solve(pending, 0)) throw LoadError(s.best_code, s.best_msg.empty() ? "no consistent set of versions exists" : s.best_msg);
  std::vector<Manifest> out;
  for (const auto& [id, m] : s.selected) { (void)id; out.push_back(*m); }
  return out;
}

}  // namespace mods
