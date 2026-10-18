"""The migrator's merged result against snapshots of the REAL legacy loader (tests/fixtures/legacy_golden.json, made by
tools/make_golden.py from the dump hook). The complete comparison is tools/compare_legacy.py (see README)."""
import json
import os
import unittest

from helpers import *  # noqa
import compare_legacy as cl
import runs

GOLD = json.load(open(os.path.join(runs.FIXTURES, "legacy_golden.json")))
KEYS = {"item_db": "id", "mob_db": "id", "skill_db": "id", "status": "type", "size_fix": "weapon", "attr_fix": "level",
        "statpoint": "level", "job_stats": "job"}
MAPPERS = {"item_db": (cl.row_item, None), "mob_db": (None, cl.legacy_mob), "status": (cl.row_status, None)}


def check(tc, loaded, scen, mode, only_present=True):
    g = GOLD[mode][scen]
    for db, ent in g.items():
        rows = loaded.dbs[db].rows
        if ent.get("rows") is None:
            continue
        key = KEYS[db]
        want = [r for r in ent["rows"].values()]
        mine = {r[key]: rows[r[key]] for r in want if r[key] in rows}
        mapper, lmap = MAPPERS.get(db, (None, None))
        res = cl.compare(db, want, key, mine, mapper, legacy_mapper=lmap)
        unexplained = {f: v for f, v in res["mismatched_fields"].items()}
        tc.assertEqual(unexplained, {}, "%s/%s/%s" % (mode, scen, db))
        tc.assertEqual(res["only_in_legacy_count"], 0, "%s/%s/%s: ids the legacy loader has but the migrator lacks" % (mode, scen, db))
        for k in ent.get("absent", []):
            tc.assertNotIn(k, rows, "%s/%s/%s: %s must be rejected" % (mode, scen, db, k))
        if scen in ("base", "clear") and db in ("item_db", "mob_db", "skill_db", "status", "size_fix", "statpoint", "job_stats"):
            tc.assertEqual(len(rows), ent["total"], "%s/%s/%s entry count" % (mode, scen, db))
        if scen == "patch" and db in ("item_db", "mob_db", "skill_db", "status"):
            tc.assertEqual(len(rows), ent["total"], "%s/%s/%s entry count" % (mode, scen, db))


class GoldenPrerenewal(unittest.TestCase):
    def test_base_sample(self):
        loaded, _, _ = runs.cli_run("prerenewal")
        check(self, loaded, "base", "prerenewal")

    def test_overlay_patch(self):
        check(self, runs.overlay("prerenewal", "patch"), "patch", "prerenewal")

    def test_overlay_clear(self):
        check(self, runs.overlay("prerenewal", "clear"), "clear", "prerenewal")


@unittest.skipUnless(os.environ.get("RMIG_FULL") == "1", "renewal full-database tests are slow (set RMIG_FULL=1)")
class GoldenRenewal(unittest.TestCase):
    def test_base_sample(self):
        loaded, _, _ = runs.cli_run("renewal")
        check(self, loaded, "base", "renewal")

    def test_overlay_patch(self):
        check(self, runs.overlay("renewal", "patch"), "patch", "renewal")

    def test_overlay_clear(self):
        check(self, runs.overlay("renewal", "clear"), "clear", "renewal")


class OverlayBehaviour(unittest.TestCase):
    """Readable assertions of what the golden snapshots encode (all verified against the real loader)."""

    def test_patch_overlay_semantics(self):
        L = runs.overlay("prerenewal", "patch")
        it = L.dbs["item_db"].rows
        self.assertEqual((it[501]["buy"], it[501]["sell"], it[501]["weight"]), (77, 38, 55))
        self.assertEqual((it[502]["buy"], it[502]["slots"]), (123, 0))               # partial failure keeps Buy, never reaches Slots
        self.assertNotIn(31992, it)                                                    # duplicate AegisName
        self.assertEqual(it[31991]["elvmax"], 10)                                      # EquipLevelMax capped to Min
        self.assertEqual(it[31993]["type"], L.dbs["item_db"].t_etc)                    # armor without Locations
        mob = L.dbs["mob_db"].rows
        self.assertEqual(len(mob[1002]["drops"]), 10)
        self.assertIn(31990, mob)
        self.assertNotIn(31991, mob)
        self.assertEqual(mob[1002]["drops"][3]["rate"], 123)
        sk = L.dbs["skill_db"].rows
        self.assertEqual(sk[5]["cast"][:4], [100, 200, 300, 400])
        self.assertNotIn(29991, sk)

    def test_clear_overlay_semantics(self):
        L = runs.overlay("prerenewal", "clear")
        self.assertEqual(set(L.dbs["mob_db"].rows), {1002, 31990})
        self.assertEqual(len(L.dbs["size_fix"].rows), 2)
        self.assertEqual(len(L.dbs["status"].rows), 2)


if __name__ == "__main__":
    unittest.main()
