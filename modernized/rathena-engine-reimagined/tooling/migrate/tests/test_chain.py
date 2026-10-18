import unittest
from helpers import *  # noqa
from rmig.db_items import ItemDb
from rmig.db_misc import JobDb, SizeFixDb, StatPointDb


def item(i, aegis, extra=""):
    return "  - Id: %d\n    AegisName: %s\n    Name: %s\n%s" % (i, aegis, aegis, extra)


class ChainTests(unittest.TestCase):
    def tearDown(self):
        if getattr(self, "t", None):
            self.t.cleanup()

    def test_mode_import_selection(self):
        files = {
            "db/item_db.yml": stub("ITEM_DB", 3, imports=[("db/pre-re/item_db.yml", "Mode: Prerenewal"),
                                                          ("db/re/item_db.yml", "Mode: Renewal"), "db/import/item_db.yml"]),
            "db/pre-re/item_db.yml": stub("ITEM_DB", 3, body=item(1, "Pre_Only")),
            "db/re/item_db.yml": stub("ITEM_DB", 3, body=item(2, "Re_Only")),
        }
        self.t = Tree(files)
        pre = items_app(self.t.root, "prerenewal")
        ren = items_app(self.t.root, "renewal")
        self.assertIn(1, pre.rows)
        self.assertNotIn(2, pre.rows)
        self.assertIn(2, ren.rows)
        self.assertNotIn(1, ren.rows)

    def test_last_file_wins_per_field_and_import_order(self):
        files = {
            "db/item_db.yml": stub("ITEM_DB", 3, imports=["db/pre-re/item_db.yml", "db/import/item_db.yml"]),
            "db/pre-re/item_db.yml": stub("ITEM_DB", 3, body=item(1, "Thing", "    Weight: 10\n    Slots: 2\n"), imports=["db/pre-re/more.yml"]),
            "db/pre-re/more.yml": stub("ITEM_DB", 3, body="  - Id: 1\n    Weight: 20\n"),
            "db/import/item_db.yml": stub("ITEM_DB", 3, body="  - Id: 1\n    Slots: 4\n"),
        }
        self.t = Tree(files)
        a = items_app(self.t.root)
        r = a.rows[1]
        self.assertEqual((r["weight"], r["slots"]), (20, 4))          # nested import runs before the next sibling
        self.assertEqual(a.files, ["db/item_db.yml", "db/pre-re/item_db.yml", "db/pre-re/more.yml", "db/import/item_db.yml"])

    def test_body_of_a_file_is_applied_before_its_own_imports(self):
        files = {
            "db/item_db.yml": stub("ITEM_DB", 3, body=item(1, "Thing", "    Weight: 1\n"), imports=["db/x.yml"]),
            "db/x.yml": stub("ITEM_DB", 3, body="  - Id: 1\n    Weight: 2\n"),
        }
        self.t = Tree(files)
        self.assertEqual(items_app(self.t.root).rows[1]["weight"], 2)

    def test_header_clear(self):
        files = {
            "db/item_db.yml": stub("ITEM_DB", 3, imports=["db/a.yml", "db/import/item_db.yml"]),
            "db/a.yml": stub("ITEM_DB", 3, body=item(1, "Old")),
            "db/import/item_db.yml": stub("ITEM_DB", 3, body=item(2, "New"), clear=True),
        }
        self.t = Tree(files)
        a = items_app(self.t.root)
        self.assertNotIn(1, a.rows)
        self.assertIn(2, a.rows)
        self.assertEqual(a.clears, 1)
        self.assertNotIn("old", a.aegis)                              # the aegis lookup is cleared too

    def test_missing_overlay_is_not_an_error_but_missing_stub_import_is(self):
        files = {"db/item_db.yml": stub("ITEM_DB", 3, imports=["db/import/item_db.yml", "db/nope.yml"])}
        self.t = Tree(files)
        a = items_app(self.t.root)
        self.assertEqual(a.missing, ["db/import/item_db.yml"])
        self.assertEqual([f for f, _ in a.file_errors], ["db/nope.yml"])

    def test_header_type_and_version_checks(self):
        cases = [("ITEM_DB", 99, "Version 99"), ("MOB_DB", 3, "Type"), ("ITEM_DB", 0, "minimum")]
        for typ, ver, what in cases:
            self.t = Tree({"db/item_db.yml": stub(typ, ver, body=item(1, "X"))})
            a = items_app(self.t.root)
            self.assertEqual(len(a.file_errors), 1, what)
            self.assertEqual(set(a.rows), {499})   # only the loader-made dummy item
            self.t.cleanup()
        self.t = None

    def test_outdated_but_supported_version_loads_with_warning(self):
        from rmig.db_misc import StatusDb
        # STATUS_DB: version 4, minimum 3 -> a version 3 file loads with a warning
        self.t = Tree({"db/status.yml": stub("STATUS_DB", 3, body="  - Status: Poison\n")})
        w = world(self.t.root)
        from rmig.db_skills import SkillDb
        sk = SkillDb(w, items_app(self.t.root))
        app = StatusDb(w, sk)
        load_chain(app, app.STUB)
        self.assertEqual(app.file_errors, [])
        self.assertIn(4, app.rows)
        self.assertTrue(any("outdated" in m for _, m in app.r.warnings))

    def test_syntax_error_and_missing_header_are_file_errors(self):
        self.t = Tree({"db/item_db.yml": "Header: [\n"})
        a = items_app(self.t.root)
        self.assertIn("syntax", a.file_errors[0][1])
        self.t.cleanup()
        self.t = Tree({"db/item_db.yml": "Body:\n  - Id: 1\n"})
        a = items_app(self.t.root)
        self.assertIn("Header", a.file_errors[0][1])

    def test_generator_imports_are_skipped_unless_enabled(self):
        files = {
            "db/item_db.yml": stub("ITEM_DB", 3, imports=[("db/gen.yml", "Generator: true")]),
            "db/gen.yml": stub("ITEM_DB", 3, body=item(7, "Gen")),
        }
        self.t = Tree(files)
        self.assertEqual(items_app(self.t.root).rows.keys() - {499}, set())
        from rmig.db_items import ItemDb
        a = load_app(ItemDb, self.t.root, world_kw={"generator": True})
        self.assertIn(7, a.rows)

    def test_job_aspd_special_rule(self):
        # renewal build without RENEWAL_ASPD loads the Prerenewal-mode job_aspd import
        files = {
            "db/job_stats.yml": stub("JOB_STATS", 4, imports=[("db/pre-re/job_aspd.yml", "Mode: Prerenewal"),
                                                            ("db/re/job_aspd.yml", "Mode: Renewal")]),
            "db/pre-re/job_aspd.yml": stub("JOB_STATS", 4, body="  - Jobs:\n      Novice: true\n    BaseASPD:\n      Fist: 500\n"),
            "db/re/job_aspd.yml": stub("JOB_STATS", 4, body="  - Jobs:\n      Novice: true\n    BaseASPD:\n      Fist: 150\n"),
        }
        self.t = Tree(files)
        novice = lambda a: a.rows[consts.load("renewal").get("JOB_NOVICE")]["aspd_base"][0]
        a = load_app(JobDb, self.t.root, "renewal")
        self.assertEqual(novice(a), 150)
        a = load_app(JobDb, self.t.root, "renewal", world_kw={"renewal_aspd": False})
        self.assertEqual(novice(a), 500)
        a = load_app(JobDb, self.t.root, "prerenewal")
        self.assertEqual(a.rows[consts.load("prerenewal").get("JOB_NOVICE")]["aspd_base"][0], 500)

    def test_import_dir_override(self):
        files = {
            "db/item_db.yml": stub("ITEM_DB", 3, body=item(1, "T", "    Weight: 1\n"), imports=["db/import/item_db.yml"]),
            "overlay/item_db.yml": stub("ITEM_DB", 3, body="  - Id: 1\n    Weight: 9\n"),
        }
        self.t = Tree(files)
        a = load_app(ItemDb, self.t.root, world_kw={"import_dir": "overlay"})
        self.assertEqual(a.rows[1]["weight"], 9)


if __name__ == "__main__":
    unittest.main()
