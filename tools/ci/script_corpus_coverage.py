#!/usr/bin/env python3
"""Buildin coverage of the script corpus (tools/ci/script_corpus/*.txt).

A script command counts as exercised when a corpus file calls it: its name appears as an
identifier in script code (comments and string literals removed; labels, variables and NPC
header fields do not count). The denominator is every effective command in the buildin
manifest, doc/script_buildins.tsv (tools/codegen/script_buildins.py).

    python3 tools/ci/script_corpus_coverage.py            # summary
    python3 tools/ci/script_corpus_coverage.py --list     # also the exercised names
    python3 tools/ci/script_corpus_coverage.py --missing  # also the commands not exercised
"""
from __future__ import annotations

import argparse
import importlib.util
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CORPUS = ROOT / "tools/ci/script_corpus"


def load_manifest() -> list[dict[str, str]]:
    spec = importlib.util.spec_from_file_location("script_buildins", ROOT / "tools/codegen/script_buildins.py")
    mod = importlib.util.module_from_spec(spec)
    sys.dont_write_bytecode = True  # no __pycache__ in tools/codegen
    sys.modules[spec.name] = mod  # dataclasses look the module up while it loads
    spec.loader.exec_module(mod)
    return mod.read_manifest()


def code_identifiers(text: str) -> set[str]:
    """Identifiers used as calls/commands in the script code of one corpus file."""
    out, i, n = [], 0, len(text)
    while i < n:  # drop comments and string literals
        if text.startswith("//", i):
            while i < n and text[i] != "\n":
                i += 1
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            i = n if j < 0 else j + 2
        elif text[i] == '"':
            j = i + 1
            while j < n and text[j] != '"':
                j += 2 if text[j] == "\\" else 1
            out.append('""')
            i = j + 1
        else:
            out.append(text[i])
            i += 1
    code = "".join(out)
    names = set()
    for line in code.splitlines():
        if "\t" in line and not line.startswith(("\t", " ")):
            # NPC header: "map,x,y,dir<TAB>script<TAB>Name<TAB>sprite,{" -- keep only what follows "{"
            line = line.split("{", 1)[1] if "{" in line else ""
        for m in re.finditer(r"(?<![\w.@$#'])([A-Za-z_]\w*)(?!\w)", line):
            rest = line[m.end():].lstrip()
            if rest.startswith(":") and not rest.startswith("::"):
                continue  # a label
            names.add(m.group(1))
    return names


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--missing", action="store_true")
    opts = ap.parse_args()

    manifest = load_manifest()
    by_name = {row["name"]: row for row in manifest}
    used: set[str] = set()
    for path in sorted(CORPUS.glob("*.txt")):
        used |= code_identifiers(path.read_text(encoding="utf-8", errors="replace"))
    exercised = sorted(n for n in used if n in by_name)
    headless = [r for r in manifest if r["player"] == "no"]
    headless_hit = [r for r in headless if r["name"] in used]
    print(f"buildin corpus coverage: {len(exercised)}/{len(manifest)} script commands "
          f"({100 * len(exercised) / len(manifest):.1f}%); "
          f"{len(headless_hit)}/{len(headless)} of the commands that never resolve a player")
    if opts.list:
        print("exercised: " + " ".join(exercised))
    if opts.missing:
        print("not exercised: " + " ".join(sorted(set(by_name) - set(exercised))))
    return 0


if __name__ == "__main__":
    sys.exit(main())
