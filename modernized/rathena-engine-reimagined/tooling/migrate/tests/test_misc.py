import subprocess
import tempfile
import os
import unittest
from helpers import *  # noqa
from rmig import luau, diffreport
from rmig.emit import slug
from rmig.db_misc import SizeFixDb
from rmig.db_items import ItemDb

LUAU = os.environ.get("RMIG_LUAU", "/tmp/claude-0/-home-user-reAthena/1d67890b-8b04-5f37-9297-104bbabb4a76/scratchpad/luau-src/b/luau")


class Loaded(object):
    def __init__(self, w, **dbs):
        self.w = w
        self.dbs = dbs


class SerializerTests(unittest.TestCase):
    def test_values(self):
        self.assertEqual(luau.val(True), "true")
        self.assertEqual(luau.val([1, 2, 3]), "{1,2,3}")
        self.assertEqual(luau.val({"a": 1, "end": 2, "x y": 3}).replace("\n", "").replace("\t", ""), '{a = 1,["end"] = 2,["x y"] = 3,}')
        self.assertEqual(luau.q('a"b\\c\n\x01'), '"a\\"b\\\\c\\n\\001"')
        notes = []
        self.assertEqual(luau.val(1 << 60, notes=notes), '"%d"' % (1 << 60))
        self.assertTrue(notes)

    def test_slug(self):
        self.assertEqual(slug("Red_Potion"), "red_potion")
        self.assertEqual(slug("Scorpion's Tail!"), "scorpion_s_tail")
        self.assertEqual(slug("___"), "unnamed")

    @unittest.skipUnless(os.path.exists(LUAU), "Luau CLI not available")
    def test_round_trip_through_luau(self):
        tricky = "line1\nline2 \"quoted\" \\ back\ttab \x07 bell é中"
        text = luau.module(["x"], [("ns:kind/a", {"id": "ns:kind/a", "s": tricky, "n": [1, 2], "t": {"k": True}})])
        d = tempfile.mkdtemp(prefix="rmig-lu-")
        with open(os.path.join(d, "m.luau"), "w", encoding="utf-8") as f:
            f.write(text)
        with open(os.path.join(d, "t.luau"), "w", encoding="utf-8") as f:
            f.write('local m = require("./m")\nlocal e = m["ns:kind/a"]\nprint(#e.s)\nprint(e.s)\nprint(e.n[2], e.t.k)\n')
        p = subprocess.run([LUAU, "t.luau"], cwd=d, capture_output=True, text=True)
        self.assertEqual(p.returncode, 0, p.stderr)
        self.assertEqual(p.stdout.split("\n", 1)[1].rsplit("\n2\ttrue", 1)[0].rstrip("\n"), tricky)


class DiffReportTests(unittest.TestCase):
    def test_size_fix_diff(self):
        pre = Tree({"db/size_fix.yml": stub("SIZE_FIX_DB", 1, body="  - Weapon: Dagger\n    Small: 50\n  - Weapon: Mace\n    Large: 20\n")})
        ren = Tree({"db/size_fix.yml": stub("SIZE_FIX_DB", 1, body="  - Weapon: Dagger\n    Small: 60\n  - Weapon: Staff\n    Large: 20\n  - Weapon: Mace\n    Large: 20\n")})
        a = load_app(SizeFixDb, pre.root, "prerenewal")
        b = load_app(SizeFixDb, ren.root, "renewal")
        pre.cleanup(), ren.cleanup()
        d = diffreport.diff_db("size_fix", Loaded(a.w, size_fix=a), Loaded(b.w, size_fix=b))
        self.assertEqual((d["pre"], d["re"], d["common"], d["added_in_re"], d["removed_in_re"]), (2, 3, 2, 1, 0))
        self.assertEqual((d["identical"], d["changed"]), (1, 1))
        self.assertEqual(d["fields"], {"small": 1})

    def test_item_matk_and_class_all_are_not_changes(self):
        body = "  - Id: 5\n    AegisName: A\n    Name: A\n"
        pre = Tree({"db/item_db.yml": stub("ITEM_DB", 3, body=body)})
        ren = Tree({"db/item_db.yml": stub("ITEM_DB", 3, body=body)})
        a = items_app(pre.root, "prerenewal")
        b = items_app(ren.root, "renewal")
        pre.cleanup(), ren.cleanup()
        d = diffreport.diff_db("item_db", Loaded(a.w, item_db=a), Loaded(b.w, item_db=b))
        self.assertEqual(d["changed"], 0)             # ITEMJ_ALL differs per build (7 vs 127), the data does not
        text = diffreport.render([d], {"version": "t"})
        self.assertIn("| item_db |", text)
        self.assertIn("shared (rathena.base candidate)", text)


if __name__ == "__main__":
    unittest.main()
