#!/usr/bin/env python3
"""Numerics lint (M0 finding): ruleset code must not use `/` or `//` (Luau floors `//` and `/` is a double divide, legacy C++ truncates
signed integers). All integer division goes through ctx.idiv / ctx.mul_div / the accumulator methods. Also flags `%` outside the two
documented uses (non-negative RNG output, wrap-around of narrowing, parity of a float mantissa). Scans scripts/damage and scripts/formulas.luau. A division that is a FLOATING POINT operation the legacy code also does in floating point carries a `lint:float` marker (scripts/damage/fp.luau only).

usage: lint_numeric.py        exit 1 with file:line on a finding
"""
import pathlib, re, sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
FILES = sorted((ROOT / "scripts" / "damage").glob("*.luau")) + [ROOT / "scripts" / "formulas.luau"]
STR = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'')
bad = 0
for f in FILES:
    for n, line in enumerate(f.read_text().splitlines(), 1):
        code = STR.sub('""', line)
        code = code.split("--", 1)[0]
        if "/" in code and "lint:float" not in line:
            print(f"{f.relative_to(ROOT)}:{n}: division operator in ruleset code: {line.strip()}")
            bad += 1
        if "%" in code and "rng.rnd()" not in code and "+ 32768" not in code and "% (bit * 2)" not in code \
                and "% 65536" not in code and "% 2 == 1" not in code:
            print(f"{f.relative_to(ROOT)}:{n}: '%' outside the documented uses: {line.strip()}")
            bad += 1
sys.exit(1 if bad else 0)
