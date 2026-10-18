import unittest
from helpers import *  # noqa
from rmig.db_items import ItemDb
from rmig.db_mobs import MobDb, RandOptGroupNames
from rmig.base import World, load_chain

ITEMS = "".join("  - Id: %d\n    AegisName: Item_%d\n    Name: Item %d\n" % (i, i, i) for i in range(500, 520))

MOB = """  - Id: 1002
    AegisName: PORING
    Name: Poring
    Level: 1
    Hp: 50
    Attack: 7
    Attack2: 10
    Ai: 02
    Modes:
      Looter: true
    Drops:
%s
"""
DROPS8 = "".join("      - Item: Item_%d\n        Rate: %d\n" % (500 + i, 100 + i) for i in range(8))


def build(base_mob_body, overlay="", mode="prerenewal", clear=False):
    files = {
        "db/item_db.yml": stub("ITEM_DB", 3, body=ITEMS),
        "db/mob_db.yml": stub("MOB_DB", 5, body=base_mob_body, imports=["db/import/mob_db.yml"]),
    }
    if overlay:
        files["db/import/mob_db.yml"] = stub("MOB_DB", 5, body=overlay, clear=clear)
    t = Tree(files)
    w = world(t.root, mode)
    items = ItemDb(w)
    load_chain(items, items.STUB)
    items.finish()
    ro = RandOptGroupNames(w)
    mobs = MobDb(w, items, ro)
    load_chain(mobs, mobs.STUB)
    mobs.finish()
    t.cleanup()
    return mobs


def drops(m, id_=1002, key="drops"):
    return [(d["nameid"], d["rate"]) for d in m.rows[id_][key]]


class MobTests(unittest.TestCase):
    def setUp(self):
        self.base = MOB % DROPS8

    def test_index_patch_rules(self):
        ov = """  - Id: 1002
    Drops:
      - Index: 0
        Item: Item_510
        Rate: 5000
      - Index: 3
        Item: Item_511
        Rate: 123
        StealProtected: true
      - Index: 8
        Item: Item_512
        Rate: 10
      - Index: 20
        Item: Item_513
        Rate: 1
      - Item: Item_514
        Rate: 5
      - Item: Item_515
        Rate: 5
      - Index: 1
        Item: No_Such_Item
        Rate: 5
      - Index: 15
        Item: Item_516
        Rate: 5
"""
        m = build(self.base, ov)
        d = m.rows[1002]["drops"]
        self.assertEqual(len(d), 10)                                         # MAX_MOB_DROP
        self.assertEqual((d[0]["nameid"], d[0]["rate"]), (510, 5000))        # overwritten
        self.assertEqual((d[3]["nameid"], d[3]["rate"], d[3]["steal"]), (511, 123, True))
        self.assertEqual((d[1]["nameid"], d[1]["rate"]), (501, 101))         # unknown item: entry untouched
        self.assertEqual(d[8]["nameid"], 512)                                # Index == size appends
        self.assertEqual(d[9]["nameid"], 514)                                # no Index appends
        self.assertNotIn(513, [x["nameid"] for x in d])                      # Index >= MAX skipped
        self.assertNotIn(515, [x["nameid"] for x in d])                      # list full
        self.assertNotIn(516, [x["nameid"] for x in d])                      # Index > size ignored

    def test_overwrite_resets_unlisted_drop_fields(self):
        base = self.base.replace("Item: Item_500\n        Rate: 100\n", "Item: Item_500\n        Rate: 100\n        StealProtected: true\n")
        m = build(base)
        self.assertTrue(m.rows[1002]["drops"][0]["steal"])
        m = build(base, "  - Id: 1002\n    Drops:\n      - Index: 0\n        Item: Item_500\n        Rate: 7\n")
        self.assertFalse(m.rows[1002]["drops"][0]["steal"])                  # StealProtected defaults to false on overwrite

    def test_drop_rate_is_mandatory_and_bounded(self):
        m = build(self.base, "  - Id: 1002\n    Drops:\n      - Item: Item_517\n")
        self.assertEqual(len(m.rows[1002]["drops"]), 8)
        self.assertEqual(len(m.rejected), 1)
        m = build(self.base, "  - Id: 1002\n    Drops:\n      - Item: Item_517\n        Rate: 10001\n")
        self.assertEqual(len(m.rejected), 1)

    def test_modes_patch_bits_after_ai(self):
        m = build(self.base, "  - Id: 1002\n    Modes:\n      Aggressive: true\n      Looter: false\n")
        c = m.c
        mode = m.rows[1002]["mode"]
        self.assertTrue(mode & c.get("MD_AGGRESSIVE"))
        self.assertFalse(mode & c.get("MD_LOOTER"))
        # Ai replaces the whole mask, Modes then patch it (order inside the node)
        m = build(self.base, "  - Id: 1002\n    Ai: 25\n")
        self.assertEqual(m.rows[1002]["mode"], c.get("MONSTER_TYPE_25"))     # Ai resets the Looter bit set by the base Modes

    def test_class_boss_adds_modes_in_loading_finished(self):
        m = build(self.base, "  - Id: 1002\n    Class: Boss\n")
        c = m.c
        for bit in ("DETECTOR", "STATUSIMMUNE", "KNOCKBACKIMMUNE"):
            self.assertTrue(m.rows[1002]["mode"] & c.get("MD_" + bit))

    def test_new_mob_needs_aegis_and_name_and_valid_id(self):
        m = build(self.base, "  - Id: 1003\n    AegisName: X\n  - Id: 999\n    AegisName: Y\n    Name: Y\n  - Id: 31990\n    AegisName: Z\n    Name: Z\n")
        self.assertNotIn(1003, m.rows)
        self.assertNotIn(999, m.rows)
        self.assertIn(31990, m.rows)
        self.assertEqual(m.rows[31990]["jname"], "Z")                        # JapaneseName defaults to Name
        self.assertEqual(m.rows[31990]["clientamotion"], m.rows[31990]["amotion"])

    def test_race_groups_add_and_remove(self):
        m = build(self.base, "  - Id: 1002\n    RaceGroups:\n      Goblin: true\n  - Id: 1002\n    RaceGroups:\n      Goblin: false\n")
        self.assertEqual(m.rows[1002]["race2"], [])

    def test_clear_header_drops_everything_loaded_before(self):
        m = build(self.base, "  - Id: 31990\n    AegisName: Only\n    Name: Only\n", clear=True)
        self.assertEqual(set(m.rows), {31990})

    def test_attack2_goes_to_atk2_in_prerenewal_and_matk_in_renewal(self):
        pre = build(self.base)
        self.assertEqual((pre.rows[1002]["atk"], pre.rows[1002]["atk2"]), (7, 10))
        ren = build(self.base, mode="renewal")
        self.assertEqual(ren.rows[1002]["matk"], 10)
        self.assertEqual((ren.rows[1002]["atk"], ren.rows[1002]["atk2"]), (5, 8))   # status_calc_misc: 80% / 120%
        self.assertEqual(ren.rows[1002]["_attack"], 7)

    def test_names_are_capped_at_24(self):
        m = build(self.base, "  - Id: 1002\n    Name: ABCDEFGHIJKLMNOPQRSTUVWXYZ\n")
        self.assertEqual(m.rows[1002]["name"], "ABCDEFGHIJKLMNOPQRSTUVWX")


if __name__ == "__main__":
    unittest.main()
