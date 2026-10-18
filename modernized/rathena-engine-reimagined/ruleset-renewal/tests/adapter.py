"""Seam to the Luau script host: runs the package's Luau code under the Luau CLI.

Two entry points:
  suite()          run tools/run_tests.py once (lint, golden expansion, bundle, Luau test suite) and return the parsed results
  call(fn, args)   call scripts/formulas.luau `fn` with plain number/boolean arguments and return its results

The Luau CLI is found through $LUAU, `luau` on PATH, or $RATHENA_LUAU_ROOT/{b,build}/luau. Without it every test that needs Luau is
skipped (never silently passed).
"""
import functools
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent


class HostUnavailable(NotImplementedError):
    pass


def luau_exe():
    cands = [os.environ.get("LUAU"), shutil.which("luau")]
    root = os.environ.get("RATHENA_LUAU_ROOT")
    if root:
        cands += [str(pathlib.Path(root) / "b" / "luau"), str(pathlib.Path(root) / "build" / "luau")]
    for c in cands:
        if c and pathlib.Path(c).is_file():
            return c
    return None


def available():
    return luau_exe() is not None


@functools.lru_cache(maxsize=1)
def suite():
    exe = luau_exe()
    if exe is None:
        raise HostUnavailable("Luau CLI not found (set LUAU or RATHENA_LUAU_ROOT)")
    r = subprocess.run([sys.executable, "-I", str(ROOT / "tools" / "run_tests.py"), "--luau", exe], capture_output=True, text=True, cwd=ROOT)
    results = []
    for line in r.stdout.splitlines():
        if line.startswith("T "):
            _, status, rule, title = line.split(" ", 3)
            results.append({"status": status, "rule": rule, "title": title})
    return {"returncode": r.returncode, "results": results, "stdout": r.stdout, "stderr": r.stderr}


def tagged(rule):
    return [x for x in suite()["results"] if x["rule"] == rule]


def _lua(v):
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, (int, float)):
        return repr(v)
    raise TypeError(v)


@functools.lru_cache(maxsize=1)
def _lib():
    out = ROOT / "build" / "bundle_lib.luau"
    subprocess.run([sys.executable, "-I", str(ROOT / "tools" / "bundle_luau.py"), "scripts/formulas", str(out), "--lib"],
                   check=True, stdout=subprocess.DEVNULL)
    return out.read_text()


def call(fn: str, args: list):
    exe = luau_exe()
    if exe is None:
        raise HostUnavailable("Luau CLI not found (set LUAU or RATHENA_LUAU_ROOT)")
    code = _lib() + (
        "\nlocal F = import('scripts/formulas')\n"
        f"local r = table.pack(F.{fn}({', '.join(_lua(a) for a in args)}))\n"
        "local out = {}\nfor i = 1, r.n do out[i] = tostring(r[i]) end\n"
        "print('R ' .. table.concat(out, ' '))\n")
    with tempfile.NamedTemporaryFile("w", suffix=".luau", delete=False, dir=ROOT / "build") as f:
        f.write(code)
        path = f.name
    try:
        p = subprocess.run([exe, path], capture_output=True, text=True, cwd=ROOT)
    finally:
        os.unlink(path)
    if p.returncode != 0:
        raise RuntimeError(p.stderr.strip().splitlines()[0] if p.stderr.strip() else "luau failed")
    line = [l for l in p.stdout.splitlines() if l.startswith("R ")][-1]
    vals = [float(x) if "." in x else int(x) for x in line[2:].split()]
    return vals[0] if len(vals) == 1 else vals
