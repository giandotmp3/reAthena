#!/usr/bin/env python3
"""Runs the package tests.

  python3 -I tools/run_tests.py                  # plain Lua through lupa (pip install lupa): the original runner
  python3 -I tools/run_tests.py --luau [PATH]    # the Luau CLI (PATH, $LUAU, or `luau` on PATH): bundles every module into
                                                 # build/bundle_tests.luau first (the CLI sandboxes require()d modules, so the
                                                 # `import` global the sources use cannot reach them otherwise)
  python3 -I tools/run_tests.py --luau [PATH] --report     # golden match rate by category (tests/golden_report)

Both runners execute the same tests/main.luau. The golden data (conformance/golden/*.jsonl.gz, recorded from the real legacy
code) is expanded into build/golden_weapon_pre.luau first (tools/build_golden.py).
"""
import os
import pathlib
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
TOOLS = ROOT / "tools"

BOOT = r"""
local root = ...
local cache = {}
function import(name)
  if cache[name] == nil then
    local f = assert(io.open(root .. "/" .. name .. ".luau", "r"))
    local src = f:read("a"); f:close()
    local chunk = assert(load(src, "@" .. name, "t", _G))
    cache[name] = chunk()
  end
  return cache[name]
end
return import("tests/main")
"""


def build_golden():
    subprocess.run([sys.executable, "-I", str(TOOLS / "build_golden.py")], check=True)


def run_luau(args):
    i = args.index("--luau")
    path = args[i + 1] if i + 1 < len(args) and not args[i + 1].startswith("--") else None
    exe = path or os.environ.get("LUAU") or shutil.which("luau")
    if not exe:
        sys.exit("run_tests: Luau CLI not found (pass --luau PATH or set $LUAU)")
    entry = "tests/golden_report" if "--report" in args else "tests/main"
    out = ROOT / "build" / "bundle_tests.luau"
    subprocess.run([sys.executable, "-I", str(TOOLS / "bundle_luau.py"), entry, str(out)], check=True, stdout=subprocess.DEVNULL)
    rc = subprocess.run([exe, str(out)], cwd=ROOT).returncode
    if entry == "tests/main":  # the extra suite (tests/extra.luau) runs after the main one, in its own process
        out2 = ROOT / "build" / "bundle_extra.luau"
        subprocess.run([sys.executable, "-I", str(TOOLS / "bundle_luau.py"), "tests/extra", str(out2)], check=True, stdout=subprocess.DEVNULL)
        rc = rc or subprocess.run([exe, str(out2)], cwd=ROOT).returncode
    return rc


def run_lupa():
    from lupa import LuaRuntime
    code = int(LuaRuntime(unpack_returned_tuples=True).execute(BOOT, str(ROOT)) or 0)
    extra = int(LuaRuntime(unpack_returned_tuples=True).execute(BOOT.replace("tests/main", "tests/extra"), str(ROOT)) or 0)
    return code or extra


def main() -> int:
    if subprocess.run([sys.executable, "-I", str(TOOLS / "lint_numeric.py")]).returncode != 0:
        return 1  # `/` or `//` in ruleset code: Luau floors, legacy truncates (M0 numerics note)
    build_golden()
    if "--luau" in sys.argv:
        return run_luau(sys.argv)
    return run_lupa()


if __name__ == "__main__":
    sys.exit(main())
