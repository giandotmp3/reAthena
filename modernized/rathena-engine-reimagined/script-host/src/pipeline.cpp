#include "script_host/pipeline.hpp"

#include <algorithm>
#include <tuple>

namespace script_host {

namespace {

const char* op_name(PipelineOpKind k) {
  switch (k) {
    case PipelineOpKind::Define: return "define";
    case PipelineOpKind::Stage: return "stage";
    case PipelineOpKind::InsertBefore: return "insert_before";
    case PipelineOpKind::InsertAfter: return "insert_after";
    case PipelineOpKind::Replace: return "replace";
    case PipelineOpKind::Cancel: return "cancel";
    case PipelineOpKind::Move: return "move";
  }
  return "?";
}

const char* mode_name(Mode m) { return m == Mode::Observe ? "observe" : m == Mode::Modify ? "modify" : "replace"; }

std::string join(const std::vector<std::string>& v, std::size_t limit = 12) {
  std::string s;
  for (std::size_t i = 0; i < v.size() && i < limit; ++i) s += (i ? ", " : "") + v[i];
  if (v.size() > limit) s += ", ...";
  return s;
}

}  // namespace

// ---- plan: the effective order of one pipeline -------------------------------------------------------------------
struct PipelineRegistry::Plan {
  struct Step {
    PipelineStepInfo info;
    PipelineBodyPtr body;
    bool same_state = false;  // the body runs in the defining mod's own state and receives `ctx`
    HookSpec spec;            // foreign execution (mod, id, mode)
  };
  std::string name, owner_mod;
  std::uint64_t generation = 0;
  const void* owner_key = nullptr;
  std::vector<Step> steps;
  std::vector<std::string> ignored;
};

PipelineRegistry::PipelineRegistry(BudgetLimits l) : strikes_(l) {}

void PipelineRegistry::apply(const PipelineOp& op, const std::string& mod, std::uint64_t generation, PipelineBodyPtr body) {
  plans_.clear();
  if (op.kind == PipelineOpKind::Define) {
    if (op.pipeline.empty()) throw LoadError("pipeline.define: the pipeline name is empty");
    auto it = defs_.find(op.pipeline);
    if (it != defs_.end())
      throw LoadError("pipeline '" + op.pipeline + "' is defined twice: by mod '" + it->second.owner + "' and by mod '" + mod + "'");
    std::set<std::string> seen;
    for (auto& s : op.stages) {
      if (s.empty()) throw LoadError("pipeline.define('" + op.pipeline + "'): empty stage name in mod '" + mod + "'");
      if (!seen.insert(s).second) throw LoadError("pipeline.define('" + op.pipeline + "'): stage '" + s + "' listed twice by mod '" + mod + "'");
    }
    Definition d;
    d.name = op.pipeline;
    d.owner = mod;
    d.generation = generation;
    d.owner_key = body ? body->owner : nullptr;
    d.stages = op.stages;
    defs_.emplace(op.pipeline, std::move(d));
    return;
  }
  if (op.kind == PipelineOpKind::Stage) {
    auto it = defs_.find(op.pipeline);
    if (it == defs_.end())
      throw LoadError("pipeline.stage: pipeline '" + op.pipeline + "' is not defined (mod '" + mod + "' must call pipeline.define first)");
    Definition& d = it->second;
    if (d.owner != mod || d.generation != generation)
      throw LoadError("pipeline.stage: only the mod that defined pipeline '" + op.pipeline + "' ('" + d.owner + "') registers stage bodies; '" + mod +
                      "' must use insert_before, insert_after or replace");
    if (std::find(d.stages.begin(), d.stages.end(), op.stage) == d.stages.end())
      throw LoadError("pipeline.stage: '" + op.stage + "' is not a stage of pipeline '" + op.pipeline + "' (stages: " + join(d.stages) + ")");
    if (!d.bodies.emplace(op.stage, Definition::Reg{std::move(body), op.is_protected}).second)
      throw LoadError("pipeline.stage: stage '" + op.stage + "' of pipeline '" + op.pipeline + "' already has a body");
    return;
  }
  Edit e;
  e.op = op;
  e.mod = mod;
  e.generation = generation;
  e.body = std::move(body);
  e.seq = next_seq_++;
  edits_.push_back(std::move(e));
}

std::size_t PipelineRegistry::remove_generation(const std::string& mod, std::uint64_t generation) {
  plans_.clear();
  std::size_t n = 0;
  for (auto it = defs_.begin(); it != defs_.end();) {
    if (it->second.owner == mod && it->second.generation == generation) {
      n += 1 + it->second.bodies.size();
      it = defs_.erase(it);
    } else {
      ++it;
    }
  }
  auto before = edits_.size();
  edits_.erase(std::remove_if(edits_.begin(), edits_.end(), [&](const Edit& e) { return e.mod == mod && e.generation == generation; }), edits_.end());
  return n + (before - edits_.size());
}

std::size_t PipelineRegistry::count(const std::string& mod, std::uint64_t generation) const {
  std::size_t n = 0;
  for (auto& [name, d] : defs_)
    if (d.owner == mod && d.generation == generation) n += 1 + d.bodies.size();
  for (auto& e : edits_)
    if (e.mod == mod && e.generation == generation) ++n;
  return n;
}

std::vector<std::string> PipelineRegistry::names() const {
  std::vector<std::string> v;
  for (auto& [n, d] : defs_) v.push_back(n);
  return v;
}

std::string PipelineRegistry::owner_of(const std::string& name) const {
  auto it = defs_.find(name);
  return it == defs_.end() ? std::string() : it->second.owner;
}

// Computes the effective order. See the header for the rules; every failure is a LoadError naming the mods involved.
std::shared_ptr<const PipelineRegistry::Plan> PipelineRegistry::build(const std::string& name, const PipelineOptions& o) const {
  auto di = defs_.find(name);
  if (di == defs_.end()) throw LoadError("pipeline '" + name + "' is not defined");
  const Definition& d = di->second;

  auto lo = [&](const std::string& mod) {
    if (o.load_order) return o.load_order(mod);
    return 0;
  };

  struct Node {
    std::string id;
    bool base = false;
    int base_index = -1;
    std::string source;
    const Edit* inserted = nullptr;  // the insert edit that created this node
    bool is_protected = false;
    std::vector<PipelineEditNote> notes;
  };
  std::vector<Node> nodes;
  std::map<std::string, int> by_id;
  for (std::size_t i = 0; i < d.stages.size(); ++i) {
    Node n;
    n.id = d.stages[i];
    n.base = true;
    n.base_index = static_cast<int>(i);
    n.source = d.owner;
    auto r = d.bodies.find(n.id);
    n.is_protected = r != d.bodies.end() && r->second.is_protected;
    by_id.emplace(n.id, static_cast<int>(nodes.size()));
    nodes.push_back(std::move(n));
  }

  std::vector<const Edit*> es;
  for (auto& e : edits_)
    if (e.op.pipeline == name) es.push_back(&e);
  std::sort(es.begin(), es.end(), [](const Edit* a, const Edit* b) { return a->seq < b->seq; });

  Plan plan;
  plan.name = name;
  plan.owner_mod = d.owner;
  plan.generation = d.generation;
  plan.owner_key = d.owner_key;

  auto fail_missing = [&](const Edit& e, const std::string& what, const std::string& stage) {
    std::vector<std::string> known;
    for (auto& n : nodes) known.push_back(n.id);
    throw LoadError("mod '" + e.mod + "': " + op_name(e.op.kind) + " on pipeline '" + name + "' names " + what + " '" + stage +
                    "' which does not exist (stages: " + join(known) + ")");
  };

  // 1. inserted stages become nodes (any order of edits: anchors may be inserted stages of later edits)
  for (const Edit* e : es) {
    if (e->op.kind != PipelineOpKind::InsertBefore && e->op.kind != PipelineOpKind::InsertAfter) continue;
    const std::string& id = e->op.id;
    const std::string want = e->mod + ":";
    if (id.size() <= want.size() || id.compare(0, want.size(), want) != 0)
      throw LoadError("mod '" + e->mod + "': inserted stage id '" + id + "' on pipeline '" + name + "' must be namespaced '" + want + "<name>'");
    if (by_id.count(id)) {
      const Node& other = nodes[static_cast<std::size_t>(by_id[id])];
      throw LoadError("duplicate stage id '" + id + "' on pipeline '" + name + "': mod '" + e->mod + "' and " +
                      (other.base ? "the stage list of mod '" + d.owner + "'" : "mod '" + other.source + "'"));
    }
    Node n;
    n.id = id;
    n.source = e->mod;
    n.inserted = e;
    by_id.emplace(id, static_cast<int>(nodes.size()));
    nodes.push_back(std::move(n));
  }

  // 2. resolve every edit
  struct Resolved { const Edit* e; int target = -1; int anchor = -1; };
  std::vector<Resolved> inserts, replaces, cancels, moves;
  for (const Edit* e : es) {
    const PipelineOp& op = e->op;
    if (op.draws_main_rng && o.strict_parity)
      throw LoadError("strict_parity: stage hook " + e->mod + "/" + (op.id.empty() ? op.stage : op.id) + " on pipeline '" + name +
                      "' draws from the main RNG stream");
    auto find = [&](const std::string& s) { auto it = by_id.find(s); return it == by_id.end() ? -1 : it->second; };
    switch (op.kind) {
      case PipelineOpKind::InsertBefore:
      case PipelineOpKind::InsertAfter: {
        int a = find(op.stage);
        if (a < 0) {
          if (op.optional) {
            plan.ignored.push_back(e->mod + ": " + op_name(op.kind) + " '" + op.id + "' anchored on missing '" + op.stage + "'");
            by_id.erase(op.id);  // the node is dropped below
            continue;
          }
          fail_missing(*e, "anchor stage", op.stage);
        }
        inserts.push_back({e, find(op.id), a});
        break;
      }
      case PipelineOpKind::Replace:
      case PipelineOpKind::Cancel:
      case PipelineOpKind::Move: {
        int t = find(op.stage);
        if (t < 0) {
          if (op.optional) { plan.ignored.push_back(e->mod + ": " + op_name(op.kind) + " of missing stage '" + op.stage + "'"); continue; }
          fail_missing(*e, "stage", op.stage);
        }
        const Node& tn = nodes[static_cast<std::size_t>(t)];
        if (tn.is_protected && !op.permit_protected)
          throw LoadError("mod '" + e->mod + "' cannot " + op_name(op.kind) + " protected stage " + name + "/" + op.stage +
                          " without the protected-stage permission");
        if (op.kind == PipelineOpKind::Replace) {
          if (op.id.empty()) throw LoadError("mod '" + e->mod + "': replace of " + name + "/" + op.stage + " needs a hook id");
          replaces.push_back({e, t, -1});
        } else if (op.kind == PipelineOpKind::Cancel) {
          cancels.push_back({e, t, -1});
        } else {
          int a = find(op.anchor);
          if (a < 0) {
            if (op.optional) { plan.ignored.push_back(e->mod + ": move of '" + op.stage + "' next to missing '" + op.anchor + "'"); continue; }
            fail_missing(*e, "anchor stage", op.anchor);
          }
          if (a == t) throw LoadError("mod '" + e->mod + "': move of " + name + "/" + op.stage + " relative to itself");
          moves.push_back({e, t, a});
        }
        break;
      }
      default: break;
    }
  }

  // an inserted node whose edit was ignored has no entry in `inserts`: it never reached `by_id` (erased above) but still
  // sits in `nodes`; drop such nodes now (indices of the others are rebuilt)
  {
    std::set<const Edit*> live;
    for (auto& r : inserts) live.insert(r.e);
    bool dropped = false;
    for (auto& n : nodes)
      if (n.inserted && !live.count(n.inserted)) dropped = true;
    if (dropped) {
      std::vector<int> remap(nodes.size(), -1);
      std::vector<Node> kept;
      for (std::size_t i = 0; i < nodes.size(); ++i) {
        if (nodes[i].inserted && !live.count(nodes[i].inserted)) continue;
        remap[i] = static_cast<int>(kept.size());
        kept.push_back(std::move(nodes[i]));
      }
      nodes = std::move(kept);
      by_id.clear();
      for (std::size_t i = 0; i < nodes.size(); ++i) by_id.emplace(nodes[i].id, static_cast<int>(i));
      for (auto* v : {&inserts, &replaces, &cancels, &moves})
        for (auto& r : *v) {
          if (r.target >= 0) r.target = remap[static_cast<std::size_t>(r.target)];
          if (r.anchor >= 0) r.anchor = remap[static_cast<std::size_t>(r.anchor)];
        }
    }
  }

  // 3. conflicts per target stage
  auto pick = [&](std::vector<Resolved>& list, const char* what, std::vector<std::pair<const Edit*, const Edit*>>* skipped)
      -> std::map<int, const Resolved*> {
    std::map<int, std::vector<const Resolved*>> by_t;
    for (auto& r : list) by_t[r.target].push_back(&r);
    std::map<int, const Resolved*> win;
    for (auto& [t, v] : by_t) {
      if (v.size() == 1) { win[t] = v[0]; continue; }
      std::vector<const Resolved*> pr;
      for (auto* r : v)
        if (r->e->op.precedence) pr.push_back(r);
      const Node& tn = nodes[static_cast<std::size_t>(t)];
      if (pr.size() == 1) {
        win[t] = pr[0];
        for (auto* r : v)
          if (r != pr[0]) skipped->push_back({r->e, pr[0]->e});
        continue;
      }
      const Edit* a = pr.size() >= 2 ? pr[0]->e : v[0]->e;
      const Edit* b = pr.size() >= 2 ? pr[1]->e : v[1]->e;
      auto qual = [&](const Edit* e) { return e->mod + "/" + (e->op.id.empty() ? std::string(op_name(e->op.kind)) : e->op.id); };
      throw LoadError(std::string(what) + " conflict on " + name + "/" + tn.id + ": " + qual(a) + " and " + qual(b) + " both " + what +
                      " this stage and " + (pr.size() >= 2 ? "both declare precedence" : "neither declares precedence"));
    }
    return win;
  };
  std::vector<std::pair<const Edit*, const Edit*>> skipped_replace, skipped_move;
  auto rep_win = pick(replaces, "replace", &skipped_replace);
  auto mov_win = pick(moves, "move", &skipped_move);
  std::set<int> cancelled;
  for (auto& c : cancels) {
    cancelled.insert(c.target);
    if (rep_win.count(c.target)) {
      const Edit* r = rep_win[c.target]->e;
      throw LoadError("stage " + name + "/" + nodes[static_cast<std::size_t>(c.target)].id + " is cancelled by mod '" + c.e->mod + "' and replaced by " + r->mod + "/" +
                      r->op.id);
    }
  }

  // 4. placement: an inserted stage attaches to its anchor; a moved stage re-attaches to its new anchor
  struct Att { int node; bool before; int prio; int order; std::uint64_t seq; };
  std::vector<int> parent(nodes.size(), -1);
  std::vector<Att> att_of(nodes.size());
  std::vector<bool> placed(nodes.size(), false);
  for (auto& r : inserts) {
    parent[static_cast<std::size_t>(r.target)] = r.anchor;
    att_of[static_cast<std::size_t>(r.target)] = {r.target, r.e->op.kind == PipelineOpKind::InsertBefore, r.e->op.priority, lo(r.e->mod), r.e->seq};
    placed[static_cast<std::size_t>(r.target)] = true;
  }
  for (auto& [t, r] : mov_win) {
    parent[static_cast<std::size_t>(t)] = r->anchor;
    att_of[static_cast<std::size_t>(t)] = {t, r->e->op.before, r->e->op.priority, lo(r->e->mod), r->e->seq};
    placed[static_cast<std::size_t>(t)] = true;
  }
  std::vector<std::vector<Att>> before(nodes.size()), after(nodes.size());
  for (std::size_t i = 0; i < nodes.size(); ++i) {
    if (!placed[i]) continue;
    (att_of[i].before ? before : after)[static_cast<std::size_t>(parent[i])].push_back(att_of[i]);
  }
  auto att_less = [](const Att& a, const Att& b) { return std::tie(a.prio, a.order, a.seq) < std::tie(b.prio, b.order, b.seq); };
  for (auto& v : before) std::sort(v.begin(), v.end(), att_less);
  for (auto& v : after) std::sort(v.begin(), v.end(), att_less);

  std::vector<int> order;
  std::vector<bool> emitted(nodes.size(), false);
  std::function<void(int)> emit = [&](int n) {
    auto idx = static_cast<std::size_t>(n);
    if (emitted[idx]) return;
    emitted[idx] = true;
    for (auto& a : before[idx]) emit(a.node);
    order.push_back(n);
    for (auto& a : after[idx]) emit(a.node);
  };
  // `emitted` is set before the recursion so a cyclic attachment cannot loop; leftovers are reported below
  for (std::size_t i = 0; i < nodes.size(); ++i)
    if (nodes[i].base && !placed[i]) emit(static_cast<int>(i));
  if (order.size() != nodes.size()) {
    std::vector<std::string> stuck;
    for (std::size_t i = 0; i < nodes.size(); ++i)
      if (!emitted[i]) stuck.push_back(nodes[i].id);
    throw LoadError("ordering cycle on pipeline '" + name + "' among stages: " + join(stuck) +
                    " (each is placed relative to another one of the group)");
  }

  // 5. notes and bodies
  for (auto& r : inserts) {
    Node& n = nodes[static_cast<std::size_t>(r.target)];
    n.notes.push_back({op_name(r.e->op.kind), r.e->mod,
                       std::string(r.e->op.kind == PipelineOpKind::InsertBefore ? "before " : "after ") + nodes[static_cast<std::size_t>(r.anchor)].id +
                           ", priority " + std::to_string(r.e->op.priority) + ", " + mode_name(r.e->op.mode)});
  }
  for (auto& [t, r] : mov_win)
    nodes[static_cast<std::size_t>(t)].notes.push_back({"move", r->e->mod,
        std::string(r->e->op.before ? "before " : "after ") + nodes[static_cast<std::size_t>(r->anchor)].id + ", priority " + std::to_string(r->e->op.priority)});
  for (auto& [loser, winner] : skipped_move)
    nodes[static_cast<std::size_t>(std::find_if(moves.begin(), moves.end(), [&](const Resolved& r) { return r.e == loser; })->target)].notes.push_back(
        {"move_skipped", loser->mod, "precedence held by " + winner->mod});
  for (auto& [loser, winner] : skipped_replace)
    nodes[static_cast<std::size_t>(std::find_if(replaces.begin(), replaces.end(), [&](const Resolved& r) { return r.e == loser; })->target)].notes.push_back(
        {"replace_skipped", loser->mod, loser->op.id + " skipped, precedence held by " + winner->mod + "/" + winner->op.id});
  for (auto& c : cancels) nodes[static_cast<std::size_t>(c.target)].notes.push_back({"cancel", c.e->mod, ""});
  for (auto& [t, r] : rep_win)
    nodes[static_cast<std::size_t>(t)].notes.push_back({"replace", r->e->mod, "by " + r->e->op.id});

  for (int n : order) {
    const Node& nd = nodes[static_cast<std::size_t>(n)];
    Plan::Step s;
    PipelineStepInfo& in = s.info;
    in.id = nd.id;
    in.kind = nd.base ? "base" : "inserted";
    in.source = nd.source;
    in.protected_stage = nd.is_protected;
    in.active = !cancelled.count(n);
    in.edits = nd.notes;
    const Edit* src = nullptr;  // edit whose body runs, if not the definition's own
    if (auto r = rep_win.find(n); r != rep_win.end()) src = r->second->e;
    else if (nd.inserted) src = nd.inserted;
    if (src) {
      s.body = src->body;
      in.body_mod = src->mod;
      in.body_id = src->op.id;
      in.mode = src->op.kind == PipelineOpKind::Replace ? "replace" : mode_name(src->op.mode);
      in.priority = src->op.priority;
      s.spec.mod = src->mod;
      s.spec.id = src->op.id;
      s.spec.mode = src->op.kind == PipelineOpKind::Replace ? Mode::Replace : src->op.mode;
      s.spec.priority = src->op.priority;
      s.spec.permit_protected = src->op.permit_protected;
      s.spec.draws_main_rng = src->op.draws_main_rng;
      s.spec.generation = src->generation;
    } else if (nd.base) {
      auto r = d.bodies.find(nd.id);
      if (r != d.bodies.end()) {
        s.body = r->second.body;
        in.body_mod = d.owner;
        in.mode = "own";
        s.spec.mod = d.owner;
        s.spec.id = nd.id;
        s.spec.generation = d.generation;
      }
    }
    s.spec.event = "pipeline:" + name + "/" + nd.id;
    in.has_body = s.body != nullptr;
    s.same_state = s.body && s.body->owner == d.owner_key;
    plan.steps.push_back(std::move(s));
  }
  return std::make_shared<const Plan>(std::move(plan));
}

void PipelineRegistry::validate(const PipelineOptions& opts) {
  opts_ = opts;
  plans_.clear();
  // edits naming a pipeline nobody defines
  for (auto& e : edits_) {
    if (defs_.count(e.op.pipeline)) continue;
    if (e.op.optional) continue;
    throw LoadError("mod '" + e.mod + "': " + op_name(e.op.kind) + " names pipeline '" + e.op.pipeline + "' which is not defined (defined: " +
                    join(names()) + ")");
  }
  std::map<std::string, std::shared_ptr<const Plan>> fresh;
  for (auto& [n, d] : defs_) fresh[n] = build(n, opts);  // throws on any conflict; nothing is kept on failure
  plans_ = std::move(fresh);
}

std::shared_ptr<const PipelineRegistry::Plan> PipelineRegistry::plan_for(const std::string& name) const {
  auto it = plans_.find(name);
  if (it != plans_.end()) return it->second;
  if (!defs_.count(name)) return nullptr;
  auto p = build(name, opts_);
  plans_[name] = p;
  return p;
}

std::vector<std::string> PipelineRegistry::order(const std::string& name) const {
  std::vector<std::string> out;
  auto p = plan_for(name);
  if (!p) return out;
  for (auto& s : p->steps)
    if (s.info.active) out.push_back(s.info.id);
  return out;
}

PipelineInfo PipelineRegistry::describe(const std::string& name) const {
  auto p = plan_for(name);
  if (!p) throw LoadError("pipeline '" + name + "' is not defined");
  PipelineInfo info;
  info.name = p->name;
  info.owner_mod = p->owner_mod;
  info.generation = p->generation;
  for (auto& s : p->steps) info.steps.push_back(s.info);
  info.ignored = p->ignored;
  return info;
}

// ---- run -----------------------------------------------------------------------------------------------------------
void PipelineRegistry::run(const std::string& name, const void* caller, PipelineHost& host, PipelineRunResult& out) {
  auto p = plan_for(name);
  if (!p) throw LoadError("pipeline '" + name + "' is not defined");
  if (p->owner_key != caller)
    throw LoadError("pipeline '" + name + "' is run only by the mod that defined it ('" + p->owner_mod +
                    "'), or this state belongs to an older generation of it");

  HookContext hc;
  EffectQueue q;
  hc.effects = &q;
  std::shared_ptr<const Value> view;
  bool view_made = false;
  const auto& steps = p->steps;
  for (std::size_t i = 0; i < steps.size(); ++i) {
    const Plan::Step& s = steps[i];
    if (!s.info.active) continue;
    if (host.skip(s.info.id)) continue;
    if (!s.body) host.missing_body(s.info.id);
    if (s.same_state) {
      host.run_own(*s.body);
      continue;
    }
    // ---- a hook living in another mod's state
    const std::string qid = s.spec.qualified();
    if (disabled_.count(qid)) continue;
    if (!view_made) {
      view = host.view();
      view_made = true;
    }
    std::int64_t lane = 0;
    const bool has_lane = host.lane_get(lane);
    hc.num.set(has_lane ? lane : 0);
    hc.view = view;
    hc.hook = &s.spec;
    hc.read_only = s.spec.mode == Mode::Observe;
    hc.cancel_reason.reset();
    const std::size_t eff_before = q.size();
    const std::int64_t num_before = hc.num.value();
    out.ran.push_back(qid);
    bool failed = false;
    try {
      s.body->call(hc);
    } catch (const BudgetExceeded& ex) {
      failed = true;
      out.errors.push_back({qid, "budget", ex.what()});
      if (strikes_.strike(qid, tick_)) { disabled_.insert(qid); out.auto_disabled.push_back(qid); }
    } catch (const HookFailure& ex) {
      failed = true;
      const char* k = ex.kind == HookFailure::Kind::Budget ? "budget"
                      : ex.kind == HookFailure::Kind::Memory ? "memory"
                      : ex.kind == HookFailure::Kind::Permission ? "permission" : "error";
      out.errors.push_back({qid, k, ex.what()});
      if (ex.kind == HookFailure::Kind::Budget && strikes_.strike(qid, tick_)) { disabled_.insert(qid); out.auto_disabled.push_back(qid); }
      if (ex.kind == HookFailure::Kind::Memory) { disabled_.insert(qid); out.auto_disabled.push_back(qid); }
    } catch (const std::exception& ex) {
      failed = true;
      out.errors.push_back({qid, "error", ex.what()});
    }
    hc.read_only = false;
    if (failed) {  // a failed hook is rolled back: its effects, its lane change, its cancel
      q.truncate(eff_before);
      hc.cancel_reason.reset();
      continue;
    }
    if (s.spec.mode == Mode::Observe) {
      if (hc.num.value() != num_before) out.diagnostics.push_back("observe stage " + qid + " changed the lane; change discarded");
    } else if (has_lane && hc.num.value() != num_before) {
      host.lane_set(hc.num.value());
    }
    if (hc.cancel_reason) {
      // cancelling skips every later stage: refuse when that would skip a protected one without permission
      std::string prot;
      for (std::size_t j = i + 1; j < steps.size(); ++j)
        if (steps[j].info.active && steps[j].info.protected_stage) { prot = steps[j].info.id; break; }
      if (!prot.empty() && !s.spec.permit_protected) {
        out.diagnostics.push_back("cancel by " + qid + " ignored: it would skip protected stage " + name + "/" + prot);
        hc.cancel_reason.reset();
      } else {
        out.cancelled = true;
        out.cancel_reason = *hc.cancel_reason;
        out.cancelled_by = qid;
        q.truncate(0);  // transactional: the effects of this run vanish
        break;
      }
    }
  }
  if (!out.cancelled) out.effects = q.take_from(0);
}

}  // namespace script_host
