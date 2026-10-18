// Pipeline runtime: effective order, edits, conflicts, protected stages, describe, run semantics.
// The first half drives PipelineRegistry directly (no Luau); the second half is the Luau API through ReloadManager.
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <random>

#include "fixtures.hpp"
#include "script_host/pipeline.hpp"

using namespace script_host;

namespace {

// ---- registry-level fixtures ---------------------------------------------------------------------------------------
int g_ruleset_key = 0, g_other_key = 0;

PipelineOp def(const std::string& name, std::vector<std::string> stages) {
  PipelineOp o;
  o.kind = PipelineOpKind::Define;
  o.pipeline = name;
  o.stages = std::move(stages);
  return o;
}
PipelineOp stg(const std::string& name, const std::string& stage, bool prot = false) {
  PipelineOp o;
  o.kind = PipelineOpKind::Stage;
  o.pipeline = name;
  o.stage = stage;
  o.is_protected = prot;
  return o;
}
PipelineOp ins(bool before, const std::string& name, const std::string& anchor, const std::string& id, int prio = 0, Mode m = Mode::Observe) {
  PipelineOp o;
  o.kind = before ? PipelineOpKind::InsertBefore : PipelineOpKind::InsertAfter;
  o.pipeline = name;
  o.stage = anchor;
  o.id = id;
  o.priority = prio;
  o.mode = m;
  return o;
}
PipelineOp rep(const std::string& name, const std::string& stage, const std::string& id, bool precedence = false) {
  PipelineOp o;
  o.kind = PipelineOpKind::Replace;
  o.pipeline = name;
  o.stage = stage;
  o.id = id;
  o.precedence = precedence;
  o.mode = Mode::Replace;
  return o;
}
PipelineOp cnl(const std::string& name, const std::string& stage) {
  PipelineOp o;
  o.kind = PipelineOpKind::Cancel;
  o.pipeline = name;
  o.stage = stage;
  return o;
}
PipelineOp mv(const std::string& name, const std::string& stage, const std::string& anchor, bool before, int prio = 0, bool precedence = false) {
  PipelineOp o;
  o.kind = PipelineOpKind::Move;
  o.pipeline = name;
  o.stage = stage;
  o.anchor = anchor;
  o.before = before;
  o.priority = prio;
  o.precedence = precedence;
  return o;
}

// A body that lives in "another state" (calls the std::function) or in the defining state (ref names it).
PipelineBodyPtr body_of(const void* owner, int ref, HookFn fn = {}) {
  auto b = std::make_shared<PipelineBody>();
  b->owner = owner;
  b->ref = ref;
  b->call = std::move(fn);
  return b;
}
PipelineBodyPtr owner_only(const void* owner) { return body_of(owner, 0); }

struct Rs {  // a ruleset-like fixture over a registry
  PipelineRegistry reg;
  std::vector<std::string> own_names{""};
  void define(const std::string& name, const std::vector<std::string>& stages, const std::string& mod = "rs", std::uint64_t gen = 1) {
    reg.apply(def(name, stages), mod, gen, owner_only(&g_ruleset_key));
    for (auto& s : stages) {
      own_names.push_back(name + "/" + s);
      reg.apply(stg(name, s), mod, gen, body_of(&g_ruleset_key, static_cast<int>(own_names.size()) - 1));
    }
  }
  PipelineBodyPtr foreign(const std::string& label, std::vector<std::string>* log = nullptr, std::function<void(HookContext&)> f = {}) {
    return body_of(&g_other_key, 1, [label, log, f](HookContext& c) {
      if (log) log->push_back(label);
      if (f) f(c);
    });
  }
};

std::string joined(const std::vector<std::string>& v) {
  std::string s;
  for (auto& x : v) s += (s.empty() ? "" : " ") + x;
  return s;
}

struct FakeHost final : PipelineHost {
  Rs* rs = nullptr;
  std::vector<std::string> trace;
  std::set<std::string> skips;
  std::optional<std::int64_t> lane;
  std::shared_ptr<const Value> snapshot;
  std::function<void(const std::string&)> on_own;
  bool skip(const std::string& id) override { return skips.count(id) != 0; }
  void run_own(const PipelineBody& b) override {
    trace.push_back(rs->own_names.at(static_cast<std::size_t>(b.ref)));
    if (on_own) on_own(trace.back());
  }
  bool lane_get(std::int64_t& v) override {
    if (!lane) return false;
    v = *lane;
    return true;
  }
  void lane_set(std::int64_t v) override { lane = v; }
  std::shared_ptr<const Value> view() override { return snapshot; }
  void missing_body(const std::string& id) override { throw std::runtime_error("no body: " + id); }
};

std::string err_of(const std::function<void()>& f) {
  try {
    f();
  } catch (const LoadError& e) {
    return e.what();
  }
  return "";
}

const std::vector<std::string> kPre = {"init", "base", "skill_ratio", "defense", "element", "card", "final"};

}  // namespace

// ---- order ---------------------------------------------------------------------------------------------------------
TEST("BR-RULESET-019", "pipeline: define + stage bodies give the defined order") {
  Rs rs;
  rs.define("weapon", kPre);
  rs.reg.validate();
  CHECK_EQ(joined(rs.reg.order("weapon")), std::string("init base skill_ratio defense element card final"));
  FakeHost h;
  h.rs = &rs;
  PipelineRunResult r;
  rs.reg.run("weapon", &g_ruleset_key, h, r);
  CHECK_EQ(joined(h.trace), std::string("weapon/init weapon/base weapon/skill_ratio weapon/defense weapon/element weapon/card weapon/final"));
  CHECK(!r.noteworthy());
}

TEST("BR-RULESET-019", "pipeline: a stage without a body fails at run, not silently") {
  PipelineRegistry reg;
  reg.apply(def("w", {"a", "b"}), "rs", 1, owner_only(&g_ruleset_key));
  reg.apply(stg("w", "a"), "rs", 1, body_of(&g_ruleset_key, 1));
  reg.validate();
  Rs rs;
  rs.own_names = {"", "a"};
  FakeHost h;
  h.rs = &rs;
  PipelineRunResult r;
  bool threw = false;
  try { reg.run("w", &g_ruleset_key, h, r); } catch (const std::runtime_error& e) { threw = std::string(e.what()).find("no body: b") != std::string::npos; }
  CHECK(threw);
  CHECK_EQ(joined(h.trace), std::string("a"));
}

TEST("BR-RULESET-019", "pipeline: only the defining state may run a pipeline") {
  Rs rs;
  rs.define("weapon", kPre);
  rs.reg.validate();
  FakeHost h;
  h.rs = &rs;
  PipelineRunResult r;
  std::string e = err_of([&] { rs.reg.run("weapon", &g_other_key, h, r); });
  CHECK(fx::contains(e, "run only by the mod that defined it ('rs')"));
  CHECK(fx::contains(err_of([&] { rs.reg.run("nope", &g_ruleset_key, h, r); }), "not defined"));
}

TEST("BR-RULESET-019", "pipeline: define errors (twice, duplicate or empty stage names, bodies for unknown stages)") {
  PipelineRegistry reg;
  reg.apply(def("w", {"a"}), "rs", 1, owner_only(&g_ruleset_key));
  CHECK(fx::contains(err_of([&] { reg.apply(def("w", {"a"}), "other", 1, owner_only(&g_other_key)); }), "defined twice"));
  CHECK(fx::contains(err_of([&] { reg.apply(def("x", {"a", "a"}), "rs", 1, owner_only(&g_ruleset_key)); }), "listed twice"));
  CHECK(fx::contains(err_of([&] { reg.apply(def("y", {""}), "rs", 1, owner_only(&g_ruleset_key)); }), "empty stage name"));
  CHECK(fx::contains(err_of([&] { reg.apply(stg("w", "zzz"), "rs", 1, body_of(&g_ruleset_key, 1)); }), "not a stage of pipeline 'w'"));
  CHECK(fx::contains(err_of([&] { reg.apply(stg("w", "a"), "other", 1, body_of(&g_other_key, 1)); }), "only the mod that defined pipeline 'w'"));
  reg.apply(stg("w", "a"), "rs", 1, body_of(&g_ruleset_key, 1));
  CHECK(fx::contains(err_of([&] { reg.apply(stg("w", "a"), "rs", 1, body_of(&g_ruleset_key, 2)); }), "already has a body"));
  CHECK(fx::contains(err_of([&] { reg.apply(stg("nope", "a"), "rs", 1, body_of(&g_ruleset_key, 1)); }), "pipeline 'nope' is not defined"));
}

// ---- insert -----------------------------------------------------------------------------------------------------------
TEST("BR-RULESET-019", "pipeline: insert_before / insert_after place stages, chains follow their anchor") {
  Rs rs;
  rs.define("weapon", kPre);
  rs.reg.apply(ins(false, "weapon", "element", "m:fire"), "m", 1, rs.foreign("fire"));
  rs.reg.apply(ins(false, "weapon", "m:fire", "m:fire2"), "m", 1, rs.foreign("fire2"));
  rs.reg.apply(ins(true, "weapon", "defense", "m:pre_def"), "m", 1, rs.foreign("pre_def"));
  rs.reg.validate();
  CHECK_EQ(joined(rs.reg.order("weapon")), std::string("init base skill_ratio m:pre_def defense element m:fire m:fire2 card final"));
}

TEST("BR-RULESET-019", "pipeline: stages on one anchor run by priority, then load order, then registration") {
  Rs rs;
  rs.define("weapon", kPre);
  PipelineOptions o;
  o.load_order = [](const std::string& m) { return m == "zz" ? 0 : m == "aa" ? 1 : 2; };  // zz loads before aa
  rs.reg.apply(ins(false, "weapon", "element", "aa:late", 10), "aa", 1, rs.foreign("a"));
  rs.reg.apply(ins(false, "weapon", "element", "aa:first", -5), "aa", 1, rs.foreign("b"));
  rs.reg.apply(ins(false, "weapon", "element", "zz:tie", 10), "zz", 1, rs.foreign("c"));
  rs.reg.apply(ins(false, "weapon", "element", "aa:tie2", 10), "aa", 1, rs.foreign("d"));
  rs.reg.validate(o);
  CHECK_EQ(joined(rs.reg.order("weapon")), std::string("init base skill_ratio defense element aa:first zz:tie aa:late aa:tie2 card final"));
  // before an anchor: ascending priority, the last one ends adjacent to the anchor
  Rs r2;
  r2.define("weapon", kPre);
  r2.reg.apply(ins(true, "weapon", "card", "m:b", 5), "m", 1, r2.foreign("b"));
  r2.reg.apply(ins(true, "weapon", "card", "m:a", -5), "m", 1, r2.foreign("a"));
  r2.reg.validate();
  CHECK_EQ(joined(r2.reg.order("weapon")), std::string("init base skill_ratio defense element m:a m:b card final"));
}

TEST("BR-RULESET-019", "pipeline: inserted ids must be namespaced by the inserting mod and unique") {
  Rs rs;
  rs.define("weapon", kPre);
  rs.reg.apply(ins(false, "weapon", "element", "other:fire"), "m", 1, rs.foreign("x"));
  CHECK(fx::contains(err_of([&] { rs.reg.validate(); }), "must be namespaced 'm:<name>'"));
  Rs r2;
  r2.define("weapon", kPre);
  r2.reg.apply(ins(false, "weapon", "element", "m:x"), "m", 1, r2.foreign("x"));
  r2.reg.apply(ins(false, "weapon", "card", "m:x"), "m", 1, r2.foreign("y"));
  CHECK(fx::contains(err_of([&] { r2.reg.validate(); }), "duplicate stage id 'm:x'"));
  Rs r3;
  r3.define("weapon", {"init", "m:x"});
  r3.reg.apply(ins(false, "weapon", "init", "m:x"), "m", 1, r3.foreign("y"));
  CHECK(fx::contains(err_of([&] { r3.reg.validate(); }), "duplicate stage id 'm:x'"));
}

TEST("BR-RULESET-019", "pipeline: an edit that names a missing pipeline or stage is refused, naming the mod, unless optional") {
  Rs rs;
  rs.define("weapon", kPre);
  rs.reg.apply(ins(false, "weapon", "nosuch", "m:x"), "m", 1, rs.foreign("x"));
  std::string e = err_of([&] { rs.reg.validate(); });
  CHECK(fx::contains(e, "mod 'm'") && fx::contains(e, "insert_after") && fx::contains(e, "'nosuch'") && fx::contains(e, "(stages: init, base"));
  Rs r2;
  r2.define("weapon", kPre);
  r2.reg.apply(cnl("magic", "init"), "m", 1, nullptr);
  CHECK(fx::contains(err_of([&] { r2.reg.validate(); }), "names pipeline 'magic' which is not defined"));
  // optional: ignored, recorded, and an optional insert leaves no node behind
  Rs r3;
  r3.define("weapon", kPre);
  PipelineOp a = ins(false, "weapon", "nosuch", "m:x");
  a.optional = true;
  PipelineOp b = cnl("weapon", "nosuch2");
  b.optional = true;
  PipelineOp c = cnl("magic", "init");
  c.optional = true;
  PipelineOp d = mv("weapon", "init", "nosuch3", false);
  d.optional = true;
  r3.reg.apply(a, "m", 1, r3.foreign("x"));
  r3.reg.apply(b, "m", 1, nullptr);
  r3.reg.apply(c, "m", 1, nullptr);
  r3.reg.apply(d, "m", 1, nullptr);
  r3.reg.validate();
  CHECK_EQ(joined(r3.reg.order("weapon")), joined(kPre));
  auto info = r3.reg.describe("weapon");
  CHECK_EQ(info.ignored.size(), std::size_t(3));
  CHECK(fx::contains(info.ignored[0], "m:") && fx::contains(info.ignored[0], "missing 'nosuch'"));
}

// ---- replace / cancel / precedence ------------------------------------------------------------------------------------
TEST("BR-RULESET-019", "pipeline: two replaces of one stage are a load error unless exactly one declares precedence") {
  Rs rs;
  rs.define("weapon", kPre);
  rs.reg.apply(rep("weapon", "defense", "a:flat"), "a", 1, rs.foreign("a"));
  rs.reg.apply(rep("weapon", "defense", "b:flat"), "b", 1, rs.foreign("b"));
  std::string e = err_of([&] { rs.reg.validate(); });
  CHECK(fx::contains(e, "replace conflict on weapon/defense") && fx::contains(e, "a/a:flat") && fx::contains(e, "b/b:flat") && fx::contains(e, "neither declares precedence"));
  // both declare it: still an error
  Rs r2;
  r2.define("weapon", kPre);
  r2.reg.apply(rep("weapon", "defense", "a:flat", true), "a", 1, r2.foreign("a"));
  r2.reg.apply(rep("weapon", "defense", "b:flat", true), "b", 1, r2.foreign("b"));
  CHECK(fx::contains(err_of([&] { r2.reg.validate(); }), "both declare precedence"));
  // exactly one: it wins and the other is recorded as skipped
  Rs r3;
  r3.define("weapon", kPre);
  r3.reg.apply(rep("weapon", "defense", "a:flat"), "a", 1, r3.foreign("a"));
  r3.reg.apply(rep("weapon", "defense", "b:flat", true), "b", 1, r3.foreign("b"));
  r3.reg.validate();
  auto info = r3.reg.describe("weapon");
  const PipelineStepInfo* d = nullptr;
  for (auto& s : info.steps) if (s.id == "defense") d = &s;
  CHECK(d != nullptr);
  CHECK_EQ(d->body_mod, std::string("b"));
  CHECK_EQ(d->body_id, std::string("b:flat"));
  CHECK_EQ(d->mode, std::string("replace"));
  bool skipped_note = false;
  for (auto& n : d->edits) if (n.op == "replace_skipped" && n.mod == "a" && fx::contains(n.detail, "precedence held by b")) skipped_note = true;
  CHECK(skipped_note);
}

TEST("BR-RULESET-019", "pipeline: replace swaps the body, cancel skips the stage, cancel + replace of one stage is an error") {
  Rs rs;
  rs.define("weapon", kPre);
  std::vector<std::string> log;
  rs.reg.apply(rep("weapon", "defense", "m:flat"), "m", 1, rs.foreign("flat", &log));
  rs.reg.apply(cnl("weapon", "card"), "m", 1, nullptr);
  rs.reg.validate();
  FakeHost h;
  h.rs = &rs;
  PipelineRunResult r;
  rs.reg.run("weapon", &g_ruleset_key, h, r);
  CHECK_EQ(joined(h.trace), std::string("weapon/init weapon/base weapon/skill_ratio weapon/element weapon/final"));
  CHECK_EQ(joined(log), std::string("flat"));
  CHECK_EQ(joined(r.ran), std::string("m/m:flat"));
  CHECK_EQ(joined(rs.reg.order("weapon")), std::string("init base skill_ratio defense element final"));
  Rs r2;
  r2.define("weapon", kPre);
  r2.reg.apply(rep("weapon", "defense", "a:flat"), "a", 1, r2.foreign("a"));
  r2.reg.apply(cnl("weapon", "defense"), "b", 1, nullptr);
  CHECK(fx::contains(err_of([&] { r2.reg.validate(); }), "is cancelled by mod 'b' and replaced by a/a:flat"));
}

TEST("BR-RULESET-019", "pipeline: a cancelled stage stays in describe() flagged inactive, and anchors keep their slot") {
  Rs rs;
  rs.define("weapon", kPre);
  rs.reg.apply(cnl("weapon", "element"), "m", 1, nullptr);
  rs.reg.apply(ins(false, "weapon", "element", "m:after_element"), "m", 1, rs.foreign("x"));
  rs.reg.validate();
  CHECK_EQ(joined(rs.reg.order("weapon")), std::string("init base skill_ratio defense m:after_element card final"));
  auto info = rs.reg.describe("weapon");
  const PipelineStepInfo* e = nullptr;
  for (auto& s : info.steps) if (s.id == "element") e = &s;
  CHECK(e != nullptr && !e->active);
  CHECK_EQ(e->edits.size(), std::size_t(1));
  CHECK_EQ(e->edits[0].op, std::string("cancel"));
  CHECK_EQ(e->edits[0].mod, std::string("m"));
}

// ---- protected stages -------------------------------------------------------------------------------------------------
TEST("BR-RULESET-019", "pipeline: protected stages cannot be replaced, cancelled or moved without the permission") {
  auto build = [](const PipelineOp& edit, bool permit) {
    Rs rs;
    rs.reg.apply(def("weapon", {"init", "defense", "final"}), "rs", 1, owner_only(&g_ruleset_key));
    rs.reg.apply(stg("weapon", "init"), "rs", 1, body_of(&g_ruleset_key, 1));
    rs.reg.apply(stg("weapon", "defense"), "rs", 1, body_of(&g_ruleset_key, 2));
    rs.reg.apply(stg("weapon", "final", true), "rs", 1, body_of(&g_ruleset_key, 3));
    PipelineOp e = edit;
    e.permit_protected = permit;
    rs.reg.apply(e, "m", 1, rs.foreign("x"));
    return rs;
  };
  for (const PipelineOp& e : {rep("weapon", "final", "m:r"), cnl("weapon", "final"), mv("weapon", "final", "init", true)}) {
    Rs denied = build(e, false);
    std::string msg = err_of([&] { denied.reg.validate(); });
    CHECK(fx::contains(msg, "mod 'm' cannot") && fx::contains(msg, "protected stage weapon/final without the protected-stage permission"));
    Rs allowed = build(e, true);
    allowed.reg.validate();  // the permission lifts the refusal
  }
  // inserting next to a protected stage is not an edit of it
  Rs ok = build(ins(true, "weapon", "final", "m:before_final"), false);
  ok.reg.validate();
  CHECK_EQ(joined(ok.reg.order("weapon")), std::string("init defense m:before_final final"));
  // the flag is visible to operators
  auto info = ok.reg.describe("weapon");
  CHECK(info.steps.back().protected_stage && !info.steps.front().protected_stage);
}

// ---- move ---------------------------------------------------------------------------------------------------------------
TEST("BR-RULESET-019", "pipeline: move reorders a stage without touching the defined order") {
  Rs rs;
  rs.define("weapon", kPre);
  rs.reg.apply(mv("weapon", "card", "skill_ratio", false), "m", 1, nullptr);
  rs.reg.apply(mv("weapon", "init", "final", false), "m", 1, nullptr);
  rs.reg.validate();
  CHECK_EQ(joined(rs.reg.order("weapon")), std::string("base skill_ratio card defense element final init"));
  auto info = rs.reg.describe("weapon");
  const PipelineStepInfo* c = nullptr;
  for (auto& s : info.steps) if (s.id == "card") c = &s;
  CHECK(c != nullptr && c->edits.size() == 1 && c->edits[0].op == "move" && c->edits[0].mod == "m" && fx::contains(c->edits[0].detail, "after skill_ratio"));
  // the registry's definition is unchanged: dropping the edits restores the original order exactly
  CHECK_EQ(rs.reg.remove_generation("m", 1), std::size_t(2));
  rs.reg.validate();
  CHECK_EQ(joined(rs.reg.order("weapon")), joined(kPre));
}

TEST("BR-RULESET-019", "pipeline: stages attached to a moved stage travel with it; move before an anchor and onto an inserted stage") {
  Rs rs;
  rs.define("weapon", kPre);
  rs.reg.apply(ins(false, "weapon", "card", "m:after_card"), "m", 1, rs.foreign("x"));
  rs.reg.apply(mv("weapon", "card", "base", true), "n", 1, nullptr);
  rs.reg.validate();
  CHECK_EQ(joined(rs.reg.order("weapon")), std::string("init card m:after_card base skill_ratio defense element final"));
  Rs r2;
  r2.define("weapon", kPre);
  r2.reg.apply(ins(false, "weapon", "init", "m:i"), "m", 1, r2.foreign("x"));
  r2.reg.apply(mv("weapon", "final", "m:i", true), "m", 1, nullptr);
  r2.reg.validate();
  CHECK_EQ(joined(r2.reg.order("weapon")), std::string("init final m:i base skill_ratio defense element card"));
}

TEST("BR-RULESET-019", "pipeline: two moves of one stage need exactly one precedence; cycles and self moves are refused") {
  Rs rs;
  rs.define("weapon", kPre);
  rs.reg.apply(mv("weapon", "card", "init", false), "a", 1, nullptr);
  rs.reg.apply(mv("weapon", "card", "final", false), "b", 1, nullptr);
  std::string e = err_of([&] { rs.reg.validate(); });
  CHECK(fx::contains(e, "move conflict on weapon/card") && fx::contains(e, "neither declares precedence"));
  Rs r2;
  r2.define("weapon", kPre);
  r2.reg.apply(mv("weapon", "card", "init", false), "a", 1, nullptr);
  r2.reg.apply(mv("weapon", "card", "final", false, 0, true), "b", 1, nullptr);
  r2.reg.validate();
  CHECK_EQ(joined(r2.reg.order("weapon")), std::string("init base skill_ratio defense element final card"));
  Rs r3;
  r3.define("weapon", kPre);
  r3.reg.apply(mv("weapon", "init", "base", false), "m", 1, nullptr);
  r3.reg.apply(mv("weapon", "base", "init", false), "m", 1, nullptr);
  CHECK(fx::contains(err_of([&] { r3.reg.validate(); }), "ordering cycle on pipeline 'weapon' among stages: init, base"));
  Rs r4;
  r4.define("weapon", kPre);
  r4.reg.apply(mv("weapon", "init", "init", false), "m", 1, nullptr);
  CHECK(fx::contains(err_of([&] { r4.reg.validate(); }), "relative to itself"));
}

// ---- describe -------------------------------------------------------------------------------------------------------------
TEST("BR-RULESET-019", "pipeline: describe lists the effective order with the source mod of every edit") {
  Rs rs;
  rs.define("weapon", kPre);
  rs.reg.apply(ins(false, "weapon", "element", "fire:brand", 7, Mode::Modify), "fire", 1, rs.foreign("brand"));
  rs.reg.apply(rep("weapon", "defense", "flat:def"), "flat", 1, rs.foreign("def"));
  rs.reg.apply(cnl("weapon", "card"), "nocard", 1, nullptr);
  rs.reg.apply(mv("weapon", "skill_ratio", "final", true), "mover", 1, nullptr);
  rs.reg.validate();
  auto info = rs.reg.describe("weapon");
  CHECK_EQ(info.name, std::string("weapon"));
  CHECK_EQ(info.owner_mod, std::string("rs"));
  std::vector<std::string> ids;
  for (auto& s : info.steps) ids.push_back(s.id + (s.active ? "" : "(off)"));
  CHECK_EQ(joined(ids), std::string("init base defense element fire:brand card(off) skill_ratio final"));
  auto at = [&](const std::string& id) -> const PipelineStepInfo& { for (auto& s : info.steps) if (s.id == id) return s; throw mt::Failure("no step " + id); };
  CHECK_EQ(at("fire:brand").source, std::string("fire"));
  CHECK_EQ(at("fire:brand").kind, std::string("inserted"));
  CHECK_EQ(at("fire:brand").mode, std::string("modify"));
  CHECK_EQ(at("fire:brand").priority, 7);
  CHECK_EQ(at("fire:brand").edits[0].op, std::string("insert_after"));
  CHECK(fx::contains(at("fire:brand").edits[0].detail, "after element"));
  CHECK_EQ(at("defense").source, std::string("rs"));
  CHECK_EQ(at("defense").body_mod, std::string("flat"));
  CHECK_EQ(at("defense").edits[0].op, std::string("replace"));
  CHECK_EQ(at("defense").edits[0].mod, std::string("flat"));
  CHECK_EQ(at("init").mode, std::string("own"));
  CHECK_EQ(at("init").body_mod, std::string("rs"));
  CHECK(at("init").edits.empty());
  CHECK_EQ(at("card").edits[0].mod, std::string("nocard"));
  CHECK_EQ(at("skill_ratio").edits[0].mod, std::string("mover"));
}

TEST("BR-RULESET-019", "pipeline: edits on one pipeline never change another, and generation removal restores the baseline") {
  Rs rs;
  rs.define("weapon", kPre);
  rs.define("magic", {"init", "mdef", "final"});
  rs.reg.validate();
  const std::string w0 = joined(rs.reg.order("weapon")), m0 = joined(rs.reg.order("magic"));
  rs.reg.apply(ins(false, "weapon", "init", "m:x"), "m", 1, rs.foreign("x"));
  rs.reg.apply(cnl("weapon", "final"), "m", 1, nullptr);
  rs.reg.validate();
  CHECK_EQ(joined(rs.reg.order("magic")), m0);
  CHECK(joined(rs.reg.order("weapon")) != w0);
  CHECK_EQ(rs.reg.remove_generation("m", 1), std::size_t(2));
  rs.reg.validate();
  CHECK_EQ(joined(rs.reg.order("weapon")), w0);
  CHECK_EQ(joined(rs.reg.order("magic")), m0);
}

TEST("BR-RULESET-115", "pipeline: strict_parity refuses stage hooks that declare draws_main_rng") {
  Rs rs;
  rs.define("weapon", kPre);
  PipelineOp o = ins(false, "weapon", "element", "m:rng");
  o.draws_main_rng = true;
  rs.reg.apply(o, "m", 1, rs.foreign("x"));
  rs.reg.validate();  // allowed when strict_parity is off
  PipelineOptions strict;
  strict.strict_parity = true;
  CHECK(fx::contains(err_of([&] { rs.reg.validate(strict); }), "strict_parity: stage hook m/m:rng on pipeline 'weapon' draws from the main RNG stream"));
}

// ---- run semantics (registry level) ---------------------------------------------------------------------------------------
TEST("BR-RULESET-019", "pipeline run: skip, lane read/write for modify stages, observe changes discarded") {
  Rs rs;
  rs.define("weapon", kPre);
  rs.reg.apply(ins(false, "weapon", "element", "m:plus", 0, Mode::Modify), "m", 1, rs.foreign("plus", nullptr, [](HookContext& c) { c.num.add(5); }));
  rs.reg.apply(ins(false, "weapon", "element", "m:look", 1, Mode::Observe), "m", 1, rs.foreign("look", nullptr, [](HookContext& c) { c.num.add(1000); }));
  rs.reg.validate();
  FakeHost h;
  h.rs = &rs;
  h.lane = 100;
  h.skips = {"card", "final"};
  PipelineRunResult r;
  rs.reg.run("weapon", &g_ruleset_key, h, r);
  CHECK_EQ(joined(h.trace), std::string("weapon/init weapon/base weapon/skill_ratio weapon/defense weapon/element"));
  CHECK_EQ(*h.lane, std::int64_t(105));
  CHECK_EQ(joined(r.ran), std::string("m/m:plus m/m:look"));
  CHECK_EQ(r.diagnostics.size(), std::size_t(1));
  CHECK(fx::contains(r.diagnostics[0], "observe stage m/m:look changed the lane"));
  // no lane: the hook sees 0 and nothing is written back
  FakeHost h2;
  h2.rs = &rs;
  PipelineRunResult r2;
  rs.reg.run("weapon", &g_ruleset_key, h2, r2);
  CHECK(!h2.lane.has_value());
}

TEST("BR-RULESET-116", "pipeline run: a failing hook is rolled back, recorded, and the run goes on") {
  Rs rs;
  rs.define("weapon", kPre);
  rs.reg.apply(ins(false, "weapon", "base", "m:bad", 0, Mode::Modify), "m", 1, rs.foreign("bad", nullptr, [](HookContext& c) {
                 c.num.add(500);
                 c.effects->push({"m:fx", "t", "{}"});
                 throw HookFailure(HookFailure::Kind::Error, "boom");
               }));
  rs.reg.apply(ins(false, "weapon", "base", "m:good", 1, Mode::Modify), "m", 1, rs.foreign("good", nullptr, [](HookContext& c) {
                 c.num.add(1);
                 c.effects->push({"m:ok", "t", "{}"});
               }));
  rs.reg.validate();
  FakeHost h;
  h.rs = &rs;
  h.lane = 10;
  PipelineRunResult r;
  rs.reg.run("weapon", &g_ruleset_key, h, r);
  CHECK_EQ(*h.lane, std::int64_t(11));
  CHECK_EQ(r.errors.size(), std::size_t(1));
  CHECK_EQ(r.errors[0].hook, std::string("m/m:bad"));
  CHECK_EQ(r.errors[0].kind, std::string("error"));
  CHECK_EQ(r.effects.size(), std::size_t(1));
  CHECK_EQ(r.effects[0].kind, std::string("m:ok"));
  CHECK_EQ(h.trace.size(), std::size_t(7));  // all own stages still ran
}

TEST("BR-RULESET-116", "pipeline run: ctx:cancel aborts the run and discards the run's queued effects (transactional)") {
  Rs rs;
  rs.define("weapon", kPre);
  rs.reg.apply(ins(false, "weapon", "init", "m:queue", 0, Mode::Observe), "m", 1, rs.foreign("queue", nullptr, [](HookContext& c) { c.effects->push({"m:fx", "t", "{}"}); }));
  rs.reg.apply(ins(false, "weapon", "base", "m:stop", 0, Mode::Observe), "m", 1, rs.foreign("stop", nullptr, [](HookContext& c) {
                 c.effects->push({"m:fx2", "t", "{}"});
                 c.cancel("nope");
               }));
  rs.reg.validate();
  FakeHost h;
  h.rs = &rs;
  PipelineRunResult r;
  rs.reg.run("weapon", &g_ruleset_key, h, r);
  CHECK(r.cancelled);
  CHECK_EQ(r.cancel_reason, std::string("nope"));
  CHECK_EQ(r.cancelled_by, std::string("m/m:stop"));
  CHECK(r.effects.empty());
  CHECK_EQ(joined(h.trace), std::string("weapon/init weapon/base"));  // later stages skipped
}

TEST("BR-RULESET-019", "pipeline run: a cancel that would skip a protected stage is refused without the permission") {
  auto build = [](bool permit) {
    auto rs = std::make_unique<Rs>();
    rs->reg.apply(def("weapon", {"init", "base", "final"}), "rs", 1, owner_only(&g_ruleset_key));
    rs->own_names = {"", "init", "base", "final"};
    rs->reg.apply(stg("weapon", "init"), "rs", 1, body_of(&g_ruleset_key, 1));
    rs->reg.apply(stg("weapon", "base"), "rs", 1, body_of(&g_ruleset_key, 2));
    rs->reg.apply(stg("weapon", "final", true), "rs", 1, body_of(&g_ruleset_key, 3));
    PipelineOp o = ins(false, "weapon", "init", "m:stop", 0, Mode::Observe);
    o.permit_protected = permit;
    rs->reg.apply(o, "m", 1, rs->foreign("stop", nullptr, [](HookContext& c) { c.cancel("abort"); }));
    rs->reg.validate();
    return rs;
  };
  auto denied = build(false);
  FakeHost h;
  h.rs = denied.get();
  PipelineRunResult r;
  denied->reg.run("weapon", &g_ruleset_key, h, r);
  CHECK(!r.cancelled);
  CHECK_EQ(h.trace.size(), std::size_t(3));
  CHECK(fx::contains(r.diagnostics.at(0), "cancel by m/m:stop ignored: it would skip protected stage weapon/final"));
  auto allowed = build(true);
  FakeHost h2;
  h2.rs = allowed.get();
  PipelineRunResult r2;
  allowed->reg.run("weapon", &g_ruleset_key, h2, r2);
  CHECK(r2.cancelled);
  CHECK_EQ(h2.trace.size(), std::size_t(1));
}

TEST("BR-RULESET-116", "pipeline run: budget strikes disable a stage hook; disabled hooks are skipped; an operator can re-enable") {
  Rs rs;
  rs.define("weapon", kPre);
  int calls = 0;
  rs.reg.apply(ins(false, "weapon", "base", "m:slow"), "m", 1, rs.foreign("slow", nullptr, [&](HookContext&) { ++calls; throw BudgetExceeded("m", "m:slow"); }));
  rs.reg.validate();
  FakeHost h;
  h.rs = &rs;
  PipelineRunResult r1, r2, r3, r4;
  rs.reg.run("weapon", &g_ruleset_key, h, r1);
  rs.reg.run("weapon", &g_ruleset_key, h, r2);
  rs.reg.run("weapon", &g_ruleset_key, h, r3);
  CHECK_EQ(calls, 3);
  CHECK_EQ(r3.auto_disabled.size(), std::size_t(1));
  CHECK(rs.reg.is_disabled("m/m:slow"));
  rs.reg.run("weapon", &g_ruleset_key, h, r4);
  CHECK_EQ(calls, 3);  // skipped now
  rs.reg.enable_stage("m/m:slow");
  CHECK(!rs.reg.is_disabled("m/m:slow"));
}

TEST("BR-RULESET-019", "pipeline run: a same-state body added by an edit receives the run's ctx like the defining body") {
  Rs rs;
  rs.define("weapon", kPre);
  rs.own_names.push_back("extra");
  rs.reg.apply(ins(false, "weapon", "element", "rs:extra"), "rs", 1, body_of(&g_ruleset_key, static_cast<int>(rs.own_names.size()) - 1));
  rs.reg.validate();
  FakeHost h;
  h.rs = &rs;
  PipelineRunResult r;
  rs.reg.run("weapon", &g_ruleset_key, h, r);
  CHECK_EQ(h.trace.size(), std::size_t(8));
  CHECK_EQ(h.trace[5], std::string("extra"));
  CHECK(r.ran.empty());
}

// ---- Luau: num, config, engine, pipeline API ------------------------------------------------------------------------------
namespace {

std::vector<std::string> eval(ReloadManager& rm, const std::string& mod, const std::string& src) {
  auto sb = rm.active_sandbox(mod);
  if (!sb) throw mt::Failure("mod not loaded: " + mod);
  std::vector<std::string> lines;
  sb->set_host({{}, [&](const std::string& l) { lines.push_back(l); }});
  auto r = sb->run("eval", src);
  sb->set_host({});
  if (r.status != RunStatus::Ok) throw mt::Failure("eval failed: " + r.error);
  return lines;
}
std::string eval_err(ReloadManager& rm, const std::string& mod, const std::string& src) {
  auto sb = rm.active_sandbox(mod);
  auto r = sb->run("eval", src);
  return r.status == RunStatus::Ok ? std::string() : r.error;
}

// A ruleset: lane-style accumulator, stage bodies that log into S.log and compute S.dmg
const char* kRuleset = R"LUA(
local function lane(v) return { v = v, get = function(self) return self.v end, set = function(self, x) self.v = x end } end
local order = { "init", "base", "element", "final" }
pipeline.define("weapon", order)
local B = {}
function B.init(S) S.log[#S.log + 1] = "init" end
function B.base(S) S.log[#S.log + 1] = "base"; S.dmg:set(1000) end
function B.element(S) S.log[#S.log + 1] = "element"; S.dmg:set(S.dmg:get() * 15 // 10) end
function B.final(S) S.log[#S.log + 1] = "final" end
for _, n in ipairs(order) do pipeline.stage("weapon", n, B[n], { protected = n == "final" }) end
function attack(opts)
  local S = { log = {}, dmg = lane(0), extra = 5 }
  local r = pipeline.run("weapon", S, opts and opts(S) or { lane = S.dmg })
  return S, r
end
function show(S, r)
  local parts = { table.concat(S.log, ","), tostring(S.dmg:get()) }
  if r then parts[#parts + 1] = "ran=" .. table.concat(r.ran, ",") end
  print(table.concat(parts, " | "))
end
)LUA";

struct World {
  ReloadManager rm;
  RngStreams rng{42};
  World() { rm.set_rng(&rng); rng.set_strict_parity(false); }
  ReloadResult load(const std::string& mod, const std::string& src, bool protected_perm = false) {
    ModEnvironmentSpec e = fx::env(mod);
    if (protected_perm) e.granted.insert(Permission::ProtectedStages);
    rm.set_environment(e);
    return fx::load(rm, mod, src);
  }
  ReloadResult try_load(const std::string& mod, const std::string& src, bool protected_perm = false) {
    ModEnvironmentSpec e = fx::env(mod);
    if (protected_perm) e.granted.insert(Permission::ProtectedStages);
    rm.set_environment(e);
    return rm.reload(mod, src);
  }
};

}  // namespace

namespace {
std::string g17(double d) {
  char b[64];
  std::snprintf(b, sizeof b, "%.17g", d);
  return b;
}
}  // namespace

LUAU_TEST("BR-RULESET-115", "num.f32 rounds to binary32 exactly like the C++ cast (normal, denormal, overflow, special values)") {
  ReloadManager rm;
  rm.set_environment(fx::env("m"));
  fx::load(rm, "m", "assert(true)");
  auto out = eval(rm, "m", R"LUA(
    local function bits(x) return string.format("%.17g", x) end
    print(bits(num.f32(0.1)), bits(num.f32(16777217)), bits(num.f32(1.4)), bits(num.f32(-0.0)), bits(num.f32(1e-45)))
    print(bits(num.f32(3.5e38)), bits(num.f32(-3.5e38)), bits(num.f32(1/0)), tostring(num.f32(0/0) ~= num.f32(0/0)))
    print(bits(num.f32(3.4028235677973366e38)), bits(num.f32(3.4028235677973362e38)), bits(num.f32(2^24 + 1.5)))
  )LUA");
  CHECK_EQ(out.size(), std::size_t(3));
  const double inf = std::numeric_limits<double>::infinity();
  CHECK_EQ(out[0], g17(static_cast<double>(0.1f)) + "\t" + "16777216" + "\t" + g17(static_cast<double>(1.4f)) + "\t" + "-0" + "\t" + g17(static_cast<double>(1e-45f)));
  CHECK_EQ(out[1], std::string("inf\t-inf\tinf\ttrue"));
  CHECK_EQ(out[2], std::string("inf\t") + g17(static_cast<double>(std::numeric_limits<float>::max())) + "\t16777218");
  CHECK_EQ(g17(f32_round(1e-45)), g17(static_cast<double>(1e-45f)));  // denormals are exact
  CHECK(std::isinf(inf));
}

LUAU_TEST("BR-RULESET-115", "num.f32 equals (double)(float)x on random doubles of every magnitude, and f32_round is idempotent") {
  ReloadManager rm;
  ModEnvironmentSpec e = fx::env("m", 64ull << 20);
  e.budget.steps_per_dispatch = 2000000000ull;
  rm.set_environment(e);
  fx::load(rm, "m", "assert(true)");
  std::mt19937_64 gen(7);
  std::vector<double> xs;
  for (int i = 0; i < 20000; ++i) {
    std::uint64_t b = gen();
    double d;
    std::memcpy(&d, &b, sizeof d);
    if (std::isnan(d)) continue;
    xs.push_back(d);
    if (i % 2 == 0) xs.push_back(std::ldexp(static_cast<double>(gen() >> 11) / 9007199254740992.0, static_cast<int>(gen() % 300) - 150));
    if (i % 5 == 0) xs.push_back(static_cast<double>(static_cast<std::int64_t>(gen() % 100000000000ull)) / 100.0);
  }
  std::string tbl = "local xs = {";
  for (double d : xs) {
    std::uint64_t b;
    std::memcpy(&b, &d, sizeof b);
    char buf[40];
    std::snprintf(buf, sizeof buf, "\"%016llx\",", static_cast<unsigned long long>(b));
    tbl += buf;
  }
  tbl += "}\n";
  // Luau has no 64-bit integers: decode the bit patterns through a buffer, print results as hex in lines of 40
  std::string src = tbl + R"LUA(
    local buf = buffer.create(8)
    local line, count = {}, 0
    for i, h in ipairs(xs) do
      for k = 0, 7 do buffer.writeu8(buf, 7 - k, tonumber(string.sub(h, k * 2 + 1, k * 2 + 2), 16)) end
      buffer.writef64(buf, 0, num.f32(buffer.readf64(buf, 0)))
      local bytes = {}
      for k = 7, 0, -1 do bytes[#bytes + 1] = string.format("%02x", buffer.readu8(buf, k)) end
      line[#line + 1] = table.concat(bytes)
      if #line == 40 then print(table.concat(line, ",")) line = {} end
    end
    if #line > 0 then print(table.concat(line, ",")) end
  )LUA";
  auto lines = eval(rm, "m", src);
  std::string all;
  for (auto& l : lines) all += (all.empty() ? "" : ",") + l;
  std::size_t pos = 0, n = 0;
  for (double d : xs) {
    std::size_t c = all.find(',', pos);
    std::string hex = all.substr(pos, c == std::string::npos ? std::string::npos : c - pos);
    pos = c == std::string::npos ? all.size() : c + 1;
    double want = f32_round(d);
    std::uint64_t wb;
    std::memcpy(&wb, &want, sizeof wb);
    char buf[40];
    std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(wb));
    CHECK_MSG(hex == buf, "value #" + std::to_string(n) + " got " + hex + " want " + buf);
    if (std::fabs(d) <= static_cast<double>(std::numeric_limits<float>::max())) CHECK_EQ(want, static_cast<double>(static_cast<float>(d)));  // the hardware oracle
    CHECK_EQ(f32_round(want), want);
    ++n;
  }
  CHECK_EQ(n, xs.size());
}

TEST("BR-RULESET-115", "f32_round: tie rounds to even, boundaries, nan and inf pass through") {
  CHECK_EQ(f32_round(16777217.0), 16777216.0);  // halfway: even mantissa wins
  CHECK_EQ(f32_round(16777219.0), 16777220.0);
  CHECK_EQ(f32_round(0.0), 0.0);
  CHECK(std::signbit(f32_round(-0.0)));
  CHECK(std::isnan(f32_round(std::numeric_limits<double>::quiet_NaN())));
  CHECK(std::isinf(f32_round(std::numeric_limits<double>::infinity())));
  CHECK_EQ(f32_round(3.4028235677973366e38), std::numeric_limits<double>::infinity());
  CHECK_EQ(f32_round(3.4028235677973362e38), static_cast<double>(std::numeric_limits<float>::max()));
  CHECK_EQ(f32_round(-1e300), -std::numeric_limits<double>::infinity());
  CHECK_EQ(f32_round(1e-300), 0.0);
}

LUAU_TEST("BR-RULESET-118", "config: legacy_quirks defaults to true, comes from the environment, and is read-only; `engine` exists only on request") {
  ReloadManager rm;
  ModEnvironmentSpec e = fx::env("a");
  rm.set_environment(e);
  fx::load(rm, "a", "assert(true)");
  CHECK_EQ(eval(rm, "a", "print(config.legacy_quirks, type(pipeline), type(num), type(engine))")[0], std::string("true\ttable\ttable\tnil"));
  ModEnvironmentSpec f = fx::env("b");
  f.config["legacy_quirks"] = false;
  f.config["mode"] = std::string("renewal");
  f.config["subflag_count"] = std::int64_t(7);
  f.config["ratio"] = 1.5;
  f.expose_engine = true;
  rm.set_environment(f);
  fx::load(rm, "b", "assert(true)");
  CHECK_EQ(eval(rm, "b", "print(config.legacy_quirks, config.mode, config.subflag_count, config.ratio)")[0], std::string("false\trenewal\t7\t1.5"));
  // engine (on request): one table over the same globals
  CHECK_EQ(eval(rm, "b", "print(engine.config == config, engine.pipeline == pipeline, engine.num == num, engine.rng == rng, engine.events == events, engine.config.legacy_quirks)")[0],
           std::string("true\ttrue\ttrue\ttrue\ttrue\tfalse"));
  // read-only: the tables, and the engine table, refuse writes (also through rawset)
  CHECK(fx::contains(eval_err(rm, "b", "config.legacy_quirks = true"), "readonly"));
  CHECK(fx::contains(eval_err(rm, "b", "engine.config.extra = 1"), "readonly"));
  CHECK(fx::contains(eval_err(rm, "b", "engine.pipeline = nil"), "readonly"));
  CHECK(fx::contains(eval_err(rm, "b", "rawset(config, 'x', 1)"), "readonly"));
  CHECK(fx::contains(eval_err(rm, "a", "pipeline.run = nil"), "readonly"));
  // one mod's config does not leak into another
  CHECK_EQ(eval(rm, "a", "print(config.legacy_quirks, config.mode)")[0], std::string("true\tnil"));
  // a reload (new generation) keeps the environment's config
  fx::load(rm, "b", "assert(true)");
  CHECK_EQ(eval(rm, "b", "print(config.legacy_quirks)")[0], std::string("false"));
}

LUAU_TEST("BR-RULESET-019", "pipeline Luau: stage bodies run in order through pipeline.run with the caller's ctx; skip and lane work") {
  World w;
  w.load("rs", kRuleset);
  CHECK_EQ(eval(w.rm, "rs", "show(attack())")[0], std::string("init,base,element,final | 1500"));
  CHECK_EQ(eval(w.rm, "rs", "show(attack(function(S) return { lane = S.dmg, skip = { element = true, final = true } } end))")[0],
           std::string("init,base | 1000"));
  CHECK_EQ(eval(w.rm, "rs", "local S, r = attack(); print(r == nil)")[0], std::string("true"));  // nothing foreign ran: no result table
  CHECK_EQ(eval(w.rm, "rs", "print(table.concat(pipeline.order('weapon'), ','))")[0], std::string("init,base,element,final"));
}

LUAU_TEST("BR-RULESET-019", "pipeline Luau: a Lua error in a stage body reaches the caller with its value intact; a missing body is NO_STAGE_BODY") {
  World w;
  w.load("rs", kRuleset);
  CHECK_EQ(eval(w.rm, "rs", R"LUA(
    local ok, e = pcall(pipeline.run, "weapon", { log = {}, dmg = { get = function() return 0 end } }, { skip = {} })
    print(ok, type(e))
  )LUA")[0].substr(0, 5), std::string("false"));
  w.load("rs2", R"LUA(
    pipeline.define("p", { "a", "b" })
    pipeline.stage("p", "a", function(S) error({ code = "BOOM", msg = "x" }) end)
    function go() local ok, e = pcall(pipeline.run, "p", {}) ; print(ok, type(e) == "table" and e.code or tostring(e)) end
    function go2() pipeline.run("p", {}, { skip = { a = true } }) end
  )LUA");
  CHECK_EQ(eval(w.rm, "rs2", "go()")[0], std::string("false\tBOOM"));
  CHECK_EQ(eval(w.rm, "rs2", "local ok, e = pcall(go2); print(ok, e.code, e.msg)")[0], std::string("false\tNO_STAGE_BODY\tstage 'b' has no body"));
}

LUAU_TEST("BR-RULESET-019", "pipeline Luau: a mod inserts a modify stage that changes the lane; an observe stage cannot; effects come back") {
  World w;
  w.load("rs", kRuleset);
  w.load("fire", R"LUA(
    pipeline.insert_after("weapon", "element", "fire:brand", function(ctx)
      ctx:mul_pct(110)
      ctx:enqueue("fire:burn", "target", "{}")
    end, { mode = "modify", priority = 50 })
    pipeline.insert_after("weapon", "fire:brand", "fire:spy", function(ctx)
      ctx:set(1)            -- observe stages cannot change the lane
      ctx:add(1)
    end)
  )LUA");
  auto out = eval(w.rm, "rs", R"LUA(
    local S, r = attack()
    show(S, r)
    print(#r.effects, r.effects[1].kind, r.effects[1].target, #r.errors, #r.diagnostics, r.diagnostics[1])
  )LUA");
  // the observe stage's ctx:set raised ("read-only"): its failure is recorded, the lane keeps 1650
  CHECK_EQ(out[0], std::string("init,base,element,final | 1650 | ran=fire/fire:brand,fire/fire:spy"));
  CHECK(fx::contains(out[1], "1\tfire:burn\ttarget\t1\t0"));
  auto desc = eval(w.rm, "rs", R"LUA(
    local steps, meta = pipeline.describe("weapon")
    local ids = {}
    for _, s in ipairs(steps) do ids[#ids + 1] = s.stage .. (s.active and "" or "(off)") end
    print(table.concat(ids, ","), meta.owner, meta.name)
    local f = steps[4]
    print(f.stage, f.kind, f.source, f.body_mod, f.body_id, f.mode, f.priority, #f.edits, f.edits[1].op, f.edits[1].mod)
  )LUA");
  CHECK_EQ(desc[0], std::string("init,base,element,fire:brand,fire:spy,final\trs\tweapon"));
  CHECK_EQ(desc[1], std::string("fire:brand\tinserted\tfire\tfire\tfire:brand\tmodify\t50\t1\tinsert_after\tfire"));
}

LUAU_TEST("BR-RULESET-019", "pipeline Luau: replace, cancel and move from mods change the effective order; the defined order stays") {
  World w;
  w.load("rs", kRuleset);
  w.load("flat", R"LUA(
    pipeline.replace("weapon", "element", "flat:fixed", function(ctx) ctx:set(777) end)
    pipeline.cancel("weapon", "init")
    pipeline.move("weapon", "base", { after = "element" }, { priority = 1 })
  )LUA");
  // init is cancelled; element's body is replaced by flat:fixed (runs as a hook in mod 'flat', sets the lane to 777); base moved
  // behind it and sets 1000 in the ruleset's own state
  CHECK_EQ(eval(w.rm, "rs", "show(attack())")[0], std::string("base,final | 1000 | ran=flat/flat:fixed"));
  auto order = eval(w.rm, "rs", "print(table.concat(pipeline.order('weapon'), ','))");
  CHECK_EQ(order[0], std::string("element,base,final"));
  // a reload of the editing mod with different edits replaces its old edits wholesale
  CHECK(fx::contains(w.try_load("flat", "pipeline.cancel('weapon', 'final', {})").error, "protected stage weapon/final without the protected-stage permission"));
  CHECK_EQ(w.rm.active_generation("flat"), std::uint64_t(1));  // the refused reload changed nothing
  CHECK_EQ(eval(w.rm, "rs", "print(table.concat(pipeline.order('weapon'), ','))")[0], std::string("element,base,final"));
  CHECK(w.try_load("flat", "pipeline.cancel('weapon', 'base')").ok);  // an unprotected stage may be cancelled; the old edits are gone
  CHECK_EQ(eval(w.rm, "rs", "print(table.concat(pipeline.order('weapon'), ','))")[0], std::string("init,element,final"));
}

LUAU_TEST("BR-RULESET-019", "pipeline Luau: protected stages need the permission, which the environment grants") {
  World w;
  w.load("rs", kRuleset);
  auto denied = w.try_load("hax", "pipeline.replace('weapon', 'final', 'hax:final', function(ctx) end)");
  CHECK(!denied.ok);
  CHECK(fx::contains(denied.error, "mod 'hax' cannot replace protected stage weapon/final without the protected-stage permission"));
  CHECK_EQ(w.rm.active_generation("hax"), std::uint64_t(0));
  auto ok = w.try_load("trusted", "pipeline.replace('weapon', 'final', 'trusted:final', function(ctx) end)", true);
  CHECK(ok.ok);
  CHECK_EQ(eval(w.rm, "rs", "local steps = pipeline.describe('weapon'); print(steps[4].stage, steps[4].protected, steps[4].body_mod, steps[3].protected)")[0],
           std::string("final\ttrue\ttrusted\tfalse"));
}

LUAU_TEST("BR-RULESET-019", "pipeline Luau: load-time conflicts refuse the load atomically and name the mods") {
  World w;
  w.load("rs", kRuleset);
  w.load("a", "pipeline.replace('weapon', 'element', 'a:el', function(ctx) end)");
  auto r = w.try_load("b", "pipeline.replace('weapon', 'element', 'b:el', function(ctx) end)");
  CHECK(!r.ok);
  CHECK(fx::contains(r.error, "replace conflict on weapon/element") && fx::contains(r.error, "a/a:el") && fx::contains(r.error, "b/b:el"));
  CHECK_EQ(w.rm.active_generation("b"), std::uint64_t(0));
  CHECK_EQ(eval(w.rm, "rs", "print(table.concat(pipeline.order('weapon'), ','))")[0], std::string("init,base,element,final"));
  CHECK(w.try_load("b", "pipeline.replace('weapon', 'element', 'b:el', function(ctx) end, { precedence = true })").ok);
  CHECK_EQ(eval(w.rm, "rs", "local s = pipeline.describe('weapon'); print(s[3].body_mod, s[3].edits[1].op, s[3].edits[1].mod, s[3].edits[2].op, s[3].edits[2].mod)")[0],
           std::string("b\treplace_skipped\ta\treplace\tb"));
  // bad calls
  CHECK(fx::contains(w.try_load("c", "pipeline.insert_after('weapon', 'init', 'other:x', function() end)").error, "must be namespaced 'c:<name>'"));
  CHECK(fx::contains(w.try_load("c", "pipeline.insert_after('weapon', 'ghost', 'c:x', function() end)").error, "names anchor stage 'ghost' which does not exist"));
  CHECK(w.try_load("c", "pipeline.insert_after('weapon', 'ghost', 'c:x', function() end, { optional = true })").ok);
  CHECK(fx::contains(w.try_load("d", "pipeline.move('weapon', 'init', {})").error, "exactly one of"));
  CHECK(fx::contains(w.try_load("d", "pipeline.insert_after('weapon', 'init', 'd/x', function() end)").error, "may not contain '/'"));
  CHECK(fx::contains(w.try_load("d", "pipeline.insert_after('weapon', 'init', 'd:x', function() end, { mode = 'replace' })").error, "observe or modify"));
  CHECK(fx::contains(w.try_load("d", "pipeline.insert_after('weapon', 'init', 'd:x', function() end, { precedence = true })").error, "applies to replace and move"));
  CHECK(fx::contains(w.try_load("e", "pipeline.define('weapon', { 'x' })").error, "'weapon' is defined twice"));
  CHECK(fx::contains(w.try_load("e", "pipeline.stage('weapon', 'init', function() end)").error, "only the mod that defined pipeline 'weapon'"));
}

LUAU_TEST("BR-RULESET-019", "pipeline Luau: define, stage and edits are load-time only; run belongs to the defining state") {
  World w;
  w.load("rs", kRuleset);
  w.load("m", "assert(true)");
  w.load("t", R"LUA(
    events.on("t:ev", { id = "h1" }, function(c) pipeline.define("late", { "a" }) end)
    events.on("t:ev", { id = "h2", priority = 1 }, function(c) pipeline.insert_after("weapon", "init", "t:x", function() end) end)
    events.on("t:ev", { id = "h3", priority = 2 }, function(c) pipeline.stage("weapon", "init", function() end) end)
    events.on("t:ev", { id = "h4", priority = 3 }, function(c) pipeline.move("weapon", "init", { after = "final" }) end)
  )LUA");
  HookContext hc;
  auto res = w.rm.bus().dispatch("t:ev", hc);
  CHECK_EQ(res.errors.size(), std::size_t(4));
  CHECK(fx::contains(res.errors[0].message, "pipeline.define is only available while the mod chunk loads"));
  CHECK(fx::contains(res.errors[1].message, "pipeline.insert_after is only available while the mod chunk loads"));
  CHECK(fx::contains(res.errors[2].message, "pipeline.stage is only available while the mod chunk loads"));
  CHECK(fx::contains(res.errors[3].message, "pipeline.move is only available while the mod chunk loads"));
  // another mod cannot run the ruleset's pipeline
  CHECK(fx::contains(eval_err(w.rm, "m", "pipeline.run('weapon', {})"), "run only by the mod that defined it ('rs')"));
  CHECK(fx::contains(eval_err(w.rm, "m", "pipeline.run('nope', {})"), "is not defined"));
  CHECK(fx::contains(eval_err(w.rm, "m", "pipeline.describe('nope')"), "is not defined"));
  CHECK(fx::contains(eval_err(w.rm, "rs", "pipeline.run('weapon')"), "ctx is required"));
}

LUAU_TEST("BR-RULESET-116", "pipeline Luau: cancel from a mod stage discards the effects of the run (transactional) and skips later stages") {
  World w;
  w.load("rs", kRuleset);
  w.load("g", R"LUA(
    pipeline.insert_after("weapon", "init", "g:queue", function(ctx) ctx:enqueue("g:x", "t", "{}") end)
    pipeline.insert_after("weapon", "base", "g:guard", function(ctx)
      if ctx:get() >= 1000 then ctx:cancel("too strong") end
    end, { mode = "modify" })
  )LUA", true);  // 'final' is protected: only a mod holding the permission may skip it by cancelling
  auto out = eval(w.rm, "rs", R"LUA(
    local S, r = attack()
    print(table.concat(S.log, ","), r.cancelled, r.cancel_reason, r.cancelled_by, #r.effects)
  )LUA");
  CHECK_EQ(out[0], std::string("init,base\ttrue\ttoo strong\tg/g:guard\t0"));
  // protected 'final' is still ahead of the cancelling stage but the hook has no permission: ignored with a diagnostic
  w.load("rs3", R"LUA(
    pipeline.define("p", { "a", "b" })
    pipeline.stage("p", "a", function(S) S.n = (S.n or 0) + 1 end)
    pipeline.stage("p", "b", function(S) S.n = (S.n or 0) + 10 end, { protected = true })
    function go() local S = {} local r = pipeline.run("p", S) print(S.n, r.cancelled, r.diagnostics[1]) end
  )LUA");
  w.load("hook3", "pipeline.insert_after('p', 'a', 'hook3:stop', function(ctx) ctx:cancel('x') end)");
  CHECK_EQ(eval(w.rm, "rs3", "go()")[0], std::string("11\tfalse\tcancel by hook3/hook3:stop ignored: it would skip protected stage p/b"));
}

LUAU_TEST("BR-RULESET-115", "pipeline Luau: stage hooks draw from their own RNG stream, never the main one; rng.main is refused under strict_parity") {
  World w;
  w.load("rs", kRuleset);
  w.load("dice", R"LUA(
    pipeline.insert_after("weapon", "base", "dice:roll", function(ctx)
      ctx:add(rng.roll(1000))
    end, { mode = "modify" })
  )LUA");
  RngStreams reference(42);
  std::uint32_t expect1 = reference.hook_next("dice", "dice:roll") % 1000;
  std::uint32_t expect2 = reference.hook_next("dice", "dice:roll") % 1000;
  const std::uint64_t main_before = w.rng.main_draw_count();
  auto out = eval(w.rm, "rs", "local S1 = attack(); local S2 = attack(); print(S1.dmg:get(), S2.dmg:get())");
  // base sets 1000 BEFORE the hook, the hook adds the roll, element multiplies by 1.5 (integer floor)
  auto expected = [](std::uint32_t r) { return std::to_string((1000 + r) * 15 / 10); };
  CHECK_EQ(out[0], expected(expect1) + "\t" + expected(expect2));
  CHECK_EQ(w.rng.main_draw_count(), main_before);
  // strict_parity: rng.main in a stage hook raises; the hook is rolled back and recorded, the run goes on
  w.rng.set_strict_parity(true);
  w.load("main", "pipeline.insert_after('weapon', 'base', 'main:draw', function(ctx) ctx:add(1); local x = rng.main(10) end, { mode = 'modify' })");
  auto out2 = eval(w.rm, "rs", "local S, r = attack(); print(r.errors[1].hook, r.errors[1].kind, S.dmg:get() ~= nil)");
  CHECK(fx::contains(out2[0], "main/main:draw\terror\ttrue"));
  // a stage declaring draws_main_rng is refused at load under strict_parity
  auto bad = w.try_load("main2", "pipeline.insert_after('weapon', 'base', 'main2:draw', function(ctx) end, { draws_main_rng = true })");
  CHECK(!bad.ok);
  CHECK(fx::contains(bad.error, "strict_parity: stage hook main2/main2:draw on pipeline 'weapon' draws from the main RNG stream"));
}

LUAU_TEST("BR-RULESET-117", "pipeline Luau: ctx:view() gives other mods' stages a read-only snapshot (functions left out, copies independent)") {
  World w;
  w.load("rs", kRuleset);
  w.load("peek", R"LUA(
    pipeline.insert_after("weapon", "base", "peek:look", function(ctx)
      local v = ctx:view()
      v.src.level = 999                       -- a copy: nothing the ruleset or later stages see
      local again = ctx:view()
      ctx:add(again.src.level)                -- 50 (unchanged)
      ctx:enqueue("peek:seen", tostring(again.tgt.size) .. ":" .. tostring(again.fn == nil) .. ":" .. tostring(again.src.bonus.add_race[3]), "{}")
    end, { mode = "modify" })
  )LUA");
  auto out = eval(w.rm, "rs", R"LUA(
    local S = { log = {}, dmg = { v = 0, get = function(self) return self.v end, set = function(self, x) self.v = x end },
                src = { level = 50, bonus = { add_race = { [3] = 20 } } }, tgt = { size = 2 }, fn = function() end }
    S.self = S                                   -- a cycle is cut at the depth limit, not an error
    local r = pipeline.run("weapon", S, { lane = S.dmg, view = S })
    print(S.dmg:get(), S.src.level, r.effects[1].target)
  )LUA");
  CHECK_EQ(out[0], std::string("1575\t50\t2:true:20"));  // (1000 + 50) * 1.5
}

LUAU_TEST("BR-RULESET-019", "pipeline Luau: reload rebuilds pipelines atomically, per generation") {
  World w;
  w.load("rs", kRuleset);
  w.load("fire", "pipeline.insert_after('weapon', 'element', 'fire:brand', function(ctx) ctx:mul_pct(110) end, { mode = 'modify' })");
  CHECK_EQ(eval(w.rm, "rs", "show(attack())")[0], std::string("init,base,element,final | 1650 | ran=fire/fire:brand"));
  // (1) the ruleset reloads with a changed stage body and the same stage names: edits survive, the new body runs
  std::string v2 = kRuleset;
  v2.replace(v2.find("1000"), 4, "2000");
  w.load("rs", v2);
  CHECK_EQ(w.rm.active_generation("rs"), std::uint64_t(2));
  CHECK_EQ(eval(w.rm, "rs", "show(attack())")[0], std::string("init,base,element,final | 3300 | ran=fire/fire:brand"));
  // (2) the ruleset reloads WITHOUT the stage mod 'fire' anchors on: refused, naming fire; generation 2 stays live
  std::string v3 = kRuleset;
  v3.replace(v3.find("{ \"init\", \"base\", \"element\", \"final\" }"), std::strlen("{ \"init\", \"base\", \"element\", \"final\" }"), "{ \"init\", \"base\", \"final\" }");
  v3.replace(v3.find("local B = {}"), 12, "local B = {} B.element = nil");
  auto refused = w.try_load("rs", v3);
  CHECK(!refused.ok);
  CHECK(fx::contains(refused.error, "mod 'fire'") && fx::contains(refused.error, "'element'"));
  CHECK_EQ(w.rm.active_generation("rs"), std::uint64_t(2));
  CHECK_EQ(eval(w.rm, "rs", "show(attack())")[0], std::string("init,base,element,final | 3300 | ran=fire/fire:brand"));
  // (3) the edit mod reloads: its old edit disappears together with the old generation
  w.load("fire", "pipeline.cancel('weapon', 'element')");
  CHECK_EQ(eval(w.rm, "rs", "print(table.concat(pipeline.order('weapon'), ','))")[0], std::string("init,base,final"));
  CHECK_EQ(w.rm.pipelines().count("fire", 1), std::size_t(0));
  CHECK_EQ(w.rm.pipelines().count("fire", 2), std::size_t(1));
  CHECK_EQ(w.rm.live_registrations("fire", 2), std::size_t(1));
  // (4) a failed reload of the edit mod (syntax error, conflict) leaves the previous edits in force
  CHECK(!w.try_load("fire", "pipeline.cancel(").ok);
  CHECK(!w.try_load("fire", "pipeline.replace('weapon', 'final', 'fire:f', function() end)").ok);
  CHECK_EQ(eval(w.rm, "rs", "print(table.concat(pipeline.order('weapon'), ','))")[0], std::string("init,base,final"));
}

LUAU_TEST("BR-RULESET-019", "pipeline Luau: a stale generation's state cannot run the pipeline after the ruleset reloaded") {
  World w;
  w.load("rs", kRuleset);
  auto old_sb = w.rm.active_sandbox("rs");
  w.load("rs", kRuleset);
  old_sb->set_host({});
  auto r = old_sb->run("late", "local S = { log = {}, dmg = { get = function() return 0 end } } pipeline.run('weapon', S)");
  CHECK(r.status == RunStatus::Error);
  CHECK(fx::contains(r.error, "older generation"));
}

LUAU_TEST("BR-RULESET-019", "pipeline Luau: a failing foreign stage does not abort the ruleset's run; its error is reported") {
  World w;
  w.load("rs", kRuleset);
  w.load("bad", "pipeline.insert_after('weapon', 'base', 'bad:x', function(ctx) ctx:set(5) error('kaput') end, { mode = 'modify' })");
  auto out = eval(w.rm, "rs", "local S, r = attack(); show(S, r); print(r.errors[1].hook, r.errors[1].kind, r.errors[1].message:find('kaput') ~= nil)");
  CHECK_EQ(out[0], std::string("init,base,element,final | 1500 | ran=bad/bad:x"));  // lane change rolled back
  CHECK_EQ(out[1], std::string("bad/bad:x\terror\ttrue"));
}

LUAU_TEST("BR-RULESET-019", "pipeline Luau: lane values beyond 2^53 and a lane that returns junk are refused clearly") {
  World w;
  w.load("rs", kRuleset);
  w.load("m", "pipeline.insert_after('weapon', 'base', 'm:x', function(ctx) ctx:mul(10000000000000) end, { mode = 'modify' })");
  CHECK(fx::contains(eval_err(w.rm, "rs", "attack()"), "beyond 2^53"));
  w.load("rs4", R"LUA(
    pipeline.define("p", { "a" })
    pipeline.stage("p", "a", function() end)
    function go(lane) return pipeline.run("p", {}, { lane = lane }) end
  )LUA");
  w.load("h", "pipeline.insert_after('p', 'a', 'h:x', function(ctx) end)");
  CHECK(fx::contains(eval_err(w.rm, "rs4", "go({ get = function() return 'x' end, set = function() end })"), "must return an integer"));
  CHECK(fx::contains(eval_err(w.rm, "rs4", "go({ get = function() return 1.5 end, set = function() end })"), "must return an integer"));
}
