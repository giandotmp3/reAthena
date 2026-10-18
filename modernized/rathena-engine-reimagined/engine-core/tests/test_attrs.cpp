#include "engine/attrs/attrs.hpp"
#include "harness.hpp"

using namespace engine;

namespace {
// Fake same-shape ruleset stages; names and numbers are test-local.
AggregationStage sum_tag(const std::string& tag) {
  return {"sum_" + tag, [tag](int64_t acc, std::span<const Contribution> cs) {
            for (auto& c : cs) if (c.tag == tag) acc += c.value;
            return acc;
          }};
}
AggregationStage pct_tag(const std::string& tag) {
  return {"pct_" + tag, [tag](int64_t acc, std::span<const Contribution> cs) {
            int64_t p = 0;
            for (auto& c : cs) if (c.tag == tag) p += c.value;
            return acc * (100 + p) / 100;
          }};
}
}  // namespace

TEST("EC-ATTR-001", interning_is_stable) {
  AttributeSystem s;
  AttrId a = s.intern("alpha"), b = s.intern("beta");
  CHECK(a != b);
  CHECK(s.intern("alpha") == a);
  CHECK(s.name_of(b) == "beta");
}

TEST("EC-ATTR-002", engine_runs_ruleset_stage_list_in_order) {
  AttributeSystem s;
  EntityAllocator al;
  EntityId e = al.create();
  AttrId a = s.intern("a");
  s.define_stages(a, {sum_tag("add"), pct_tag("pct")});
  s.add_contribution(e, a, {1, "add", 10});
  s.add_contribution(e, a, {2, "add", 5});
  s.add_contribution(e, a, {3, "pct", 20});
  CHECK(s.get(e, a) == 18);  // (10+5) * 120 / 100
  AttrId b = s.intern("b");
  s.define_stages(b, {pct_tag("pct"), sum_tag("add")});
  s.add_contribution(e, b, {1, "add", 15});
  s.add_contribution(e, b, {3, "pct", 20});
  CHECK(s.get(e, b) == 15);  // pct applied to 0 first, then add
}

TEST("EC-ATTR-003", remove_source_drops_all_its_contributions) {
  AttributeSystem s;
  EntityAllocator al;
  EntityId e = al.create();
  AttrId a = s.intern("a");
  s.define_stages(a, {sum_tag("add")});
  s.add_contribution(e, a, {7, "add", 4});
  s.add_contribution(e, a, {7, "add", 6});
  s.add_contribution(e, a, {8, "add", 1});
  CHECK(s.get(e, a) == 11);
  CHECK(s.remove_source(e, 7) == 2);
  CHECK(s.get(e, a) == 1);
}

TEST("EC-ATTR-004", incremental_recompute_equals_from_scratch) {
  AttributeSystem s;
  EntityAllocator al;
  EntityId e = al.create();
  AttrId a = s.intern("a");
  s.define_stages(a, {sum_tag("add"), pct_tag("pct")});
  uint32_t rng = 12345;  // deterministic LCG
  auto next = [&] { rng = rng * 1664525u + 1013904223u; return rng >> 8; };
  for (int i = 0; i < 500; ++i) {
    uint32_t r = next() % 3;
    SourceId src = next() % 10;
    if (r == 0) s.add_contribution(e, a, {src, "add", int64_t(next() % 50)});
    else if (r == 1) s.add_contribution(e, a, {src, "pct", int64_t(next() % 30)});
    else s.remove_source(e, src);
    CHECK(s.get(e, a) == s.recompute_from_scratch(e, a));
  }
}
