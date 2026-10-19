#!/usr/bin/env python3
"""Writes docs/STATUS_FIELDS.md: for every covered status the names of val1..val4 (Q-F), where the legacy stat code reads each value
(status.cpp lines, from the maintained Luau definitions) and where status_change_start sets it (line, expression, the legacy comment).

usage: st_fields_doc.py [--status-cpp PATH] [--out docs/STATUS_FIELDS.md]
Run after editing scripts/status/defs*.luau (the field names live there; this tool only reads them).
"""
import argparse, collections, pathlib, re, sys
HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent.parent
sys.path.insert(0, str(HERE))
import st_extract as X
import st_gen_defs as G

ap = argparse.ArgumentParser()
ap.add_argument("--status-cpp", default=X.DEFAULT_CPP)
ap.add_argument("--out", default=str(ROOT / "docs" / "STATUS_FIELDS.md"))
a = ap.parse_args()

defs_dir = ROOT / "scripts" / "status"
fields, use = {}, collections.defaultdict(list)   # status -> {N: name}; (status, name) -> [(attr, at)]
in_db = {}
for f in sorted((defs_dir / "defs").glob("defs_*.luau")):
    cur, attr = None, None
    for line in f.read_text().split("\n"):
        m = re.match(r'D\("([^"]+)"', line)
        if m:
            cur = m.group(1); attr = None
            fields[cur] = {}
            in_db[cur] = True
        if cur and "in_pre_db = false" in line:
            in_db[cur] = False
        m = re.match(r'\tfields = \{ (.*) \},', line)
        if m and cur:
            for mm in re.finditer(r'val([1-4]) = "([a-z0-9_]+)"', m.group(1)):
                fields[cur][int(mm.group(1))] = mm.group(2)
        m = re.match(r'\t\t(\w+) = \{$', line)
        if m:
            attr = m.group(1)
        if cur and attr and "at = " in line:
            at = int(re.search(r"at = (\d+)", line).group(1))
            for mm in re.finditer(r"\bs\.([a-z0-9_]+)", line):
                use[(cur, mm.group(1))].append((attr, at))
            for mm in re.finditer(r"e\.sc\.([A-Z0-9_]+)\.([a-z0-9_]+)", line):
                use[(mm.group(1), mm.group(2))].append((attr, at))
# the hand-written overlay: extend("NAME", ...) blocks and the matk() helper
hand = (defs_dir / "defs_hand.luau").read_text()
cur = None
for line in hand.split("\n"):
    m = re.search(r'status\.extend\("([A-Z0-9_]+)"', line)
    if m: cur = m.group(1)
    m = re.match(r'matk\("([A-Z0-9_]+)", (\d+), "([a-z0-9_]+)"', line)
    if m: use[(m.group(1), m.group(3))].append(("matk", int(m.group(2))))
    m = re.match(r'matk\("([A-Z0-9_]+)", (\d+), nil, function\(s, e\) return .*?s\.([a-z]+)', line)
    if m: use[(m.group(1), m.group(3))].append(("matk", int(m.group(2))))
    if cur and re.search(r"\bat = (\d+)", line):
        at = int(re.search(r"at = (\d+)", line).group(1))
        for mm in re.finditer(r"\bs\.([a-z0-9_]+)", line):
            use[(cur, mm.group(1))].append(("(hand)", at))
for line in hand.split("\n"):    # multi-line entries: attribute fields referenced on the lines after `at =`
    pass
extra_use = {   # entries of defs_hand.luau whose field reads are on a different line than `at =`
    ("NIBELUNGEN", "atk_inc"): [("watk", 7380)], ("ASSNCROS", "aspd_rate_quicken"): [("aspd_rate", 8474)],
    ("ASPDPOTION0", "aspd_rate_dec"): [("aspd_rate", 8501)], ("ASPDPOTION1", "aspd_rate_dec"): [("aspd_rate", 8501)],
    ("ASPDPOTION2", "aspd_rate_dec"): [("aspd_rate", 8501)], ("ASPDPOTION3", "aspd_rate_dec"): [("aspd_rate", 8501)],
    ("FIRE_INSIGNIA", "level"): [("matk", 7495)],
}
for k, v in extra_use.items():
    use[k] = sorted(set(use[k]) | set(v))

res, _ = G.build_all(a.status_cpp)
prod = G.producers(a.status_cpp)
db_names = {}
for m in re.finditer(r'\["([^"]+)"\] = \{ id = "[^"]+", sc = (\d+)', (ROOT / "data" / "status_records.luau").read_text()):
    db_names[m.group(1)] = int(m.group(2))

out = ["# Status value fields: val1..val4 named per status (open question Q-F)", "",
       "The status database never names `val1..val4`: their meaning lives in `src/map/status.cpp` (`status_change_start` sets them, the `status_calc_*`",
       "functions read them). For the " + str(len(fields)) + " statuses the stat functions of the pre-renewal branch know, the package names them",
       "(`scripts/status/defs/*.luau`, `fields = { val1 = ..., }`; a status instance carries both the names and `val1..val4`). The save format can adopt",
       "the names status by status; until then `val1..val4` stay the persisted keys.", "",
       "* **Name**: the Luau field name. Rule: `<attribute>_<inc|dec|pct_inc|pct_dec|set>` for a value added / subtracted / applied as a percentage to the",
       "  attribute(s) named, `lv` for the skill level that other values derive from, a short curated name for flags, enums and packed values",
       "  (`str_agi_vit_packed` = three bytes `(v >> 16) & 0xFF`, `(v >> 8) & 0xFF`, `v & 0xFF`), `valN_unused` when the covered stat code does not read it.",
       "* **Read at**: `status.cpp:line` of the contribution that reads the value (attribute in front); the lines are also the `at =` keys of the definitions.",
       "* **Set at**: the line in `status_change_start` (and the expression and comment of the legacy) where the value is assigned; `-` when the value comes",
       "  straight from the caller (`sc_start4(..., val1, val2, val3, val4)`) or is set in a path this extraction does not follow.",
       "* Statuses marked **not in PRE db** are known to the stat code but absent from `db/pre-re/status.yml`: a pre-renewal server never starts them.", "",
       "Read by code other than the stat functions (`scripts/status/states.luau`, `chain.luau`, `scripted.luau`): `GOSPEL.val4`, `BASILICA.val4`, `GRAVITATION.val3`,",
       "`CAMOUFLAGE.val1`, `MAGNETICFIELD.val2`, `FEAR.val2`, `DANCING.val1/val4`, `OBLIVIONCURSE.val3`, `NOCHAT.val1` (states, status.cpp:5606-5850), `MAGICPOWER.val3/val4`",
       "(`status_calc_bl_main`, 6268), every `getstatus(SC_X, n)` of a status script (`scripts/status/scripted.luau`).", ""]
total = sum(len(v) for v in fields.values())
unused = sum(1 for st in fields for n in fields[st] if fields[st][n].endswith("_unused"))
read = sum(1 for st in fields for n in fields[st] if use.get((st, fields[st][n])))
out.insert(2, f"{total} value slots in {len(fields)} statuses: {read} are read by the covered stat code (named by role), {unused} are `valN_unused` (set by the legacy, "
              "but read only by the combat, skill or status-start code outside this scope: battle.cpp, skill.cpp, the rest of status.cpp), the others are "
              "named from their legacy comment or a curated name (flags, enums, packed values). Where the legacy comment is misleading it is noted below the table.")
out.insert(3, "")
out.append("| Status | sc id | val | Name | Read at (status.cpp) | Set at (status_change_start) |")
out.append("|---|---|---|---|---|---|")
for st in sorted(fields):
    flag = "" if in_db.get(st, True) else " *(not in PRE db)*"
    for n in range(1, 5):
        nm = fields[st].get(n, "?")
        reads = sorted(set(use.get((st, nm), [])), key=lambda x: (x[1], x[0]))
        rtxt = ", ".join(f"{at} {attr}" for attr, at in reads) or "-"
        p = prod.get(st, {}).get(n)
        ptxt = "-"
        if p:
            ptxt = f"{p[0]}: `val{n} = {p[1].replace('|', '/')}`" + (f" // {p[2].replace('|', '/')}" if p[2] else "")
        out.append(f"| {st if n == 1 else ''}{flag if n == 1 else ''} | {db_names.get(st, '') if n == 1 else ''} | {n} | `{nm}` | {rtxt} | {ptxt} |")
out += ["", "## Notes", "",
        "* `ADRENALINE` / `ADRENALINE2` / `WEAPONPERFECTION` / `OVERTHRUST` share one block in `status_change_start` (status.cpp:11611-11627): the comment says "
        "\"Power increase\" for val3 but Adrenaline's val3 is the ASPD value (`200` or `300`, read at 8452 / 8456).",
        "* `BLESSING.val2` is `val1` on players and on non-undead, non-demon monsters, and `0` otherwise: `0` means \"halve the stat\" (status.cpp:11588-11592, read at 6800-6803).",
        "* `CONCENTRATE.val3/val4` are the card bonuses of AGI / DEX at the time of casting, excluded from the percentage (status.cpp:11598-11608).",
        "* `DEFSET.val1`, `MDEFSET.val1`, `OVERED_BOOST.val2` are returned **unclamped and narrowed** to the return type (signed char / int16): `val1 = 40000` gives DEF 64 "
        "(`Q-defset-narrowing` in the golden data).",
        "* `MARIONETTE.val3/val4`, `SPIRIT.val3/val4` pack three stats per value, read with `(v >> 16) & 0xFF`, `(v >> 8) & 0xFF`, `v & 0xFF`.",
        "* `JOINTBEAT.val2` is a bit set (`BREAK_ANKLE` 1, `BREAK_WRIST` 2, `BREAK_KNEE` 4, `BREAK_SHOULDER` 8, `BREAK_WAIST` 16).", ""]
pathlib.Path(a.out).parent.mkdir(parents=True, exist_ok=True)
pathlib.Path(a.out).write_text("\n".join(out) + "\n")
print(f"{len(fields)} statuses, {sum(len(v) for v in fields.values())} fields -> {a.out}")
