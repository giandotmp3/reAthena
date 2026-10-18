#!/usr/bin/env python3
"""Compare the migrator's merged result with the post-load dump of the REAL legacy loader (legacy_dump/).

    compare_legacy.py --mode prerenewal --dump <dump-dir> [--root <repo>] [--import-dir <dir>] [--json report.json]

For every DB: entry sets (ids only in one side), then every dumped field of every entry, field by field. Mismatches are
grouped per (db, field); known, explained classes (see KNOWN) are labelled, anything else is labelled UNEXPLAINED and
fails the run (exit 1) unless --allow-unexplained.
"""
import argparse
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))

from rmig import pipeline  # noqa: E402

# (db, field) -> (classification, explanation, predicate(legacy_value, migrator_value) -> True when the difference is
# exactly the explained one). A difference that fails its predicate is still reported as UNEXPLAINED.
#   migrator-bug | loader-quirk | not-stored-by-loader
def _layout_ok(leg, mine):
    # skill_init_unit_layout() rewrites a '-1' (special layout) to the index of the hard-coded layout of that skill id
    return all((m == l) or (m == -1 and l >= 0) for l, m in zip(leg, mine)) and len(leg) == len(mine)


def _maxparam_ok(leg, mine):
    # JobDatabase::loadingFinished fills every max_param the YAML did not set from battle.conf max_*_parameter
    return all(m == 0 or m == l for l, m in zip(leg, mine))


KNOWN = {
    ("skill_db", "unit_layout"): ("loader-quirk", "special layout -1 is replaced by a hard-coded layout-table index in skill_init_unit_layout()", _layout_ok),
    ("job_stats", "max_param"): ("not-stored-by-yaml", "defaults come from battle.conf max_parameter & class family (loadingFinished); only explicit MaxStats are modelled", _maxparam_ok),
}


def jl(path):
    out = []
    with open(path, "r", encoding="utf-8", errors="surrogateescape") as f:
        for line in f:
            line = line.strip()
            if line:
                out.append(json.loads(line))
    return out


import re as _re


def script_has_code(text):
    """parse_script() returns nullptr for a script that compiles to nothing (only comments / whitespace / braces)."""
    if text is None:
        return False
    t = _re.sub(r"/\*.*?\*/", " ", text, flags=_re.S)
    t = _re.sub(r"//[^\n]*", " ", t)
    t = t.replace("{", " ").replace("}", " ")
    return t.strip() != ""


def row_item(row):
    r = dict(row)
    for a, b in (("script", "has_script"), ("equip_script", "has_equip_script"), ("unequip_script", "has_unequip_script")):
        r[b] = script_has_code(r.pop(a))
    return r


def row_status(row):
    r = dict(row)
    r["has_script"] = script_has_code(r.pop("script"))
    return r


def row_mob(row):
    r = dict(row)
    return r


def legacy_mob(d):
    d = dict(d)
    for k in ("sprite", "name", "jname", "title"):
        d[k] = d[k].rstrip("\x00")      # name.resize(NAME_LENGTH) pads with NULs
    return d


def compare(db, dump_rows, key, mine, mapper=None, skip=(), legacy_mapper=None):
    """returns summary dict"""
    legacy = {}
    for d in dump_rows:
        if legacy_mapper:
            d = legacy_mapper(d)
        legacy[d[key] if not isinstance(key, tuple) else tuple(d[k] for k in key)] = d
    mine_m = {}
    for k, row in mine.items():
        mine_m[k] = mapper(row) if mapper else row
    only_legacy = sorted(set(legacy) - set(mine_m), key=str)
    only_mine = sorted(set(mine_m) - set(legacy), key=str)
    common = [k for k in legacy if k in mine_m]
    fields = {}
    explained = {}
    matched_entries = 0
    field_checks = 0
    field_match = 0
    for k in common:
        d = legacy[k]
        m = mine_m[k]
        bad = False
        for f, v in d.items():
            if f in skip:
                continue
            field_checks += 1
            mv = m.get(f, "<absent>")
            if mv == v:
                field_match += 1
                continue
            kn = KNOWN.get((db, f))
            if kn is not None and kn[2](v, mv):
                ex = explained.setdefault(f, {"count": 0, "class": kn[0], "note": kn[1]})
                ex["count"] += 1
                continue
            bad = True
            e = fields.setdefault(f, {"count": 0, "examples": []})
            e["count"] += 1
            if len(e["examples"]) < 3:
                e["examples"].append({"key": str(k), "legacy": v if len(json.dumps(v)) < 300 else "<long>", "migrator": mv if len(json.dumps(mv, default=str)) < 300 else "<long>"})
        if not bad:
            matched_entries += 1
    return {"db": db, "legacy_entries": len(legacy), "migrator_entries": len(mine_m), "common": len(common),
            "entries_identical": matched_entries, "only_in_legacy": [str(x) for x in only_legacy[:20]],
            "only_in_legacy_count": len(only_legacy), "only_in_migrator": [str(x) for x in only_mine[:20]],
            "only_in_migrator_count": len(only_mine), "field_checks": field_checks, "field_matches": field_match,
            "mismatched_fields": fields, "explained": explained}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--mode", required=True, choices=["prerenewal", "renewal"])
    ap.add_argument("--dump", required=True)
    ap.add_argument("--root", default=os.path.abspath(os.path.join(HERE, "..", "..", "..", "..", "..")))
    ap.add_argument("--import-dir", default="db/import")
    ap.add_argument("--json")
    ap.add_argument("--allow-unexplained", action="store_true")
    a = ap.parse_args()
    L = pipeline.load_mode(a.root, a.mode, import_dir=a.import_dir, raw=False)
    d = a.dump
    rep = []
    rep.append(compare("item_db", jl(os.path.join(d, "item_db.jsonl")), "id", L.dbs["item_db"].rows, row_item))
    rep.append(compare("mob_db", jl(os.path.join(d, "mob_db.jsonl")), "id", L.dbs["mob_db"].rows, legacy_mapper=legacy_mob))
    rep.append(compare("skill_db", jl(os.path.join(d, "skill_db.jsonl")), "id", L.dbs["skill_db"].rows))
    rep.append(compare("status", jl(os.path.join(d, "status.jsonl")), "type", L.dbs["status"].rows, row_status))
    rep.append(compare("size_fix", jl(os.path.join(d, "size_fix.jsonl")), "weapon", L.dbs["size_fix"].rows))
    rep.append(compare("attr_fix", jl(os.path.join(d, "attr_fix.jsonl")), "level", L.dbs["attr_fix"].rows))
    rep.append(compare("statpoint", jl(os.path.join(d, "statpoint.jsonl")), "level", L.dbs["statpoint"].rows))
    rep.append(compare("job_stats", jl(os.path.join(d, "job_stats.jsonl")), "job", L.dbs["job_stats"].rows))
    rep.append(compare("refine", jl(os.path.join(d, "refine.jsonl")), ("group", "level", "refine_level"), L.dbs["refine"].flat()))
    unexplained = 0
    print("mode=%s dump=%s import_dir=%s" % (a.mode, d, a.import_dir))
    for r in rep:
        print("%-10s legacy=%d migrator=%d common=%d identical_entries=%d field_checks=%d field_matches=%d only_legacy=%d only_migrator=%d" % (
            r["db"], r["legacy_entries"], r["migrator_entries"], r["common"], r["entries_identical"], r["field_checks"],
            r["field_matches"], r["only_in_legacy_count"], r["only_in_migrator_count"]))
        for f, e in sorted(r["explained"].items()):
            print("   explained %-22s %6d  [%s] %s" % (f, e["count"], e["class"], e["note"]))
        for f, e in sorted(r["mismatched_fields"].items()):
            unexplained += 1
            print("   MISMATCH %-22s %6d  [UNEXPLAINED]" % (f, e["count"]))
            for ex in e["examples"][:2]:
                print("        ", ex)
        if r["only_in_legacy"]:
            print("   only in legacy:", r["only_in_legacy"][:8])
        if r["only_in_migrator"]:
            print("   only in migrator:", r["only_in_migrator"][:8])
    if a.json:
        with open(a.json, "w") as f:
            json.dump({"mode": a.mode, "import_dir": a.import_dir, "dbs": rep}, f, indent=1, sort_keys=True)
    return 0 if (unexplained == 0 or a.allow_unexplained) else 1


if __name__ == "__main__":
    sys.exit(main())
