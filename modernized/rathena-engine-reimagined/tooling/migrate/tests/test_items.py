import unittest
from helpers import *  # noqa
from rmig.db_items import ItemDb


def build(body, overlay=None, mode="prerenewal"):
    files = {"db/item_db.yml": stub("ITEM_DB", 3, body=body, imports=["db/import/item_db.yml"])}
    if overlay:
        files["db/import/item_db.yml"] = stub("ITEM_DB", 3, body=overlay)
    t = Tree(files)
    a = items_app(t.root, mode)
    t.cleanup()
    return a


BASE = """  - Id: 501
    AegisName: Red_Potion
    Name: Red Potion
    Type: Healing
    Buy: 50
    Weight: 70
    Script: |
      itemheal rand(45,65),0;
  - Id: 1101
    AegisName: Sword
    Name: Sword
    Type: Weapon
    SubType: 1hSword
    Buy: 100
    Weight: 500
    Attack: 25
    Range: 1
    Slots: 3
    Jobs:
      Swordman: true
      Knight: true
    Locations:
      Right_Hand: true
    WeaponLevel: 1
"""


class ItemTests(unittest.TestCase):
    def test_per_field_patch_of_existing_id(self):
        a = build(BASE, "  - Id: 501\n    Weight: 55\n")
        r = a.rows[501]
        self.assertEqual(r["weight"], 55)
        self.assertEqual(r["buy"], 50)                       # untouched field keeps its value
        self.assertIn("itemheal", r["script"])               # so does the script
        self.assertEqual(r["name"], "Red Potion")

    def test_mandatory_fields_on_create_only(self):
        a = build(BASE, "  - Id: 600\n    Name: NoAegis\n  - Id: 601\n    AegisName: NoName\n  - Id: 501\n    Weight: 1\n")
        self.assertNotIn(600, a.rows)
        self.assertNotIn(601, a.rows)
        self.assertIn(501, a.rows)                           # an existing id needs only the fields it patches
        self.assertEqual(len(a.rejected), 2)

    def test_partial_failure_keeps_earlier_patches(self):
        a = build(BASE, "  - Id: 501\n    Buy: 123\n    Weight: heavy\n    Slots: 4\n")
        r = a.rows[501]
        self.assertEqual(r["buy"], 123)                      # applied before the failing field
        self.assertEqual(r["weight"], 70)                    # failing field untouched
        self.assertEqual(r["slots"], 0)                      # later fields are not reached
        self.assertEqual(len(a.rejected), 1)

    def test_duplicate_aegis_name_rejected_and_rename_updates_lookup(self):
        a = build(BASE, "  - Id: 700\n    AegisName: red_potion\n    Name: X\n  - Id: 501\n    AegisName: Crimson\n")
        self.assertNotIn(700, a.rows)
        self.assertIsNone(a.search_aegis("Red_Potion"))
        self.assertEqual(a.search_aegis("crimson")["id"], 501)

    def test_jobs_replace_set_but_classes_patch_mask(self):
        a0 = build(BASE)
        a = build(BASE, "  - Id: 1101\n    Jobs:\n      Novice: true\n    Classes:\n      Upper: false\n")
        self.assertNotEqual(a0.rows[1101]["class_base"], a.rows[1101]["class_base"])
        self.assertEqual(a.rows[1101]["class_base"][1:], [0, 0])
        self.assertEqual(a.rows[1101]["class_upper"], 0x07 & ~0x02)       # default ITEMJ_ALL (pre-re) minus Upper
        a2 = build(BASE, "  - Id: 1101\n    Jobs:\n      All: true\n      Novice: false\n")
        self.assertEqual(a2.rows[1101]["class_base"][0] & 1, 0)           # All then individual removal

    def test_price_derivation_uses_only_the_last_node_that_touched_the_item(self):
        a = build(BASE)
        self.assertEqual(a.rows[501]["sell"], 25)                          # Buy only -> Sell = Buy/2
        b = build(BASE, "  - Id: 501\n    Weight: 5\n")
        self.assertEqual(b.rows[501]["sell"], 0)                           # legacy quirk: the patch node reset hasPriceValue
        c = build("  - Id: 9\n    AegisName: S\n    Name: S\n    Sell: 30\n")
        self.assertEqual(c.rows[9]["buy"], 60)                             # Sell only -> Buy = 2*Sell
        d = build("  - Id: 9\n    AegisName: S\n    Name: S\n    Buy: 10\n    Sell: 30\n")
        self.assertEqual(d.rows[9]["sell"], 1)                             # exploit guard

    def test_new_equip_without_locations_becomes_etc(self):
        a = build("  - Id: 5\n    AegisName: A\n    Name: A\n    Type: Armor\n")
        self.assertEqual(a.rows[5]["type"], a.t_etc)

    def test_delayconsume_converted_in_loading_finished(self):
        a = build("  - Id: 5\n    AegisName: A\n    Name: A\n    Type: Delayconsume\n")
        self.assertEqual(a.rows[5]["type"], a.t_usable)
        self.assertEqual(a.rows[5]["delay_consume"] & 1, 1)

    def test_level_caps_and_gender_rules(self):
        a = build("  - Id: 5\n    AegisName: A\n    Name: A\n    Type: Weapon\n    SubType: Whip\n    Locations:\n      Right_Hand: true\n"
                  "    EquipLevelMin: 20\n    EquipLevelMax: 10\n")
        self.assertEqual(a.rows[5]["elvmax"], 20)
        self.assertEqual(a.rows[5]["sex"], a.c.get("SEX_FEMALE"))
        self.assertEqual(a.rows[5]["wlv"], 1)                               # weapon level defaults to 1 in loadingFinished

    def test_alias_requires_an_earlier_item(self):
        a = build(BASE, "  - Id: 800\n    AegisName: Alias_A\n    Name: A\n    AliasName: Sword\n"
                        "  - Id: 801\n    AegisName: Alias_B\n    Name: B\n    AliasName: Defined_Later\n"
                        "  - Id: 802\n    AegisName: Defined_Later\n    Name: C\n")
        self.assertEqual(a.rows[800]["view_id"], 1101)
        self.assertNotIn(801, a.rows)
        self.assertIn(802, a.rows)

    def test_invalid_enum_values_fall_back_like_the_loader(self):
        a = build("  - Id: 5\n    AegisName: A\n    Name: A\n    Type: Nonsense\n    Flags:\n      DropEffect: Nope\n")
        self.assertEqual(a.rows[5]["type"], a.t_etc)
        self.assertEqual(a.rows[5]["dropEffect"], 0)

    def test_dummy_item_is_synthesised(self):
        a = build(BASE)
        self.assertEqual(a.rows[499]["aegis"], "UNKNOWN_ITEM")

    def test_renewal_has_magic_attack(self):
        a = build("  - Id: 5\n    AegisName: A\n    Name: A\n    Type: Weapon\n    MagicAttack: 50\n    Locations:\n      Right_Hand: true\n", mode="renewal")
        self.assertEqual(a.rows[5]["matk"], 50)
        b = build("  - Id: 5\n    AegisName: A\n    Name: A\n    Type: Weapon\n    MagicAttack: 50\n    Locations:\n      Right_Hand: true\n")
        self.assertNotIn("matk", b.rows[5])

    def test_mandatory_ids_and_types_malformed(self):
        a = build("  - AegisName: A\n    Name: A\n  - Id: abc\n    AegisName: B\n    Name: B\n  - Id: -1\n    AegisName: C\n    Name: C\n")
        self.assertEqual(set(a.rows), {499})
        self.assertEqual(len(a.rejected), 3)

    def test_script_text_is_verbatim(self):
        a = build("  - Id: 5\n    AegisName: A\n    Name: A\n    Script: |\n      bonus bStr,1; // c\n      if (1) { }\n")
        self.assertEqual(a.rows[5]["script"], "bonus bStr,1; // c\nif (1) { }\n")


if __name__ == "__main__":
    unittest.main()
