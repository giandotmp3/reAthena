"""Acceptance tests for ruleset.renewal, tagged by BR-RULESET ID.

Run: python3 -m unittest discover -s tests -v   (from the ruleset-renewal directory)
Structure/data tests must pass. Behavior tests are expectedFailure (pending); an unexpected
pass fails the run so the marker gets removed.
"""
import json
import re
import sys
import tomllib
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import adapter  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
load = lambda p: json.loads((ROOT / p).read_text())
PIPE, TABLES, HOOKS = load("data/pipeline.json"), load("data/tables.json"), load("data/hooks.json")
VECTORS = load("conformance/vectors/ruleset_renewal.json")["vectors"]
MOD = tomllib.loads((ROOT / "mod.toml").read_text())
PKG = tomllib.loads((ROOT / "package.toml").read_text())

ASSIGNED = ["009", "010", "016", "019", "022", "025", "031", "034", "044", "045", "048",
            "054", "057", "066", "068", "102", "104", "117", "118", "119"]


def pending(rule):
    return unittest.expectedFailure


def run_vectors(tc, rule):
    vs = [v for v in VECTORS if v["rule"] == rule]
    tc.assertTrue(vs, f"no conformance vector for {rule}")
    for v in vs:
        got = adapter.call(v["fn"], v["args"])
        tc.assertEqual(got, v["expect"], v)


class Manifest(unittest.TestCase):
    def test_BR_RULESET_118_manifest_is_exclusive_ruleset(self):
        self.assertEqual(MOD["id"], "ruleset.renewal")
        self.assertIn("ruleset", MOD["provides"])
        self.assertIn("ruleset.prerenewal", MOD["conflicts"])
        self.assertIn("rathena.base", MOD["depends"])

    def test_BR_RULESET_118_mode_and_defaults(self):
        self.assertEqual(PKG["mode"], "renewal")
        self.assertEqual(PKG["db_set"], "re")
        self.assertEqual(PKG["battle_defaults"]["max_res_mres_ignored"], 50)
        self.assertEqual(PKG["battle_defaults"]["attack_attr_none"], 14)

    def test_BR_RULESET_118_subflags_bundle_default_and_unsupported_override(self):
        self.assertEqual(set(PKG["subflags"]), {"cast", "drop", "exp", "lvdmg", "aspd", "stat", "edp"})
        self.assertTrue(all(PKG["subflags"].values()))
        self.assertFalse(PKG["allow_unsupported_subflags"])

    def test_BR_RULESET_118_no_credentials_or_literals_in_config(self):
        text = (ROOT / "package.toml").read_text() + (ROOT / "mod.toml").read_text()
        self.assertIsNone(re.search(r"(?i)(password|secret|token|api[_-]?key)\s*=", text))

    @pending("BR-RULESET-118")
    def test_BR_RULESET_118_unsupported_subflag_combo_refused_at_load(self):
        raise adapter.HostUnavailable("loader validation of sub-flag combos")


class PipelineOrder(unittest.TestCase):
    def idx(self, s, kind="weapon"):
        return PIPE[kind].index(s)

    def test_BR_RULESET_019_re_stage_order(self):
        w = PIPE["weapon"]
        self.assertEqual(len(w), len(set(w)))
        order = ["base_attack", "card_attacker", "p_atk", "mastery", "crit_rate", "skill_ratio",
                 "constant_add", "adv_katar_mastery", "res", "defense", "post_defense",
                 "skill_atk_bonus", "element", "crit_multiplier", "card_target", "div_fix",
                 "hands", "map_rates", "final"]
        self.assertEqual([s for s in w if s in order], order)

    def test_BR_RULESET_019_res_between_ratio_and_defense(self):
        self.assertLess(self.idx("skill_ratio"), self.idx("res"))
        self.assertLess(self.idx("res"), self.idx("defense"))

    def test_BR_RULESET_019_overrides_are_few_and_registered(self):
        self.assertLessEqual(len(PIPE["overrides"]), 6)
        for s in PIPE["overrides"]:
            self.assertTrue(s in PIPE["weapon"] or s in PIPE["magic"], s)

    @pending("BR-RULESET-019")
    def test_BR_RULESET_019_register_defines_pipeline(self):
        raise adapter.HostUnavailable("scripts/init.luau register()")

    def test_BR_RULESET_016_atk_percent_seeds_ratio(self):
        self.assertEqual(PIPE["atk_percent_placement"], "seeds_skill_ratio")

    @pending("BR-RULESET-016")
    def test_BR_RULESET_016_atk_percent_behavior(self):
        raise adapter.HostUnavailable("status ATK% contribution")

    def test_BR_RULESET_044_magic_order(self):
        m = PIPE["magic"]
        for a, b in [("two_hand_staff", "s_matk"), ("s_matk", "skill_ratio"),
                     ("skill_ratio", "skill_atk_bonus"), ("skill_atk_bonus", "ignore_mdef"),
                     ("ignore_mdef", "mdef"), ("mdef", "element")]:
            self.assertLess(m.index(a), m.index(b), (a, b))
        self.assertLess(m.index("card_attacker"), m.index("two_hand_staff"))

    @pending("BR-RULESET-044")
    def test_BR_RULESET_044_magic_damage_behavior(self):
        raise adapter.HostUnavailable("magic pipeline")

    def test_BR_RULESET_022_magic_cardfix_sequence(self):
        self.assertEqual(PIPE["magic_cardfix_order"],
                         ["size", "race2", "element", "attack_element", "race", "class"])

    @pending("BR-RULESET-022")
    def test_BR_RULESET_022_magic_cardfix_rounded_steps(self):
        raise adapter.HostUnavailable("card fix steps")


class Hooks(unittest.TestCase):
    def test_BR_RULESET_117_minimum_hook_surface(self):
        for k in ("damage_stage", "status", "item", "formula"):
            self.assertTrue(HOOKS[k], k)
        self.assertEqual(set(HOOKS["damage_stage"]),
                         {"base", "ratio", "element", "defense", "final"})


class DataDiff(unittest.TestCase):
    def test_BR_RULESET_102_bonus_semantics(self):
        self.assertEqual(TABLES["bonus_semantics"]["bBaseAtk"], "eatk")
        self.assertEqual(TABLES["bonus_semantics"]["bAspdRate"], "aspd_rate2_add")

    @pending("BR-RULESET-102")
    def test_BR_RULESET_102_bonus_dispatch(self):
        raise adapter.HostUnavailable("bonus vocabulary")

    def test_BR_RULESET_104_refine_data(self):
        self.assertEqual(TABLES["refine"]["weapon_p_atk_s_matk_per_refine"], 2)

    @pending("BR-RULESET-104")
    def test_BR_RULESET_104_refine_behavior(self):
        raise adapter.HostUnavailable("refine contributions")

    def test_BR_RULESET_066_def2_negative_allowed(self):
        self.assertTrue(TABLES["def2"]["allow_negative"])

    @pending("BR-RULESET-066")
    def test_BR_RULESET_066_def2_behavior(self):
        raise adapter.HostUnavailable("DEF2 contributions")

    def test_BR_RULESET_068_aspd_caps_data(self):
        self.assertEqual(TABLES["aspd_caps"]["quicken"], 7)

    @pending("BR-RULESET-068")
    def test_BR_RULESET_068_aspd_behavior(self):
        raise adapter.HostUnavailable("ASPD contributions")

    def test_BR_RULESET_054_tick_def2_and_stats(self):
        self.assertEqual(TABLES["tick_def2_ms"]["stun"], 500)
        self.assertEqual(TABLES["status_resist_stat"]["confusion"], "luk")


class Formulas(unittest.TestCase):
    @pending("BR-RULESET-009")
    def test_BR_RULESET_009_re_atk_parts(self):
        raise adapter.HostUnavailable("ATK parts")

    @pending("BR-RULESET-010")
    def test_BR_RULESET_010_weapon_atk_roll(self):
        run_vectors(self, "BR-RULESET-010")

    @pending("BR-RULESET-025")
    def test_BR_RULESET_025_res(self):
        run_vectors(self, "BR-RULESET-025")

    @pending("BR-RULESET-031")
    def test_BR_RULESET_031_element_ratio_additive(self):
        raise adapter.HostUnavailable("element ratio statuses")

    @pending("BR-RULESET-034")
    def test_BR_RULESET_034_crit_damage(self):
        run_vectors(self, "BR-RULESET-034")

    @pending("BR-RULESET-045")
    def test_BR_RULESET_045_mdef(self):
        run_vectors(self, "BR-RULESET-045")

    @pending("BR-RULESET-048")
    def test_BR_RULESET_048_heal(self):
        run_vectors(self, "BR-RULESET-048")

    @pending("BR-RULESET-054")
    def test_BR_RULESET_054_level_difference_resistance(self):
        run_vectors(self, "BR-RULESET-054")

    @pending("BR-RULESET-057")
    def test_BR_RULESET_057_delayed_status_duration(self):
        run_vectors(self, "BR-RULESET-057")


class Conformance(unittest.TestCase):
    def test_BR_RULESET_119_every_vector_keyed_by_known_rule(self):
        for v in VECTORS:
            self.assertRegex(v["rule"], r"^BR-RULESET-\d{3}$")
            self.assertIn(v["rule"][-3:], ASSIGNED)

    def test_BR_RULESET_119_every_assigned_rule_has_a_test(self):
        src = Path(__file__).read_text()
        for n in ASSIGNED:
            self.assertIn(f"test_BR_RULESET_{n}_", src, n)

    @pending("BR-RULESET-119")
    def test_BR_RULESET_119_golden_trace_replay(self):
        raise adapter.HostUnavailable("RATHENA_GOLDEN_TRACE captures not yet imported")


if __name__ == "__main__":
    unittest.main()
