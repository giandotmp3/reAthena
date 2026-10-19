#!/usr/bin/env python3
"""Runs the Luau side of the package tests.

  python3 -I tools/run_tests.py --luau [PATH]            # the Luau CLI (PATH, $LUAU, or `luau` on PATH)
  python3 -I tools/run_tests.py --luau [PATH] --report   # golden match rate by category (tests/golden_report)
  python3 -I tools/run_tests.py --luau [PATH] --bench    # replay time per case (tests/bench_weapon)
  python3 -I tools/run_tests.py --luau [PATH] --status-report   # status golden match rate by function / category / status (tests/status_report)

The Luau CLI sandboxes require()d modules, so tools/bundle_luau.py first folds every module into build/bundle_tests.luau
(the sources use a global `import(name)`). The golden data (conformance/golden/weapon_re.jsonl.gz, recorded from the real
renewal legacy code) is expanded into build/golden_weapon_re.luau first (tools/build_golden.py). The numerics lint runs first.
The Python wrapper (python3 -m unittest discover -s tests) calls this file and maps the Luau results to BR-RULESET IDs.
"""
import os
import pathlib
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
TOOLS = ROOT / "tools"


def find_luau(args):
    path = None
    if "--luau" in args:
        i = args.index("--luau")
        if i + 1 < len(args) and not args[i + 1].startswith("--"):
            path = args[i + 1]
    exe = path or os.environ.get("LUAU") or shutil.which("luau")
    if not exe:
        sys.exit("run_tests: Luau CLI not found (pass --luau PATH or set $LUAU)")
    return exe


def main() -> int:
    args = sys.argv[1:]
    if subprocess.run([sys.executable, "-I", str(TOOLS / "lint_numeric.py")]).returncode != 0:
        return 1  # `/` or `//` in ruleset code: Luau floors, legacy truncates (M0 numerics note)
    subprocess.run([sys.executable, "-I", str(TOOLS / "build_golden.py")], check=True)
    exe = find_luau(args)
    entry = "tests/golden_report" if "--report" in args else ("tests/bench_weapon" if "--bench" in args else "tests/main")
    if "--status-report" in args:
        entry = "tests/status_report"
    out = ROOT / "build" / "bundle_tests.luau"
    subprocess.run([sys.executable, "-I", str(TOOLS / "bundle_luau.py"), entry, str(out)], check=True, stdout=subprocess.DEVNULL)
    rc = subprocess.run([exe, str(out)], cwd=ROOT).returncode
    if entry == "tests/main":  # the extra suite (tests/extra.luau) runs after the main one, in its own process
        out2 = ROOT / "build" / "bundle_extra.luau"
        subprocess.run([sys.executable, "-I", str(TOOLS / "bundle_luau.py"), "tests/extra", str(out2)], check=True, stdout=subprocess.DEVNULL)
        rc = rc or subprocess.run([exe, str(out2)], cwd=ROOT).returncode
    return rc


if __name__ == "__main__":
    sys.exit(main())
