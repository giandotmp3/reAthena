#!/usr/bin/env python3
"""Snapshot a subset of the REAL legacy loader's post-load dump into tests/fixtures/legacy_golden.json so the unit tests can
check the migrator against the legacy loader without a legacy build (the full comparison is tools/compare_legacy.py).

    make_golden.py --dump prerenewal:base=<dir> --dump prerenewal:patch=<dir> ... [--out tests/fixtures/legacy_golden.json]
"""
import argparse
import json
import os

KEYS = {"item_db": "id", "mob_db": "id", "skill_db": "id", "status": "type", "size_fix": "weapon", "attr_fix": "level",
        "statpoint": "level", "job_stats": "job"}
PATCH_IDS = {"item_db": [501, 502, 1101, 31990, 31991, 31992, 31993, 31994], "mob_db": [1002, 31990, 31991],
             "skill_db": [5, 29990, 29991], "status": [0, 4], "size_fix": None, "attr_fix": None, "statpoint": [1, 9, 10, 11],
             "job_stats": [1]}
SAMPLE_EVERY = 100


def rows(d, db):
    out = {}
    with open(os.path.join(d, db + ".jsonl"), encoding="utf-8", errors="surrogateescape") as f:
        for line in f:
            r = json.loads(line)
            out[r[KEYS[db]]] = r
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dump", action="append", required=True, help="mode:scenario=dir  (scenario: base|patch|clear)")
    ap.add_argument("--out", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tests", "fixtures", "legacy_golden.json"))
    a = ap.parse_args()
    gold = {}
    for spec in a.dump:
        left, d = spec.split("=", 1)
        mode, scen = left.split(":")
        g = gold.setdefault(mode, {}).setdefault(scen, {})
        for db in KEYS:
            r = rows(d, db)
            ent = {"total": len(r)}
            if scen == "base":
                keys = sorted(r)
                ent["rows"] = {str(k): r[k] for i, k in enumerate(keys) if i % SAMPLE_EVERY == 0}
            elif scen == "patch":
                ids = PATCH_IDS[db]
                ent["rows"] = {str(k): r[k] for k in (ids if ids is not None else sorted(r)) if k in r}
                ent["absent"] = [k for k in (ids or []) if k not in r]
            else:  # clear: whole DB for the small ones, nothing for the big ones
                if len(r) <= 300 and db in ("mob_db", "size_fix", "status", "statpoint", "attr_fix"):
                    ent["rows"] = {str(k): r[k] for k in sorted(r)}
            g[db] = ent
    with open(a.out, "w") as f:
        json.dump(gold, f, sort_keys=True, separators=(",", ":"))
        f.write("\n")
    print("wrote", a.out, os.path.getsize(a.out), "bytes")


if __name__ == "__main__":
    main()
