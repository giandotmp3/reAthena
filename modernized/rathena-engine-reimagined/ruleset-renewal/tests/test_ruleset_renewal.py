"""Acceptance tests for ruleset.renewal, tagged by BR-RULESET ID.

Run: python3 -m unittest discover -s tests -v   (from the ruleset-renewal directory; Python 3.11+, stdlib only)
The Luau CLI ($LUAU, `luau` on PATH or $RATHENA_LUAU_ROOT/b/luau) runs the package: tools/run_tests.py lints the numerics, expands
the golden data recorded from the REAL renewal legacy code (conformance/golden/weapon_re.jsonl.gz), bundles the modules and runs the
Luau suite (tests/main.luau); the tests below map its results to BR-RULESET IDs. Without the CLI the tests that need it are skipped.

Structure/data tests and the converted behaviour tests must pass. Behaviour tests whose rule no golden data covers yet stay
expectedFailure with the reason in the message; an unexpected pass fails the run so the marker gets removed.
"""
import gzip
import json
import re
import subprocess
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

needs_luau = unittest.skipUnless(adapter.available(), "Luau CLI not found (set LUAU or RATHENA_LUAU_ROOT)")


def pending(rule):
    return unittest.expectedFailure


def run_vectors(tc, rule):
    vs = [v for v in VECTORS if v["rule"] == rule]
    tc.assertTrue(vs, f"no conformance vector for {rule}")
    for v in vs:
        got = adapter.call(v["fn"], v["args"])
        tc.assertEqual(got, v["expect"], v)


def luau_rule_passes(tc, rule, at_least=1):
    """Every Luau test tagged with `rule` passed, and there are at least `at_least` of them."""
    res = adapter.tagged(rule)
    tc.assertGreaterEqual(len(res), at_least, f"no Luau test tagged {rule}")
    bad = [r for r in res if r["status"] != "PASS"]
    tc.assertFalse(bad, bad)


def luau_order(kind="weapon"):
    text = (ROOT / "scripts" / "pipeline.luau").read_text()
    block = re.search(kind + r"\s*=\s*\{(.*?)\n\t\}", text, re.S).group(1)
    return re.findall(r'"([a-z_]+)"', "\n".join(l.split("--")[0] for l in block.splitlines()))


class Manifest(unittest.TestCase):
    def test_BR_RULESET_118_manifest_is_exclusive_ruleset(self):
        self.assertEqual(MOD["id"], "ruleset.renewal")
        self.assertIn("ruleset", MOD["provides"])
        self.assertIn("ruleset.prerenewal", MOD["conflicts"])
        self.assertIn("rathena.base", MOD["depends"])

    def test_BR_RULESET_118_ruleset_table_matches_the_mods_loader_schema(self):
        # the mods unit's manifest parser (mods/src/manifest.cpp) validates [ruleset]: mode, db_set, subflags, legacy_quirks, strict_parity
        r = MOD["ruleset"]
        self.assertEqual(r["mode"], "renewal")
        self.assertEqual(r["db_set"], "db/re")
        self.assertEqual(set(r["subflags"]), set(PKG["subflags"]))
        self.assertTrue(all(r["subflags"].values()))
        self.assertTrue(r["legacy_quirks"] and r["strict_parity"])

    def test_BR_RULESET_118_mode_and_defaults(self):
        self.assertEqual(PKG["mode"], "renewal")
        self.assertEqual(PKG["db_set"], "re")
        self.assertEqual(PKG["battle_defaults"]["max_res_mres_ignored"], 50)
        self.assertEqual(PKG["battle_defaults"]["attack_attr_none"], 14)

    def test_BR_RULESET_118_battle_defaults_equal_the_values_the_legacy_binary_loaded(self):
        # data/damage_cfg.luau is what the Luau pipeline reads; package.toml is the operator-facing copy: they must agree
        cfg = dict(re.findall(r"(\w+) = (-?\d+)", (ROOT / "data" / "damage_cfg.luau").read_text()))
        self.assertEqual(int(cfg["max_res_mres_ignored"]), PKG["battle_defaults"]["max_res_mres_ignored"])
        self.assertEqual(int(cfg["attack_attr_none"]), PKG["battle_defaults"]["attack_attr_none"])
        self.assertEqual(int(cfg["min_hitrate"]), PKG["battle_defaults"]["hit_rate_min"])
        self.assertEqual(int(cfg["max_hitrate"]), PKG["battle_defaults"]["hit_rate_max"])
        self.assertEqual(int(cfg["left_cardfix_to_right"]), int(PKG["battle_defaults"]["left_cardfix_to_right"]))
        self.assertEqual(int(cfg["cardfix_monster_physical"]), int(PKG["battle_defaults"]["cardfix_monster_physical"]))

    def test_BR_RULESET_118_subflags_bundle_default_and_unsupported_override(self):
        self.assertEqual(set(PKG["subflags"]), {"cast", "drop", "exp", "lvdmg", "aspd", "stat", "edp"})
        self.assertTrue(all(PKG["subflags"].values()))
        self.assertFalse(PKG["allow_unsupported_subflags"])

    def test_BR_RULESET_118_no_credentials_or_literals_in_config(self):
        text = (ROOT / "package.toml").read_text() + (ROOT / "mod.toml").read_text()
        self.assertIsNone(re.search(r"(?i)(password|secret|token|api[_-]?key)\s*=", text))

    @needs_luau
    def test_BR_RULESET_118_legacy_quirks_flag_reproduces_legacy_and_corrects_two_defects(self):
        # tests/acceptance/quirks_test.luau: legacy_quirks = true reproduces the doubled monster crit multiplier and the weapon_damage_rate
        # slot aliasing (golden), false gives the documented corrected behaviour and changes nothing else (every golden case compared)
        luau_rule_passes(self, "BR-RULESET-118", at_least=3)
        luau_rule_passes(self, "BR-RULESET-102", at_least=5)

    def test_BR_RULESET_118_legacy_quirks_is_wired_from_the_manifest_to_the_package(self):
        self.assertIs(MOD["ruleset"]["legacy_quirks"], True)
        cfg = (ROOT / "data" / "damage_cfg.luau").read_text()
        self.assertRegex(cfg, r"legacy_quirks = true")
        init = (ROOT / "scripts" / "init.luau").read_text()
        self.assertIn("engine.config.legacy_quirks", init)

    @pending("BR-RULESET-118")
    def test_BR_RULESET_118_unsupported_subflag_combo_refused_at_load(self):
        raise adapter.HostUnavailable("needs the mod loader (mods/): sub-flag combination validation is loader behaviour, no ruleset code yet")


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
        # the stage list was renamed where the code disagreed with the first sketch (constant_add -> const_add, div_fix + hands ->
        # div_hands); the relative order of the sketch is what the golden probes confirm
        alias = {"constant_add": "const_add", "div_fix": "div_hands", "hands": "div_hands"}
        want = []
        for s in order:
            s = alias.get(s, s)
            if s not in want:
                want.append(s)
        self.assertEqual([s for s in w if s in want], want)

    def test_BR_RULESET_019_pipeline_json_equals_the_luau_order(self):
        self.assertEqual(PIPE["weapon"], luau_order("weapon"))
        self.assertEqual(PIPE["magic"], luau_order("magic"))

    def test_BR_RULESET_019_res_between_ratio_and_defense(self):
        self.assertLess(self.idx("skill_ratio"), self.idx("res"))
        self.assertLess(self.idx("res"), self.idx("defense"))

    def test_BR_RULESET_019_order_corrections_found_by_the_golden_probes(self):
        # BR-RULESET-019 as written: "... masteries -> crit rate -> ratio -> constant -> Adv. Katar -> RES -> DEF -> post-DEF ->
        # skill ATK bonus -> element -> x1.4 crit -> flat additions -> non-PC target card fix -> div -> hands ..."
        # Code (battle.cpp:5502-5817): the target's cards also apply to every ATK part of a PC attacker right after the attacker's
        # cards (stage card_target_parts), the status ATK bonuses (sc_bonus) come after both card fixes, and the status ATK% is
        # not a stage at all: it seeds the skill ratio.
        w = PIPE["weapon"]
        self.assertLess(w.index("card_attacker"), w.index("card_target_parts"))
        self.assertLess(w.index("card_target_parts"), w.index("sc_bonus"))
        self.assertLess(w.index("sc_bonus"), w.index("sum"))
        self.assertLess(w.index("crit_rate"), w.index("skill_ratio"))
        self.assertLess(w.index("post_defense"), w.index("element"))
        self.assertLess(w.index("element"), w.index("crit_multiplier"))
        self.assertLess(w.index("crit_multiplier"), w.index("card_target"))

    def test_BR_RULESET_019_override_set_is_measured_not_declared(self):
        # The first sketch of this package claimed "at most 6 overrides". The measurement (docs/pipeline_diff.json, generated by
        # tools/diff_report.py from the two real packages) says otherwise; this test pins the measured classes so a change in
        # either package shows up in review.
        d = load("docs/pipeline_diff.json")
        classes = {s["stage"]: s["class"] for s in d["stages"]}
        self.assertEqual(sorted(s for s, c in classes.items() if c == "identical"), ["div_hands", "final", "map_rates"])
        self.assertEqual(sum(1 for c in classes.values() if c == "logic differs"), 9)
        self.assertEqual(sum(1 for c in classes.values() if c == "new in RE"), 10)
        self.assertEqual(d["stages_removed_from_pre"], ["element_bonus"])
        self.assertEqual(set(classes), set(PIPE["weapon"]))

    @needs_luau
    def test_BR_RULESET_019_register_defines_pipeline(self):
        # scripts/init.luau register(engine): tests/acceptance/architecture_test.luau gives it an engine that offers ONLY
        # pipeline.define and checks the weapon and magic orders arrive unchanged
        luau_rule_passes(self, "BR-RULESET-019", at_least=4)

    @needs_luau
    def test_BR_RULESET_019_legacy_stage_probes_follow_the_registered_order(self):
        res = [r for r in adapter.tagged("BR-RULESET-019") if "legacy stage probes" in r["title"]]
        self.assertEqual([r["status"] for r in res], ["PASS"])

    def test_BR_RULESET_016_atk_percent_seeds_ratio(self):
        self.assertEqual(PIPE["atk_percent_placement"], "seeds_skill_ratio")
        self.assertNotIn("atk_percent", PIPE["weapon"])  # not a stage in RE: it is the first term of the skill ratio

    @needs_luau
    def test_BR_RULESET_016_atk_percent_behavior(self):
        luau_rule_passes(self, "BR-RULESET-016", at_least=1)

    def test_BR_RULESET_044_magic_order(self):
        m = PIPE["magic"]
        for a, b in [("two_hand_staff", "s_matk"), ("s_matk", "skill_ratio"),
                     ("skill_ratio", "skill_atk_bonus"), ("skill_atk_bonus", "ignore_mdef"),
                     ("ignore_mdef", "mdef"), ("mdef", "element")]:
            self.assertLess(m.index(a), m.index(b), (a, b))
        self.assertLess(m.index("card_attacker"), m.index("two_hand_staff"))

    @pending("BR-RULESET-044")
    def test_BR_RULESET_044_magic_damage_behavior(self):
        raise adapter.HostUnavailable("magic pipeline: no golden generator for battle_calc_magic_attack yet (order above is from the rule text)")

    def test_BR_RULESET_022_magic_cardfix_sequence(self):
        self.assertEqual(PIPE["magic_cardfix_order"],
                         ["size", "race2", "element", "attack_element", "race", "class"])

    @pending("BR-RULESET-022")
    def test_BR_RULESET_022_magic_cardfix_rounded_steps(self):
        raise adapter.HostUnavailable("magic card fix: needs the magic golden generator (the weapon card fix of BR-RULESET-020/021 is covered)")


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
        raise adapter.HostUnavailable("bonus vocabulary: the rathena.base registry exists, the RE bonus handlers (bBaseAtk -> eatk, ...) are not written; "
                                      "the size-fix and weapon_damage_rate parts of 102 are golden-covered")

    def test_BR_RULESET_104_refine_data(self):
        self.assertEqual(TABLES["refine"]["weapon_p_atk_s_matk_per_refine"], 2)

    @pending("BR-RULESET-104")
    def test_BR_RULESET_104_refine_behavior(self):
        raise adapter.HostUnavailable("refine contributions (status_calc_pc: patk/smatk +2 per refine, armor RES/MRES) are not run by the golden hook, "
                                      "which feeds finished status values")

    def test_BR_RULESET_066_def2_negative_allowed(self):
        self.assertTrue(TABLES["def2"]["allow_negative"])

    @pending("BR-RULESET-066")
    def test_BR_RULESET_066_def2_behavior(self):
        raise adapter.HostUnavailable("DEF2 contributions are status_calc_* (not run by the hook); only the DEF reduction with a negative soft DEF is covered (BR-RULESET-027)")

    def test_BR_RULESET_068_aspd_caps_data(self):
        self.assertEqual(TABLES["aspd_caps"]["quicken"], 7)

    @pending("BR-RULESET-068")
    def test_BR_RULESET_068_aspd_behavior(self):
        raise adapter.HostUnavailable("ASPD contributions: status_calc, not damage; no golden generator")

    def test_BR_RULESET_054_tick_def2_and_stats(self):
        self.assertEqual(TABLES["tick_def2_ms"]["stun"], 500)
        self.assertEqual(TABLES["status_resist_stat"]["confusion"], "luk")


class Formulas(unittest.TestCase):
    @needs_luau
    def test_BR_RULESET_009_re_atk_parts(self):
        # status / weapon / equip / percent / mastery ATK parts, element and cards per part, shield draw, masteries
        luau_rule_passes(self, "BR-RULESET-009", at_least=2)

    @needs_luau
    def test_BR_RULESET_010_weapon_atk_roll(self):
        run_vectors(self, "BR-RULESET-010")
        luau_rule_passes(self, "BR-RULESET-010", at_least=2)

    @needs_luau
    def test_BR_RULESET_025_res(self):
        run_vectors(self, "BR-RULESET-025")
        luau_rule_passes(self, "BR-RULESET-025", at_least=3)

    @needs_luau
    def test_BR_RULESET_031_element_ratio_additive(self):
        luau_rule_passes(self, "BR-RULESET-031", at_least=2)

    @needs_luau
    def test_BR_RULESET_034_crit_damage(self):
        run_vectors(self, "BR-RULESET-034")
        luau_rule_passes(self, "BR-RULESET-034", at_least=3)

    @pending("BR-RULESET-045")
    def test_BR_RULESET_045_mdef(self):
        run_vectors(self, "BR-RULESET-045")   # formulas.mdef_reduce raises NotImplemented: the vector is hand-derived and no magic golden exists

    @pending("BR-RULESET-048")
    def test_BR_RULESET_048_heal(self):
        run_vectors(self, "BR-RULESET-048")   # skill_calc_heal is not covered (misc path)

    @pending("BR-RULESET-054")
    def test_BR_RULESET_054_level_difference_resistance(self):
        run_vectors(self, "BR-RULESET-054")   # status resistance (status_calc / sc_start), not damage

    @pending("BR-RULESET-057")
    def test_BR_RULESET_057_delayed_status_duration(self):
        run_vectors(self, "BR-RULESET-057")   # status duration, not damage


class Conformance(unittest.TestCase):
    def test_BR_RULESET_119_every_vector_keyed_by_known_rule(self):
        for v in VECTORS:
            self.assertRegex(v["rule"], r"^BR-RULESET-\d{3}$")
            self.assertIn(v["rule"][-3:], ASSIGNED)

    def test_BR_RULESET_119_every_assigned_rule_has_a_test(self):
        src = Path(__file__).read_text()
        for n in ASSIGNED:
            self.assertIn(f"test_BR_RULESET_{n}_", src, n)

    def test_BR_RULESET_119_golden_data_is_recorded_legacy_output(self):
        path = ROOT / "conformance" / "golden" / "weapon_re.jsonl.gz"
        n, draws, cats = 0, 0, set()
        with gzip.open(path, "rt") as f:
            for line in f:
                c = json.loads(line)
                n += 1
                draws += len(c["out"]["draws"])
                cats.add(c["cat"])
                self.assertIn("stages", c["out"])
        self.assertGreaterEqual(n, 2000)
        self.assertGreaterEqual(len(cats), 40)
        self.assertGreater(draws, n)

    @needs_luau
    def test_BR_RULESET_119_golden_trace_replay(self):
        # every recorded case is replayed with its recorded RNG draws; damage, damage2, type, div, flag, dmg_lv, the number, kind and
        # bounds of the draws and the stage probes must all match (tests/acceptance/weapon_golden_test.luau)
        res = adapter.tagged("GOLDEN-WEAPON-RE")
        self.assertGreaterEqual(len(res), 40)
        bad = [r for r in res if r["status"] != "PASS"]
        self.assertFalse(bad, bad)

    @needs_luau
    def test_luau_suite_has_no_failures(self):
        s = adapter.suite()
        self.assertEqual(s["returncode"], 0, s["stdout"][-2000:] + s["stderr"][-2000:])
        self.assertIn("fail=0 xpass=0", s["stdout"])
        self.assertGreaterEqual(len(s["results"]), 100)
        self.assertTrue(all(r["status"] == "PASS" for r in s["results"]))


class Architecture(unittest.TestCase):
    def test_no_runtime_dependency_on_the_other_ruleset(self):
        # the architecture forbids cross-ruleset dependencies: nothing under scripts/ or data/ may mention the other package
        for p in list((ROOT / "scripts").rglob("*.luau")) + list((ROOT / "data").rglob("*.luau")):
            code = "\n".join(l.split("--")[0] for l in p.read_text().splitlines())
            self.assertNotRegex(code, r"prerenewal", str(p))
        imports = set()
        for p in (ROOT / "scripts").rglob("*.luau"):
            imports |= set(re.findall(r'import\("([^"]+)"\)', p.read_text()))
        self.assertTrue(all(i.startswith(("scripts/", "data/")) for i in imports), imports)

    def test_the_package_uses_only_the_documented_engine_api(self):
        # script-host's engine table (api/engine.yaml): pipeline.define / stage / run, num.f32 and config.legacy_quirks. No status, skill,
        # item, storage, rng or event API: this package is pure rules over the pipeline runtime.
        used = set()
        for p in (ROOT / "scripts").rglob("*.luau"):
            code = "\n".join(l.split("--")[0] for l in p.read_text().splitlines())
            used |= set(re.findall(r"\bengine\.([A-Za-z_]+(?:\.[A-Za-z_]+)*)", code))
        self.assertEqual(used, {"pipeline.define", "pipeline.stage", "pipeline.run", "num", "config.legacy_quirks"})

    def test_the_other_ruleset_does_not_depend_on_this_one_either(self):
        # the exclusion is symmetric: nothing in the pre-renewal package's code mentions ruleset.renewal or imports outside itself
        pre = ROOT.parent / "ruleset-prerenewal"
        if not pre.is_dir():
            self.skipTest("ruleset-prerenewal not present")
        for sub in ("scripts", "data", "tests"):
            for p in (pre / sub).rglob("*.luau"):
                code = "\n".join(l.split("--")[0] for l in p.read_text().splitlines())
                self.assertNotRegex(code, r"ruleset[.-]renewal|ruleset_renewal", str(p))
                for imp in re.findall(r'import\("([^"]+)"\)', code):
                    self.assertTrue(imp.startswith(("scripts/", "data/", "tests/", "conformance/", "build/")), (str(p), imp))

    def test_numerics_lint_is_clean(self):
        r = subprocess.run([sys.executable, "-I", str(ROOT / "tools" / "lint_numeric.py")], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stdout)

    def test_diff_report_is_current(self):
        r = subprocess.run([sys.executable, "-I", str(ROOT / "tools" / "diff_report.py"), "--check"], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)

    def test_the_mechanism_the_package_sits_on_was_not_touched(self):
        # engine-core, native-defaults and rathena.base (mods/base) needed no change. script-host did: it gained the pipeline runtime,
        # num.f32 and engine.config (ARCHITECTURE_PROOF.md section 12), and ruleset-prerenewal was refactored onto it; neither is watched.
        a = load("docs/pipeline_diff.json")["audit"]
        self.assertEqual(a["changed_outside_package"], [])
        self.assertEqual(a["engine_api"], ["config.legacy_quirks", "num", "pipeline.define", "pipeline.run", "pipeline.stage"])
        self.assertEqual(a["cross_ruleset_references"], [])

    def test_the_pipeline_api_reproduces_the_renewal_order_from_the_prerenewal_names(self):
        a = load("docs/pipeline_diff.json")["api"]
        self.assertTrue(a["reproduces_re_order"])
        self.assertEqual(a["define_only"], 1)
        # without `move`: 23 operations; script-host's real runtime executes exactly this list (script-host/tests/test_rulesets.cpp)
        self.assertEqual(a["counts"], {"cancel": 3, "insert_after": 12, "replace": 8})
        self.assertEqual(len(a["ops"]), 23)
        # with `move`: the two stages that only changed position (card_attacker, skill_atk_bonus) are moved and keep their ids
        m = a["with_move"]
        self.assertTrue(m["reproduces_re_order"])
        self.assertEqual(m["counts"], {"cancel": 1, "move": 2, "insert_after": 10, "replace": 8})
        self.assertEqual(sorted(o[1] for o in m["ops"] if o[0] == "move"), ["card_attacker", "skill_atk_bonus"])


if __name__ == "__main__":
    unittest.main()
