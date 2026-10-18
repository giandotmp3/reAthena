#!/usr/bin/env python3
"""Tests for the legacy packet-spec tooling (run by ctest as `legacy_tools`).

  python3 -I tests/test_legacy_tools.py

Covers: the subset C preprocessor, determinism of the extraction, agreement with a real
compiler (g++ -E / compiled oracle, skipped only when g++ is absent), and negative tests:
a spec that differs from the legacy headers by one number must make --verify-legacy fail.
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
NP = HERE.parent
sys.path.insert(0, str(NP / "tools/legacy"))
sys.path.insert(0, str(NP / "tools/codegen"))

import cpp_subset  # noqa: E402
import extract_packets as ep  # noqa: E402

CODEGEN = NP / "tools/codegen/netproto_codegen.py"


def legacy_root():
    for c in [os.environ.get("RATHENA_LEGACY_ROOT")] + list(NP.parents):
        if c and (Path(c) / "src/map/clif_packetdb.hpp").is_file():
            return Path(c)
    return None


ROOT = legacy_root()
HAVE_GXX = shutil.which("g++") is not None


def run_codegen(proto, *extra):
    return subprocess.run([sys.executable, "-I", str(CODEGEN), "--proto", str(proto), "--verify-legacy",
                           "--legacy-root", str(ROOT), *extra], capture_output=True, text=True)


class SubsetPreprocessor(unittest.TestCase):
    def pp(self, text, **defs):
        d = Path(tempfile.mkdtemp())
        f = d / "t.hpp"
        f.write_text(text)
        p = cpp_subset.Preprocessor([d], strict_paths=[f])
        for k, v in defs.items():
            p.define(k, str(v))
        out = []
        p.process(f, lambda path, n, t: out.append(t))
        shutil.rmtree(d)
        return out, p

    def test_if_elif_else_chain_takes_exactly_one_branch(self):
        t = "#if V < 5\na\n#elif V < 10\nb\n#elif V < 20\nc\n#else\nd\n#endif\n"
        for v, want in [(1, "a"), (7, "b"), (10, "c"), (19, "c"), (20, "d")]:
            self.assertEqual(self.pp(t, V=v)[0], [want])

    def test_dead_branches_are_not_evaluated_and_nest(self):
        t = "#if 0\n#error never\n#if 1\nx\n#endif\n#else\ny\n#endif\n"
        self.assertEqual(self.pp(t)[0], ["y"])

    def test_defined_and_flavour_macros(self):
        t = ("#if PACKETVER_MAIN_NUM >= 20220330 || PACKETVER_RE_NUM >= 20211103 || defined(PACKETVER_ZERO)\nnew\n"
             "#else\nold\n#endif\n")
        self.assertEqual(self.pp(t, PACKETVER_RE_NUM=20211103)[0], ["new"])
        self.assertEqual(self.pp(t, PACKETVER_MAIN_NUM=20211103)[0], ["old"])
        self.assertEqual(self.pp(t, PACKETVER_ZERO="")[0], ["new"])
        # an undefined flavour macro is 0 (as in C), and is recorded
        out, p = self.pp(t)
        self.assertEqual(out, ["old"])
        self.assertTrue({"PACKETVER_MAIN_NUM", "PACKETVER_RE_NUM"} <= p.unknown_ids)

    def test_expression_operators_and_c_division(self):
        e = cpp_subset.Preprocessor([]).eval_expr
        self.assertEqual(e("1 + 2 * 3 - (4 - 1)"), 4)
        self.assertEqual(e("!0 && !(1 == 2) && 3 >= 3 && 2 <= 2 && 5 != 6"), 1)
        self.assertEqual(e("0x10 | 1 << 2"), 20)
        self.assertEqual(e("-7 / 2"), -3)      # truncates toward zero like C
        self.assertEqual(e("-7 % 2"), -1)
        self.assertEqual(e("1 ? 20211103 : 0"), 20211103)
        self.assertEqual(e("20211103U >= 20211103L"), 1)

    def test_object_macros_expand_recursively_and_undef(self):
        out, _ = self.pp("#define A B\n#define B 3\n#if A == 3\nyes\n#endif\n#undef B\n#if A == 3\nno\n#endif\n")
        self.assertEqual(out, ["yes"])

    def test_unbalanced_and_bad_expressions_are_errors_not_guesses(self):
        with self.assertRaises(cpp_subset.CppError):
            self.pp("#if 1\nx\n")
        with self.assertRaises(cpp_subset.CppError):
            self.pp("#if 1 +\nx\n#endif\n")

    def test_error_directive_only_fires_when_live(self):
        with self.assertRaises(cpp_subset.CppError):
            self.pp("#if 1\n#error old client\n#endif\n")


@unittest.skipIf(ROOT is None, "legacy tree not found")
class Extraction(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.spec = ep.build_spec(ROOT, 20211103, "auto")

    def test_default_build_is_the_re_flavour(self):
        # src/config/packets.hpp: PACKETVER 20211103 lies in the RE range, so the stock build is RE
        self.assertEqual(self.spec["flavour"], "re")
        self.assertFalse(self.spec["hypothetical"])
        self.assertIn("PACKETVER_RE", self.spec["defined"])
        self.assertEqual(self.spec["macros"].get("PACKETVER_RE_NUM"), 20211103)

    def test_extraction_is_deterministic(self):
        again = ep.render(ep.build_spec(ROOT, 20211103, "auto"))
        self.assertEqual(ep.render(self.spec), again)

    def test_checked_in_legacy_spec_equals_fresh_evaluation(self):
        self.assertEqual((NP / "proto/legacy_20211103.yaml").read_text(encoding="utf-8"), ep.render(self.spec))

    def test_shuffle_block_overrides_the_base_table(self):
        e = {x["opcode"]: x for x in self.spec["entries"]}
        self.assertEqual((e[0x0436]["len"], e[0x0436]["handler"]), (23, "clif_parse_WantToConnection"))
        self.assertEqual(e[0x0436]["src"].split(":")[0], "map/clif_shuffle.hpp")
        # the base DB's old opcode was overwritten by a later layer
        self.assertEqual(e[0x0072]["handler"], "clif_parse_UseSkillToId")
        self.assertTrue(any(s["handler"] == "clif_parse_WantToConnection" for s in e[0x0072]["shadows"]))

    def test_main_flavour_differs_only_where_the_conditions_say(self):
        main = ep.build_spec(ROOT, 20211103, "main")
        self.assertTrue(main["hypothetical"])
        a = {x["opcode"]: x for x in self.spec["entries"]}
        b = {x["opcode"]: x for x in main["entries"]}
        diff = sorted(o for o in set(a) | set(b) if (a.get(o) or {}).get("len") != (b.get(o) or {}).get("len")
                      or (a.get(o) or {}).get("pos") != (b.get(o) or {}).get("pos"))
        self.assertEqual(diff, [0x0436, 0x0b93, 0x0b97, 0x0b98, 0x0bad])
        self.assertEqual((b[0x0436]["len"], b[0x0436]["pos"]), (19, [2, 6, 10, 14, 18]))

    def test_typed_struct_fields_and_unknown_marking(self):
        e = {x["opcode"]: x for x in self.spec["entries"]}
        self.assertEqual(e[0x0090]["fields"][1], {"name": "AID", "offset": 2, "type": "u32"})
        self.assertEqual(e[0x035f]["fields"], "unknown")
        self.assertEqual(e[0x035f]["pos"], [2])

    def test_struct_sizes_equal_declared_lengths_except_reported_mismatches(self):
        # nothing is silently reconciled: disagreements between packet_db and the struct are listed
        for c, ln, sname, sz in self.spec["mismatches"]:
            self.assertNotEqual(ln, sz)
        self.assertEqual(sorted(m[2] for m in self.spec["mismatches"]), ["PACKET_ZC_FEED_MER", "PACKET_ZC_FEED_PET"])


@unittest.skipIf(ROOT is None, "legacy tree not found")
class VerifyLegacy(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        shutil.copytree(NP / "proto", self.tmp / "proto")

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def edit(self, name, old, new):
        f = self.tmp / "proto" / name
        t = f.read_text()
        self.assertEqual(t.count(old), 1, f"{old!r} must occur exactly once in {name}")
        f.write_text(t.replace(old, new))

    def test_checked_in_spec_verifies(self):
        r = run_codegen(self.tmp / "proto/client_packets.yaml")
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("verify-legacy OK", r.stdout)

    def test_a_changed_legacy_table_entry_fails(self):
        self.edit("legacy_20211103.yaml", "  - opcode: 0x035f\n    len: 5\n", "  - opcode: 0x035f\n    len: 6\n")
        r = run_codegen(self.tmp / "proto/client_packets.yaml")
        self.assertEqual(r.returncode, 1)
        self.assertIn("differs from the legacy evaluation", r.stderr)

    def test_a_wrong_opcode_in_the_semantic_spec_fails(self):
        self.edit("client_packets.yaml", "opcode: 0x035f", "opcode: 0x0085")
        r = run_codegen(self.tmp / "proto/client_packets.yaml")
        self.assertEqual(r.returncode, 1)
        self.assertIn("CZ_REQUEST_MOVE", r.stderr)

    def test_a_wrong_length_fails_on_the_flavour_it_applies_to(self):
        self.edit("client_packets.yaml", "length: { fixed: 23 }", "length: { fixed: 19 }")
        r = run_codegen(self.tmp / "proto/client_packets.yaml")
        self.assertEqual(r.returncode, 1)
        self.assertIn("re/20211103", r.stderr)
        self.assertIn("legacy has 23", r.stderr)

    def test_main_target_is_checked_separately(self):
        self.edit("client_packets.yaml", "length: { fixed: 19 }", "length: { fixed: 23 }")
        r = run_codegen(self.tmp / "proto/client_packets.yaml")
        self.assertEqual(r.returncode, 1)
        self.assertIn("main/20211103", r.stderr)

    def test_changed_handler_offsets_and_aliases_fail(self):
        self.edit("client_packets.yaml", "handler_offsets: [2, 6, 10, 14, 22]", "handler_offsets: [2, 6, 10, 14, 18]")
        r = run_codegen(self.tmp / "proto/client_packets.yaml")
        self.assertEqual(r.returncode, 1)
        self.assertIn("handler_offsets", r.stderr)

    def test_a_guessed_field_on_a_handler_parsed_packet_fails(self):
        self.edit("client_packets.yaml",
                  "        opcode: 0x0360\n        length: { fixed: 6 }\n        handler_offsets: [2]\n        fields: unknown\n",
                  "        opcode: 0x0360\n        length: { fixed: 6 }\n        handler_offsets: [2]\n"
                  "        fields:\n          - { name: tick, offset: 2, type: u32 }\n")
        r = run_codegen(self.tmp / "proto/client_packets.yaml")
        self.assertEqual(r.returncode, 1)
        self.assertIn("CZ_REQUEST_TIME", r.stderr)

    def test_a_changed_struct_field_fails(self):
        self.edit("client_packets.yaml", "{ name: AID, offset: 2, type: u32 }", "{ name: AID, offset: 2, type: u16 }")
        r = run_codegen(self.tmp / "proto/client_packets.yaml")
        self.assertEqual(r.returncode, 1)
        self.assertIn("CZ_CONTACTNPC", r.stderr)

    def test_an_entry_for_an_unverified_target_fails(self):
        # ZC_NOTIFY_TIME re-pointed at a date nobody has evaluated: it applies to no verified target
        self.edit("client_packets.yaml",
                  '"RE>=20211103 && RE<20211104 || MAIN>=20211103 && MAIN<20211104"\n        opcode: 0x007f',
                  '"RE>=20211104"\n        opcode: 0x007f')
        r = run_codegen(self.tmp / "proto/client_packets.yaml")
        self.assertEqual(r.returncode, 1)
        self.assertIn("unverified entry", r.stderr)

    def test_a_missing_legacy_tree_is_an_error_not_a_pass(self):
        r = subprocess.run([sys.executable, "-I", str(CODEGEN), "--proto", str(self.tmp / "proto/client_packets.yaml"),
                            "--verify-legacy", "--legacy-root", str(self.tmp / "nowhere")],
                           capture_output=True, text=True, env={"PATH": os.environ.get("PATH", "")})
        # the tmp copy sits outside the repo, so nothing above it contains src/
        self.assertEqual(r.returncode, 1)
        self.assertIn("cannot find the legacy tree", r.stderr)


@unittest.skipIf(ROOT is None or not HAVE_GXX, "legacy tree or g++ not available")
class CompilerCrossCheck(unittest.TestCase):
    def run_check(self, *args):
        import crosscheck_gxx
        tmp = tempfile.mkdtemp()
        try:
            rep = Path(tmp) / "r.json"
            rc = crosscheck_gxx.main(["--legacy-root", str(ROOT), "--work-dir", tmp, "--report", str(rep), *args])
            return rc, json.loads(rep.read_text())
        finally:
            shutil.rmtree(tmp)

    def test_default_build_agrees_with_gxx(self):
        rc, rep = self.run_check()
        self.assertEqual(rep["disagreements"], [])
        self.assertEqual(rc, 0)
        self.assertEqual(rep["live_definitions_python"], rep["live_definitions_compiler"])
        self.assertEqual(rep["live_definitions_python"], rep["gxx_E_recording_calls"])
        self.assertEqual(rep["final_opcodes_python"], rep["final_opcodes_compiler"])
        self.assertGreater(rep["structs_compared"], 500)
        self.assertGreater(rep["offsets_compared"], 1900)

    def test_forced_main_flavour_agrees_with_gxx_on_a_patched_copy(self):
        rc, rep = self.run_check("--flavour", "main")
        self.assertEqual((rc, rep["disagreements"]), (0, []))
        self.assertTrue(rep["hypothetical"])

    def test_other_versions_agree_too(self):
        for ver in ("20130515", "20180704", "20220330"):
            rc, rep = self.run_check("--packetver", ver)
            self.assertEqual((rc, rep["disagreements"]), (0, []), ver)

    def test_the_oracle_catches_a_broken_evaluator(self):
        # sabotage: treat `>=` as `>` inside the subset preprocessor; the compiler must disagree
        orig = cpp_subset._Eval.apply
        cpp_subset._Eval.apply = staticmethod(lambda op, a, b: orig(">" if op == ">=" else op, a, b))
        try:
            rc, rep = self.run_check()
        except Exception:
            rc, rep = 1, {"disagreements": ["raised"]}  # a raising evaluator is also a detection
        finally:
            cpp_subset._Eval.apply = staticmethod(orig)
        self.assertEqual(rc, 1)
        self.assertTrue(rep["disagreements"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
