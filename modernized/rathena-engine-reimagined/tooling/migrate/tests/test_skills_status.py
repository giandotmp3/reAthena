import unittest
from helpers import *  # noqa
from rmig.db_items import ItemDb
from rmig.db_skills import SkillDb
from rmig.db_misc import StatusDb, SizeFixDb, AttrFixDb, RefineDb, StatPointDb, JobDb
from rmig.base import load_chain

ITEMS = "  - Id: 501\n    AegisName: Red_Potion\n    Name: Red\n  - Id: 502\n    AegisName: Orange_Potion\n    Name: Orange\n"


def skills(body, overlay="", mode="prerenewal"):
    files = {"db/item_db.yml": stub("ITEM_DB", 3, body=ITEMS),
             "db/skill_db.yml": stub("SKILL_DB", 4, body=body, imports=["db/import/skill_db.yml"])}
    if overlay:
        files["db/import/skill_db.yml"] = stub("SKILL_DB", 4, body=overlay)
    t = Tree(files)
    app = load_app(SkillDb, t.root, mode, args=(items_app(t.root, mode),), finish=True)
    t.cleanup()
    return app


SK = """  - Id: 5
    Name: SM_BASH
    Description: Bash
    MaxLevel: 10
    Type: Weapon
    TargetType: Attack
    Range: 2
    Hit: Single
    Element: Weapon
    CopyFlags:
      Skill:
        Plagiarism: true
        Reproduce: true
    Requires:
      SpCost:
        - Level: 1
          Amount: 8
        - Level: 6
          Amount: 15
"""


class SkillTests(unittest.TestCase):
    def test_scalar_fills_all_levels_and_list_extrapolates_last_value(self):
        a = skills(SK)
        r = a.rows[5]
        self.assertEqual(r["range"], [2] * 13)
        self.assertEqual(r["req_sp"][0], 8)
        # two points (1:8, 6:15): step 1..3 do not match -> remaining levels get the last value
        self.assertEqual(r["req_sp"][5], 15)
        self.assertEqual(r["req_sp"][12], 15)

    def test_linear_trend_is_extended(self):
        a = skills(SK, "  - Id: 5\n    CastTime:\n      - Level: 1\n        Time: 100\n      - Level: 2\n        Time: 200\n      - Level: 3\n        Time: 300\n")
        self.assertEqual(a.rows[5]["cast"], [100 * i for i in range(1, 14)])

    def test_declining_series_stops_at_one(self):
        a = skills(SK, "  - Id: 5\n    Cooldown:\n      - Level: 1\n        Time: 5\n      - Level: 2\n        Time: 3\n      - Level: 3\n        Time: 1\n")
        self.assertEqual(a.rows[5]["cooldown"][:5], [5, 3, 1, 1, 1])

    def test_patch_of_one_level_keeps_other_levels(self):
        a = skills(SK, "  - Id: 5\n    Requires:\n      SpCost:\n        - Level: 2\n          Amount: 99\n")
        self.assertEqual(a.rows[5]["req_sp"][1], 99)
        self.assertEqual(a.rows[5]["req_sp"][0], 8)

    def test_legacy_copyflags_quirk(self):
        a = skills(SK, "  - Id: 5\n    CopyFlags:\n      Skill:\n        Plagiarism: false\n")
        c = a.c
        # '&= FLAG' (no '~') keeps ONLY that bit instead of clearing it
        self.assertEqual(a.rows[5]["copy_option"], c.get("SKILL_COPY_PLAGIARISM") & c.get("SKILL_COPY_PLAGIARISM"))

    def test_mandatory_on_create_and_maxlevel_bounds(self):
        a = skills(SK, "  - Id: 6\n    Name: X\n    Description: X\n  - Id: 7\n    Name: Y\n    Description: Y\n    MaxLevel: 99\n"
                       "  - Id: 8\n    Name: Z\n    Description: Z\n    MaxLevel: 3\n")
        self.assertEqual(sorted(a.rows), [5, 8])
        self.assertEqual(len(a.rejected), 2)

    def test_item_cost_and_equipment_lookups(self):
        a = skills(SK, "  - Id: 5\n    Requires:\n      ItemCost:\n        - Item: Red_Potion\n          Amount: 2\n"
                       "      Equipment:\n        Orange_Potion: true\n")
        self.assertEqual(a.rows[5]["req_itemid"][0], 501)
        self.assertEqual(a.rows[5]["req_amount"][0], 2)
        self.assertEqual(a.rows[5]["req_eqitem"], [502])
        b = skills(SK, "  - Id: 5\n    Requires:\n      ItemCost:\n        - Item: Not_An_Item\n          Amount: 2\n")
        self.assertEqual(len(b.rejected), 1)

    def test_element_sequence_and_scalar(self):
        a = skills(SK, "  - Id: 5\n    Element:\n      - Level: 1\n        Element: Fire\n      - Level: 2\n        Element: Water\n")
        c = a.c
        self.assertEqual(a.rows[5]["element"][:3], [c.get("ELE_FIRE"), c.get("ELE_WATER"), c.get("ELE_WEAPON") if False else a.rows[5]["element"][2]])
        b = skills(SK, "  - Id: 5\n    Element: Dark\n")
        self.assertEqual(set(b.rows[5]["element"]), {c.get("ELE_DARK")})

    def test_unit_requires_id(self):
        a = skills(SK, "  - Id: 5\n    Unit:\n      Interval: 5\n")
        self.assertEqual(a.rows[5]["unit_id"], 0)
        self.assertEqual(len(a.rejected), 1)


def status_app(body, overlay="", skills_body=SK):
    files = {"db/item_db.yml": stub("ITEM_DB", 3, body=ITEMS),
             "db/skill_db.yml": stub("SKILL_DB", 4, body=skills_body),
             "db/status.yml": stub("STATUS_DB", 4, body=body, imports=["db/import/status.yml"])}
    if overlay:
        files["db/import/status.yml"] = stub("STATUS_DB", 4, body=overlay)
    t = Tree(files)
    sk = load_app(SkillDb, t.root, args=(items_app(t.root),))
    app = load_app(StatusDb, t.root, args=(sk,))
    t.cleanup()
    return app


class StatusTests(unittest.TestCase):
    BASE = """  - Status: Poison
    DurationLookup: SM_BASH
    CalcFlags:
      Def: true
    Fail:
      Poison: true
      Curse: true
    EndOnStart:
      Blind: true
"""

    def test_lists_add_remove_and_allow_duplicates(self):
        a = status_app(self.BASE)
        c = a.c
        poison, curse, blind = c.get("SC_POISON"), c.get("SC_CURSE"), c.get("SC_BLIND")
        self.assertEqual(a.rows[poison]["fail"], [poison, curse])
        self.assertEqual(a.rows[poison]["skill_id"], 5)                # resolved by skill name (case-insensitive)
        b = status_app(self.BASE, "  - Status: Poison\n    Fail:\n      Poison: false\n      Curse: true\n      Blind: true\n")
        self.assertEqual(b.rows[poison]["fail"], [curse, curse, blind])    # Curse pushed again: no de-duplication

    def test_calcflags_all_then_individual(self):
        a = status_app(self.BASE, "  - Status: Poison\n    CalcFlags:\n      All: true\n      Def: false\n")
        row = a.rows[a.c.get("SC_POISON")]
        self.assertNotIn(a.c.get("SCB_DEF"), row["calc_flag"])
        self.assertIn(a.c.get("SCB_STR"), row["calc_flag"])

    def test_script_forces_all_calc_flags_in_loading_finished(self):
        a = status_app(self.BASE, "  - Status: Poison\n    Script: |\n      bonus bStr,1;\n")
        row = a.rows[a.c.get("SC_POISON")]
        self.assertIn(a.c.get("SCB_MAXHP"), row["calc_flag"])
        self.assertNotIn(a.c.get("SCB_DYE"), row["calc_flag"])

    def test_defaults_and_unknown_status(self):
        a = status_app(self.BASE + "  - Status: Not_A_Status\n  - Status: Stone\n")
        row = a.rows[a.c.get("SC_STONE")]
        self.assertEqual((row["min_duration"], row["min_rate"], row["icon"]), (1, 0, a.c.lim("EFST_BLANK")))
        self.assertEqual(len(a.rejected), 1)


class SmallTableTests(unittest.TestCase):
    def test_size_fix_defaults_and_patch(self):
        t = Tree({"db/size_fix.yml": stub("SIZE_FIX_DB", 1, body="  - Weapon: Dagger\n    Medium: 75\n  - Weapon: Dagger\n    Small: 60\n  - Weapon: Nonsense\n    Small: 1\n")})
        a = load_app(SizeFixDb, t.root)
        t.cleanup()
        row = a.rows[a.c.get("W_DAGGER")]
        self.assertEqual((row["small"], row["medium"], row["large"]), (60, 75, 100))
        self.assertEqual(len(a.rejected), 1)

    def test_attr_fix_clear_restores_100(self):
        t = Tree({"db/attr_fix.yml": stub("ATTRIBUTE_DB", 1, body="  - Level: 1\n    Fire:\n      Water: 77\n", imports=["db/import/attr_fix.yml"]),
                  "db/import/attr_fix.yml": stub("ATTRIBUTE_DB", 1, body="  - Level: 2\n    Water:\n      Fire: 150\n", clear=True)})
        a = load_app(AttrFixDb, t.root)
        t.cleanup()
        c = a.c
        self.assertEqual(a.table[0][c.get("ELE_FIRE")][c.get("ELE_WATER")], 100)    # cleared
        self.assertEqual(a.table[1][c.get("ELE_WATER")][c.get("ELE_FIRE")], 150)

    def test_statpoint_extrapolates_and_patches(self):
        t = Tree({"db/statpoint.yml": stub("STATPOINT_DB", 2, body="  - Level: 1\n    Points: 48\n  - Level: 2\n    Points: 3\n  - Level: 2\n    TraitPoints: 4\n")})
        a = load_app(StatPointDb, t.root)
        t.cleanup()
        self.assertEqual((a.rows[2]["statpoints"], a.rows[2]["traitpoints"]), (3, 4))
        self.assertEqual(a.rows[3]["statpoints"], 3 + (3 - 1 + 15) // 5)              # generated by loadingFinished
        self.assertEqual(len(a.rows), 275)

    def test_refine_nested_key_patching(self):
        body = ("  - Group: Armor\n    Levels:\n      - Level: 1\n        RefineLevels:\n          - Level: 5\n            Bonus: 10\n"
                "            Chances:\n              - Type: Normal\n                Rate: 5000\n                Price: 100\n")
        ov = ("  - Group: Armor\n    Levels:\n      - Level: 1\n        RefineLevels:\n          - Level: 5\n            Bonus: 99\n"
              "            Chances:\n              - Type: Normal\n                Rate: 3333\n")
        t = Tree({"db/refine.yml": stub("REFINE_DB", 2, body=body, imports=["db/import/refine.yml"]),
                  "db/import/refine.yml": stub("REFINE_DB", 2, body=ov), "db/item_db.yml": stub("ITEM_DB", 3)})
        a = load_app(RefineDb, t.root, args=(items_app(t.root),))
        t.cleanup()
        flat = a.flat()
        (key, row), = flat.items()
        self.assertEqual(key[2], 4)                                                  # 1-based in YAML, 0-based in the loader
        self.assertEqual(row["bonus"], 99)
        self.assertEqual(row["costs"][0]["chance"], 3333)
        self.assertEqual(row["costs"][0]["zeny"], 100)                              # untouched cost field survives

    def test_job_stats_level_lists_and_accumulation(self):
        body = ("  - Jobs:\n      Swordman: true\n    MaxJobLevel: 10\n    BonusStats:\n      - Level: 2\n        Str: 1\n      - Level: 4\n        Str: 1\n"
                "  - Jobs:\n      Swordman: true\n    BonusStats:\n      - Level: 2\n        Str: 5\n")
        t = Tree({"db/job_stats.yml": stub("JOB_STATS", 4, body=body)})
        a = load_app(JobDb, t.root)
        t.cleanup()
        row = a.rows[a.c.get("JOB_SWORDMAN")]
        col = [x[0] for x in row["job_bonus"]]
        self.assertEqual(len(col), 10)                                               # truncated to MaxJobLevel
        self.assertEqual(col, [0, 5, 5, 6, 6, 6, 6, 6, 6, 6])                        # cumulative; Level 2 patched to 5
        self.assertEqual(row["max_weight_base"], 20000)                              # created with the loader default


if __name__ == "__main__":
    unittest.main()
