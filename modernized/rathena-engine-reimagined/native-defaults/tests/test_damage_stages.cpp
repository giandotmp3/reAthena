#include "fixtures.hpp"
#include "mini_test.hpp"
#include "native_defaults/not_implemented.hpp"
#include "native_defaults/stage.hpp"

using namespace nd;

static DamageCtx run(std::unique_ptr<IStage> st, DamageCtx c, const RulesetTables& t) {
  fx::CountingRng r;
  st->apply(c, t, r);
  return c;
}

// ---- native.damage.element_table@1
PENDING("BR-RULESET-031", "E-1 PRE: Deluge 15 on Water vs Fire lv1 -> 1725") {
  DamageCtx c; c.damage = 1000; c.attack_element = 1; c.target_element = 2; c.status_element_pct = 15;
  CHECK_EQ(run(make_element_table_v1(), c, fx::tables(false)).damage, 1725);
}
PENDING("BR-RULESET-031", "E-1 RE: ratio 150+15 -> 1650") {
  DamageCtx c; c.damage = 1000; c.attack_element = 1; c.target_element = 2; c.status_element_pct = 15;
  CHECK_EQ(run(make_element_table_v1(), c, fx::tables(true)).damage, 1650);
}
PENDING("BR-MAP-017", "neutral vs ghost lv1 differs by ruleset (90 RE, 25 PRE)") {
  DamageCtx c; c.damage = 1000; c.attack_element = 0; c.target_element = 3;
  CHECK_EQ(run(make_element_table_v1(), c, fx::tables(true)).damage, 900);
  CHECK_EQ(run(make_element_table_v1(), c, fx::tables(false)).damage, 250);
}
PENDING("BR-RULESET-032", "attr_recover=0 clamps negative ratio so damage is never negative") {
  auto t = fx::tables(true);
  t.element.ratio[(1 * 4 + 2)] = -50;
  t.constants["attr_recover"] = 0;
  DamageCtx c; c.damage = 1000; c.attack_element = 1; c.target_element = 2;
  CHECK_EQ(run(make_element_table_v1(), c, t).damage, 0);
}

// ---- native.damage.defense_soft@1
PENDING("BR-MAP-014", "RE defense: 1000 dmg, DEF 100, softDEF 50 -> 770") {
  DamageCtx c; c.damage = 1000; c.target_def = 100; c.target_soft_def = 50;
  CHECK_EQ(run(make_defense_soft_v1(), c, fx::tables(true)).damage, 770);
}
PENDING("BR-MAP-015", "PRE defense: dmg*(100-DEF)% minus randomised VIT def, consumes legacy draw count") {
  DamageCtx c; c.damage = 1000; c.target_def = 10; c.target_soft_def = 20;
  fx::CountingRng r;
  auto st = make_defense_soft_v1();
  st->apply(c, fx::tables(false), r);
  CHECK(c.damage <= 900 && c.damage >= 900 - 2 * 20);
  CHECK(r.draws() >= 1);
}
PENDING("BR-RULESET-025", "E-2 RES 100 -> 840; attacker ignoring 50% -> 912") {
  DamageCtx c; c.damage = 1000; c.target_res = 100;
  CHECK_EQ(run(make_defense_soft_v1(), c, fx::tables(true)).damage, 840);
  c.ignore_res_pct = 50;
  CHECK_EQ(run(make_defense_soft_v1(), c, fx::tables(true)).damage, 912);
}
PENDING("BR-RULESET-045", "RE MDEF curve dmg*(1000+mdef)/(1000+10*mdef) - mdef2") {
  throw NotImplementedError("mdef is exposed through native.damage.defense_soft@1 magic mode; API TBD");
}

// ---- native.damage.weapon_basic@1
PENDING("BR-RULESET-010", "E-9 RE weapon ATK range [120,160]; crit takes the max") {
  auto t = fx::tables(true);
  t.constants["weapon_atk"] = 100; t.constants["weapon_level"] = 4; t.constants["stat"] = 80;
  DamageCtx c; c.is_crit = true;
  CHECK_EQ(run(make_weapon_basic_v1(), c, t).damage, 160);
}
PENDING("BR-RULESET-008", "PRE non-crit roll is rnd%(max-min)+min: max never rolled (legacy_quirks)") {
  throw NotImplementedError("weapon_basic PRE roll");
}
PENDING("BR-RULESET-036", "negative div means divide then multiply; total raised to div when flagged") {
  throw NotImplementedError("div fix");
}
PENDING("BR-RULESET-111", "per-hit stage order follows the ruleset pipeline (PRE vs RE differ in order)") {
  throw NotImplementedError("stage ordering is applied by the pipeline owner; native stages are order-agnostic");
}
