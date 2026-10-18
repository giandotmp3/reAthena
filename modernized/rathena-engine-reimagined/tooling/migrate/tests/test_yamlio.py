import unittest
from helpers import *  # noqa
from rmig import yamlio
from rmig.yamlio import NULL, Reject, Reader


class YamlIoTests(unittest.TestCase):
    def test_scalars_stay_text(self):
        doc, _ = yamlio.load_text("A: Yes\nB: 0x10\nC: 1e3\nD: on\nE: '~'\nF: ~\nG:\nH: null\nI: \"null\"\n")
        self.assertEqual(doc["A"], "Yes")        # not a YAML 1.1 boolean
        self.assertEqual(doc["B"], "0x10")
        self.assertEqual(doc["C"], "1e3")
        self.assertEqual(doc["D"], "on")
        self.assertEqual(doc["E"], "~")          # quoted -> text
        self.assertIs(doc["F"], NULL)
        self.assertIs(doc["G"], NULL)
        self.assertIs(doc["H"], NULL)
        self.assertEqual(doc["I"], "null")

    def test_typed_accessors_follow_legacy_conversion(self):
        r = Reader()
        node, _ = yamlio.load_text("U: 0x10\nN: -5\nT: True\nF: faLse\nBAD: 1.5\nBIG: 70000\nE:\n")
        self.assertEqual(r.u32(node, "U"), 16)
        self.assertEqual(r.i16(node, "N"), -5)
        self.assertTrue(r.b(node, "T"))
        self.assertFalse(r.b(node, "F"))          # asBool lower-cases
        for name in ("BAD", "E"):
            with self.assertRaises(Reject):
                r.u16(node, name)
        with self.assertRaises(Reject):
            r.u16(node, "BIG")                    # overflow of uint16 is an error, not a wrap
        with self.assertRaises(Reject):
            r.u16(node, "N")                      # negative into unsigned
        with self.assertRaises(Reject):
            r.u16(node, "MISSING")

    def test_bool_rejects_yes(self):
        r = Reader()
        node, _ = yamlio.load_text("A: Yes\n")
        with self.assertRaises(Reject):
            r.b(node, "A")                        # only true/false

    def test_rate_bounds(self):
        r = Reader()
        node, _ = yamlio.load_text("Z: 0\nOK: 10000\nHI: 10001\n")
        self.assertEqual(r.u16_rate(node, "OK"), 10000)
        for k in ("Z", "HI"):
            with self.assertRaises(Reject):
                r.u16_rate(node, k)

    def test_duplicate_keys_first_wins_but_iteration_visits_both(self):
        doc, dups = yamlio.load_text("M:\n  X: true\n  X: false\n")
        self.assertEqual(dups, ["X"])
        r = Reader()
        self.assertEqual(r.items(doc["M"]), [("X", "true"), ("X", "false")])
        self.assertTrue(r.b(doc["M"], "X"))       # node[name] finds the first child

    def test_syntax_error(self):
        with self.assertRaises(yamlio.YamlSyntaxError):
            yamlio.load_text("a: [unclosed\n")

    def test_block_scalar_keeps_text(self):
        doc, _ = yamlio.load_text("S: |\n  bonus bStr,1;\n  bonus bAgi,1;\n")
        self.assertEqual(doc["S"], "bonus bStr,1;\nbonus bAgi,1;\n")


if __name__ == "__main__":
    unittest.main()
