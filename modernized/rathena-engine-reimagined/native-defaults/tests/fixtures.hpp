#pragma once
// Fake, minimal ruleset tables for tests. Numbers come from the spec worked examples
// (E-1, E-2, BR-MAP-014/017); they are not copied from legacy source files.
#include "native_defaults/damage_ctx.hpp"
#include "native_defaults/tables.hpp"

namespace fx {

struct CountingRng : nd::IRng {
  std::uint64_t n = 0;
  std::uint32_t next() override { return static_cast<std::uint32_t>(++n * 2654435761u); }
  std::uint64_t draws() const override { return n; }
};

// Elements: 0 neutral, 1 water, 2 fire, 3 ghost. One level only.
inline nd::RulesetTables tables(bool renewal) {
  nd::RulesetTables t;
  t.ruleset_id = renewal ? "ruleset.fake-renewal" : "ruleset.fake-prerenewal";
  t.element = {4, 1, std::vector<int>(16, 100)};
  t.element.ratio[(1 * 4 + 2) * 1] = 150;               // water vs fire
  t.element.ratio[(0 * 4 + 3) * 1] = renewal ? 90 : 25; // neutral vs ghost (BR-MAP-017)
  t.size = {1, 3, {100, 100, 75}};                      // knuckle vs large = 75 (BR-MAP-018)
  t.status_element_mode = renewal ? nd::StatusElementMode::RenewalAdditive : nd::StatusElementMode::PreMultiplicative;
  t.defense.curve = renewal ? nd::DefenseCurve::Renewal : nd::DefenseCurve::PreRenewal;
  return t;
}

}  // namespace fx
