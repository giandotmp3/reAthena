#!/usr/bin/env python3
"""Tests for the full-catalog tooling (run by ctest as `catalog_tools`).

  python3 -I tests/test_catalog_tools.py

Covers: the handler-layout derivation rules (what is mechanical and what must stay `unknown`), the derived
catalog (determinism, counts, invariants, agreement with the legacy tables), and negative tests: a catalog
with one wrong number, or without a curated packet, must make `--verify-catalog` fail.
"""
import collections
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

import yaml

HERE = Path(__file__).resolve().parent
NP = HERE.parent
sys.path.insert(0, str(NP / "tools/legacy"))
sys.path.insert(0, str(NP / "tools/codegen"))

import derive_catalog as dc  # noqa: E402
import handler_layout as hl  # noqa: E402

CODEGEN = NP / "tools/codegen/netproto_codegen.py"
CATALOG = NP / "proto/packets_re_20211103.yaml"
OVERLAY = NP / "proto/client_packets.yaml"
LEGACY_YAML = NP / "proto/legacy_20211103.yaml"


def legacy_root():
    for c in [os.environ.get("RATHENA_LEGACY_ROOT")] + list(NP.parents):
        if c and (Path(c) / "src/map/clif_packetdb.hpp").is_file():
            return Path(c)
    return None


ROOT = legacy_root()


def src_from(text):
    d = Path(tempfile.mkdtemp())
    f = d / "t.cpp"
    f.write_text(text)
    s = hl.Source(f)
    shutil.rmtree(d)
    return s


def derive(body, pos=(), db_len=-1, name="clif_parse_T", extra=""):
    s = src_from(extra + f"\nvoid {name}(int32 fd, map_session_data* sd)\n{{\n{body}\n}}\n")
    return hl.derive(s, name, list(pos), db_len)


class HandlerLayout(unittest.TestCase):
    def test_straight_literal_reads_become_fields_with_the_assigned_names(self):
        lay, why = derive("int a = RFIFOL(fd, 2); sd->b = RFIFOW(fd, 6); uint8 c; c = RFIFOB(fd, 8);", db_len=9)
        self.assertEqual(why, "ok")
        self.assertEqual([(f["name"], f["offset"], f["type"]) for f in lay["fields"]],
                         [("a", 2, "u32"), ("b", 6, "u16"), ("c", 8, "u8")])

    def test_pos_indices_are_resolved_with_the_pos_list_of_the_opcode(self):
        lay, _ = derive("x = RFIFOL(fd, info->pos[1]); y = RFIFOW(fd, packet_db[RFIFOW(fd,0)].pos[0]);", pos=(4, 12), db_len=16)
        self.assertEqual([(f["offset"], f["type"]) for f in lay["fields"]], [(4, "u16"), (12, "u32")])
        lay2, _ = derive("x = RFIFOL(fd, info->pos[1]);", pos=(4, 20), db_len=14)
        self.assertIsNone(lay2)   # beyond the fixed length

    def test_a_pos_index_past_the_list_is_not_guessed(self):
        lay, why = derive("x = RFIFOL(fd, info->pos[3]);", pos=(4,), db_len=10)
        self.assertIsNone(lay)
        self.assertIn("non-literal", why)

    def test_opcode_and_length_word_reads_are_not_fields(self):
        lay, _ = derive("int len = RFIFOW(fd, 2); int cmd = RFIFOW(fd, 0); y = RFIFOL(fd, 4);", db_len=-1)
        self.assertEqual([(f["offset"], f["type"]) for f in lay["fields"]], [(4, "u32")])

    def test_positions_become_pos3_pos6(self):
        lay, _ = derive("RFIFOPOS(fd, 2, &x, &y, &dir); RFIFOPOS2(fd, 5, &a, &b, &c, &d, &e, &f);", db_len=11)
        self.assertEqual([(f["name"], f["offset"], f["type"]) for f in lay["fields"]], [("pos", 2, "pos3"), ("f5", 5, "pos6")])

    def test_variable_packet_with_one_pointer_read_gets_a_tail(self):
        lay, _ = derive("a = RFIFOL(fd, 4); char* t = RFIFOCP(fd, 8);", db_len=-1)
        self.assertEqual(lay["tail"], {"offset": 8})
        lay2, why = derive("t = RFIFOCP(fd, 4); u = RFIFOCP(fd, 28);", db_len=-1)
        self.assertIsNone(lay2)                       # two pointers: widths unknown
        lay3, why3 = derive("t = RFIFOCP(fd, 4);", db_len=40)
        self.assertIsNone(lay3)                       # fixed packet: the width of the string is not in the handler
        lay4, why4 = derive("a = RFIFOL(fd, 4); t = RFIFOCP(fd, 6);", db_len=-1)
        self.assertIsNone(lay4)                       # pointer inside another read

    def test_frame_start_dump_is_not_a_tail(self):
        lay, _ = derive("ShowDump(RFIFOP(fd, 0), RFIFOREST(fd)); a = RFIFOL(fd, 2);", db_len=6)
        self.assertIsNone(lay["tail"])

    def test_loops_are_not_unrolled(self):
        for body in ("for (i = 0; i < n; ++i) x = RFIFOW(fd, 4);", "while (n--) { x = RFIFOW(fd, 4); }",
                     "for (i = 0; i < n; ++i) { for (j = 0; j < 2; ++j) { y = RFIFOL(fd, 6 + i); } }"):
            lay, why = derive(body, db_len=-1)
            self.assertIsNone(lay, body)

    def test_computed_offsets_stay_unknown(self):
        lay, why = derive("n = RFIFOW(fd, 2); x = RFIFOL(fd, 4 + n * 2);", db_len=-1)
        self.assertIsNone(lay)
        self.assertIn("non-literal", why)

    def test_conflicting_or_overlapping_widths_stay_unknown(self):
        lay, why = derive("if (a) x = RFIFOW(fd, 4); else x = RFIFOL(fd, 4);", db_len=8)
        self.assertIsNone(lay)
        lay, why = derive("x = RFIFOL(fd, 4); y = RFIFOW(fd, 6);", db_len=8)
        self.assertIsNone(lay)

    def test_reads_under_preprocessor_conditionals_are_not_mechanical(self):
        lay, why = derive("a = RFIFOL(fd, 2);\n#if PACKETVER >= 20200101\nb = RFIFOL(fd, 6);\n#endif", db_len=10)
        self.assertIsNone(lay)
        self.assertIn("preprocessor", why)

    def test_helpers_that_read_the_packet_are_followed_when_they_are_mechanical(self):
        extra = "static int h(int32 fd) { return RFIFOL(fd, 2); }\n"
        lay, _ = derive("a = h(fd); b = RFIFOB(fd, 6);", db_len=7, extra=extra)
        self.assertEqual([(f["offset"], f["type"]) for f in lay["fields"]], [(2, "u32"), (6, "u8")])
        extra2 = "static int h(int32 fd) { for (;;) { x = RFIFOL(fd, 2); } }\n"
        lay2, why = derive("a = h(fd);", db_len=7, extra=extra2)
        self.assertIsNone(lay2)
        self.assertIn("helper", why)

    def test_reads_through_another_fd_are_refused(self):
        lay, why = derive("a = RFIFOL(sd->fd, 2);", db_len=6)
        self.assertIsNone(lay)

    def test_comments_do_not_count(self):
        lay, _ = derive("// x = RFIFOL(fd, 99);\n/* y = RFIFOL(fd, 98); */ a = RFIFOL(fd, 2);", db_len=6)
        self.assertEqual([f["offset"] for f in lay["fields"]], [2])

    def test_names_are_unique_and_fall_back_to_the_offset(self):
        lay, _ = derive("a = RFIFOL(fd, 2); a = RFIFOL(fd, 6); foo(RFIFOW(fd, 10));", db_len=12)
        self.assertEqual([f["name"] for f in lay["fields"]], ["a", "f6", "f10"])


@unittest.skipIf(ROOT is None, "legacy tree not found")
class DerivedCatalog(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cat = dc.build(ROOT, 20211103, OVERLAY)
        cls.doc = yaml.safe_load(CATALOG.read_text())
        cls.legacy = yaml.safe_load(LEGACY_YAML.read_text())

    def test_checked_in_catalog_is_the_fresh_derivation_and_it_is_deterministic(self):
        text = dc.render(self.cat)
        self.assertEqual(text, CATALOG.read_text())
        self.assertEqual(text, dc.render(dc.build(ROOT, 20211103, OVERLAY)))

    def test_counts_in_the_header_match_the_packets(self):
        by = collections.Counter(p["versions"][0]["source"] for p in self.doc["packets"])
        c = self.doc["counts"]
        self.assertEqual(c["packets"], len(self.doc["packets"]))
        self.assertEqual(c["typed_struct"], by["typed-struct"])
        self.assertEqual(c["common_struct"], by["common-struct"])
        self.assertEqual(c["handler_derived"], by["handler-derived"])
        self.assertEqual(c["handler_unknown"], by["handler"])
        self.assertEqual(c["length_only"], by["length-only"])
        self.assertEqual(c["typed_total"], by["typed-struct"] + by["common-struct"])

    def test_every_opcode_of_the_packet_db_with_a_typed_struct_or_handler_is_in_the_catalog(self):
        have = {(p["versions"][0]["opcode"]) for p in self.doc["packets"]}
        for e in self.legacy["packets"]:
            if isinstance(e["fields"], list) or "handler" in e or e["fields"] == "unknown":
                # aliases of a handler are deliberately not accepted; everything else must be present
                if e["opcode"] in have:
                    continue
                alias = any(e["opcode"] in (p["versions"][0].get("also_accepted_by_legacy") or [])
                            for p in self.doc["packets"])
                self.assertTrue(alias or e["fields"] == "unresolved", f"0x{e['opcode']:04x} missing")

    def test_typed_packets_keep_the_offsets_and_types_of_the_legacy_structs(self):
        by_op = {p["versions"][0]["opcode"]: p for p in self.doc["packets"]}
        checked = 0
        for e in self.legacy["packets"] + self.legacy["struct_only_packets"]:
            f = e["fields"]
            if not isinstance(f, list) or any("struct" in x or x.get("type") == "bitfield" for x in f) or "repeat" in e:
                continue
            p = by_op.get(e["opcode"])
            if p is None or p["versions"][0]["source"] != "typed-struct" or p.get("struct") != e["struct"]:
                continue
            mine = {(x["offset"]): x for x in p["versions"][0]["fields"] if not x.get("row")}
            for x in f:
                if x["offset"] == 0 and x["type"] in ("i16", "u16"):
                    continue
                if x["offset"] == 2 and x["type"] in ("i16", "u16") and "variable" in p["versions"][0]["length"]:
                    continue
                m = mine.get(x["offset"])
                self.assertIsNotNone(m, f"{e['name']}.{x['name']}")
                self.assertEqual(m["name"], x["name"])
                if m["type"] in ("pos3", "pos6"):
                    self.assertEqual(x.get("array_len"), 3 if m["type"] == "pos3" else 6)
                else:
                    self.assertEqual(m["type"], x["type"])
                    self.assertEqual(m.get("width"), x.get("width"))
                    self.assertEqual(m.get("array_len"), x.get("array_len"))
                checked += 1
        self.assertGreater(checked, 800)

    def test_positions_are_tagged_only_where_clif_cpp_packs_them(self):
        pos = [(p["name"], f["name"], f["type"]) for p in self.doc["packets"] if p["versions"][0]["source"] != "handler"
               for f in (p["versions"][0]["fields"] if p["versions"][0]["fields"] != "unknown" else []) if f["type"] in ("pos3", "pos6")]
        self.assertGreaterEqual(len(pos), 7)
        names = {(n, t) for _, n, t in pos}
        self.assertTrue({("PosDir", "pos3"), ("MoveData", "pos6")} <= names)
        self.assertIn(("CZ_REQUEST_MOVE", "pos", "pos3"), pos)

    def test_overlay_names_win_for_the_curated_opcodes(self):
        by = {p["name"]: p["versions"][0]["opcode"] for p in self.doc["packets"]}
        for n, op in {"CZ_ENTER": 0x436, "CZ_REQUEST_MOVE": 0x35f, "CZ_REQUEST_TIME": 0x360, "CZ_REQUEST_CHAT": 0xf3,
                      "CZ_NOTIFY_ACTORINIT": 0x7d, "CZ_CONTACTNPC": 0x90, "CA_LOGIN": 0x64, "ZC_NOTIFY_PLAYERMOVE": 0x87,
                      "ZC_NOTIFY_TIME": 0x7f}.items():
            self.assertEqual(by[n], op)

    def test_handler_derived_layouts_cite_their_source_line(self):
        text = CATALOG.read_text()
        self.assertIn("# clif.cpp:11207 clif_parse_TickSend", text)
        self.assertIn("# char/char_clif.cpp:", text)

    def test_no_obfuscation_key_values_in_the_catalog(self):
        text = CATALOG.read_text()
        self.assertIn("keys_env: NETPROTO_KEYS_RE_20211103", text)
        self.assertNotRegex(text, r"keys\s*:\s*\[?\s*(0x)?[0-9a-fA-F]{6,}")


@unittest.skipIf(ROOT is None, "legacy tree not found")
class VerifyCatalogNegative(unittest.TestCase):
    def run_verify(self, mutate):
        d = Path(tempfile.mkdtemp())
        try:
            shutil.copy(OVERLAY, d / "client_packets.yaml")
            (d / "packets_re_20211103.yaml").write_text(mutate(CATALOG.read_text()))
            return subprocess.run([sys.executable, "-I", str(CODEGEN), "--proto", str(d / "packets_re_20211103.yaml"),
                                   "--verify-catalog", "--legacy-root", str(ROOT)], capture_output=True, text=True)
        finally:
            shutil.rmtree(d)

    def test_unmodified_catalog_passes(self):
        r = self.run_verify(lambda t: t)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("verify-catalog OK", r.stdout)

    def test_one_wrong_offset_fails(self):
        r = self.run_verify(lambda t: t.replace('{ name: "client_tick", offset: 14, type: "u32" }', '{ name: "client_tick", offset: 15, type: "u32" }', 1))
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("differs from the fresh derivation", r.stderr)

    def test_a_hand_edited_source_label_fails(self):
        r = self.run_verify(lambda t: t.replace("source: handler-derived", "source: typed-struct", 1))
        self.assertNotEqual(r.returncode, 0)

    def test_a_missing_curated_packet_fails(self):
        def drop(t):
            i = t.index("  - name: CZ_REQUEST_TIME\n")
            j = t.index("  # clif.cpp:", i) if "  # clif.cpp:" in t[i:i + 3000] else t.index("  - name:", i + 10)
            return t[:i] + t[j:]
        r = self.run_verify(drop)
        self.assertNotEqual(r.returncode, 0)

    def test_two_packets_on_one_opcode_fail(self):
        r = self.run_verify(lambda t: t.replace("opcode: 0x0360", "opcode: 0x035f", 1))
        self.assertNotEqual(r.returncode, 0)


if __name__ == "__main__":
    unittest.main(verbosity=1)
