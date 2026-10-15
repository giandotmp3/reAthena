#!/usr/bin/env python3
"""Checks that map-server code outside the script module reaches the script VM only through
ScriptHost (src/map/script_host.hpp; REIMAGINED_ARCHITECTURE.md §3.2).

The script module is every src/map file whose owner is `script` in
modernized/rathena-reimagined/map-rules/manifest/map_modules.tsv (script.cpp, npc.cpp, mapreg.cpp,
...). Any other src/map/*.cpp that calls one of the VM entry points below fails the check, unless
the line carries a `ScriptHost-exempt: <reason>` comment.

    python3 tools/ci/check_script_host.py        # exit 1 and list the calls on failure
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
MANIFEST = ROOT / "modernized/rathena-reimagined/map-rules/manifest/map_modules.tsv"

# The VM entry points ScriptHost wraps (see the comment on each ScriptHost method).
ENTRY_POINTS = [
    "run_script", "npc_event", "npc_script_event", "npc_event_do", "npc_event_do_id",
    "npc_event_doall", "npc_event_doall_id", "npc_event_runall", "npc_event_doall_path",
    "npc_event_do_oninit", "npc_scriptcont", "npc_click", "npc_event_dequeue",
    "script_run_autobonus", "script_run_petautobonus", "set_var_str",
    "mapreg_setreg", "mapreg_readreg", "mapreg_setregstr", "mapreg_readregstr",
    "script_reload", "npc_reload", "npc_addsrcfile", "npc_unloadfile", "npc_unload_duplicates",
]
CALL = re.compile(r"(?<![\w:.>])(" + "|".join(ENTRY_POINTS) + r")\s*\(")


def script_module_files():
    owned = set()
    for line in MANIFEST.read_text().splitlines():
        if not line or line.startswith("#"):
            continue
        cols = line.split("\t")
        if len(cols) >= 2 and cols[1] == "script":
            owned.add(cols[0])
    return owned


def main():
    owned = script_module_files()
    violations = []
    for path in sorted((ROOT / "src/map").glob("*.cpp")):
        if path.name in owned:
            continue
        for number, line in enumerate(path.read_text(errors="replace").splitlines(), 1):
            code = line.split("//", 1)[0]
            if "ScriptHost-exempt:" in line or not CALL.search(code):
                continue
            violations.append(f"src/map/{path.name}:{number}: {line.strip()}")
    if violations:
        print("Direct script VM calls outside the script module (use ScriptHost, or mark the line "
              "'// ScriptHost-exempt: <reason>'):")
        print("\n".join(violations))
        return 1
    print(f"check_script_host: ok ({len(owned)} script-module files exempt)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
