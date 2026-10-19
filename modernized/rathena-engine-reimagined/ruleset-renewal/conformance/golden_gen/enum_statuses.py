#!/usr/bin/env python3
"""Enumerates which statuses the RENEWAL status_calc_* functions of src/map/status.cpp reference.

usage: enum_statuses.py <repo-root> [--json OUT] [--table OUT.inc] [--md OUT.md]

Parses the legacy source (read-only), keeps the `#ifdef RENEWAL` / `#ifdef RENEWAL_ASPD` branches and drops the `#ifndef` ones, strips
comments, and records for every function the SC_* names it reads (with the source line of each first reference). The function list is
the one the golden hook calls (golden_status_hook.inc). Writes a JSON report (function -> status -> [lines]) and prints the counts.
"""
import json, re, sys, pathlib

ROOT = None  # the legacy checkout; set by load(root) (or by main from argv)
SRC = []
DEFINED = {"RENEWAL", "RENEWAL_ASPD", "RENEWAL_STAT"}

# function key -> C++ definition (the line that starts the definition; searched by text)
FUNCS = {
    "str": "static uint16 status_calc_str(", "agi": "static uint16 status_calc_agi(", "vit": "static uint16 status_calc_vit(",
    "int": "static uint16 status_calc_int(", "dex": "static uint16 status_calc_dex(", "luk": "static uint16 status_calc_luk(",
    "pow": "static uint16 status_calc_pow(", "sta": "static uint16 status_calc_sta(", "wis": "static uint16 status_calc_wis(",
    "spl": "static uint16 status_calc_spl(", "con": "static uint16 status_calc_con(", "crt": "static uint16 status_calc_crt(",
    "batk": "static int32 status_calc_batk(", "watk": "static uint16 status_calc_watk(",
    "matk_flat": "uint16 status_calc_pseudobuff_matk(",
    "cri": "static int16 status_calc_critical(", "hit": "static int16 status_calc_hit(", "flee": "static int16 status_calc_flee(",
    "flee2": "static int16 status_calc_flee2(", "def": "static defType status_calc_def(", "def2": "static int16 status_calc_def2(",
    "mdef": "static defType status_calc_mdef(", "mdef2": "static int16 status_calc_mdef2(", "speed": "static uint16 status_calc_speed(",
    "aspd": "static int16 status_calc_aspd(", "fix_aspd": "static int16 status_calc_fix_aspd(",
    "aspd_rate": "static int16 status_calc_aspd_rate(", "patk": "static int16 status_calc_patk(", "smatk": "static int16 status_calc_smatk(",
    "res": "static int16 status_calc_res(", "mres": "static int16 status_calc_mres(", "hplus": "static int16 status_calc_hplus(",
    "crate": "static int16 status_calc_crate(", "maxhp": "static uint32 status_calc_maxhp(block_list *bl, uint64 maxhp)\n",
    "maxsp": "static uint32 status_calc_maxsp(block_list *bl, uint64 maxsp)\n", "maxap": "static uint32 status_calc_maxap(block_list *bl, uint64 maxap)\n",
    "def_ele": "static unsigned char status_calc_element(", "ele_lv": "static unsigned char status_calc_element_lv(",
    "atk_ele": "unsigned char status_calc_attack_element(", "mode": "static int32 status_calc_mode(",
}
# macros that expand to a status read (pc.hpp:1181 pc_iscarton is SC_PUSH_CART when NEW_CARTS is defined, mmo.hpp:1204)
MACRO_SC = {"pc_iscarton": "SC_PUSH_CART"}
# helper functions whose body is part of a function above (status_calc_maxhp calls status_get_hpbonus ...)
HELPERS = {"maxhp": ["static int32 status_get_hpbonus(block_list *bl, enum e_status_bonus type) {"],
           "maxsp": ["static int32 status_get_spbonus(block_list *bl, enum e_status_bonus type) {"],
           "maxap": ["static int32 status_get_apbonus(block_list *bl, enum e_status_bonus type) {"]}


def load(root):
    global ROOT, SRC
    ROOT = pathlib.Path(root)
    SRC = (ROOT / "src/map/status.cpp").read_text().split("\n")


def find_def(text):
    first = text.split("\n")[0]
    for i, l in enumerate(SRC):
        if l.startswith(first) and not l.rstrip().endswith(";"):
            return i
    sys.exit("definition not found: " + text)


def body_range(start):
    """(first_line, last_line) 0-based of the function whose definition begins at `start` (brace matching on stripped code)."""
    depth, seen = 0, False
    for i in range(start, len(SRC)):
        code = re.sub(r'//.*', '', SRC[i])
        for ch in code:
            if ch == "{":
                depth += 1; seen = True
            elif ch == "}":
                depth -= 1
        if seen and depth == 0:
            return start, i
    sys.exit("unterminated function")


def active_lines(a, b):
    """Yields (lineno 1-based, code) for the lines that are compiled with RENEWAL on."""
    stack = []  # entries: (taken_now, parent_active, else_seen)
    active = True
    in_block_comment = False
    for i in range(a, b + 1):
        raw = SRC[i]
        s = raw.strip()
        m = re.match(r'#\s*(ifdef|ifndef|if|else|elif|endif)\b\s*(\S*)', s)
        if m:
            d, arg = m.group(1), m.group(2)
            if d in ("ifdef", "ifndef"):
                cond = (arg in DEFINED) == (d == "ifdef")
                stack.append((active, cond))
                active = active and cond
            elif d == "if":
                sys.exit(f"unsupported #if at line {i+1}: {s}")
            elif d == "else":
                par, cond = stack[-1]
                active = par and not cond
            elif d == "endif":
                par, cond = stack.pop()
                active = par
            continue
        if not active:
            continue
        code = re.sub(r'//.*', '', raw)
        out = ""
        j = 0
        while j < len(code):
            if in_block_comment:
                k = code.find("*/", j)
                if k < 0:
                    j = len(code)
                else:
                    in_block_comment = False; j = k + 2
            else:
                k = code.find("/*", j)
                if k < 0:
                    out += code[j:]; j = len(code)
                else:
                    out += code[j:k]; in_block_comment = True; j = k + 2
        yield i + 1, out


def enumerate_statuses(root):
    """{"functions": {fn: {"lines": [first, last], "statuses": {SC_X: [lines]}}}, "statuses": [sorted SC names]}"""
    load(root)
    report = {}
    for key, text in FUNCS.items():
        a, b = body_range(find_def(text))
        ranges = [(a, b, key)]
        for h in HELPERS.get(key, []):
            ha, hb = body_range(find_def(h)); ranges.append((ha, hb, key))
        entry = {"lines": [a + 1, b + 1], "statuses": {}}
        for ra, rb, _ in ranges:
            for ln, code in active_lines(ra, rb):
                for macro, msc in MACRO_SC.items():
                    if re.search(r'\b' + macro + r'\(', code):
                        entry["statuses"].setdefault(msc, [])
                        if ln not in entry["statuses"][msc]:
                            entry["statuses"][msc].append(ln)
                for sc in re.findall(r'\bSC_[A-Z0-9_]+\b', code):
                    entry["statuses"].setdefault(sc, [])
                    if ln not in entry["statuses"][sc]:
                        entry["statuses"][sc].append(ln)
        report[key] = entry
    union = sorted({s for e in report.values() for s in e["statuses"]})
    return {"functions": report, "statuses": union}


def main():
    out = enumerate_statuses(sys.argv[1])
    report, union = out["functions"], out["statuses"]
    if "--json" in sys.argv:
        pathlib.Path(sys.argv[sys.argv.index("--json") + 1]).write_text(json.dumps(out, indent=1, sort_keys=True) + "\n")
    if "--md" in sys.argv:
        L = ["# Statuses read by the RENEWAL status_calc_* functions", "",
             "GENERATED by `conformance/golden_gen/enum_statuses.py` from `src/map/status.cpp` (the `#ifdef RENEWAL` / `RENEWAL_ASPD` branches are kept, the `#ifndef` ones dropped). "
             "Do not edit.", "",
             f"{len(report)} functions, **{len(union)} distinct statuses** referenced (`pc_iscarton` is counted as `SC_PUSH_CART`: `NEW_CARTS`, `pc.hpp:1181`).", "",
             "| function | status.cpp lines | statuses |", "|---|---|---|"]
        for k, e in report.items():
            L.append(f"| {k} | {e['lines'][0]}-{e['lines'][1]} | {len(e['statuses'])} |")
        L += ["", "| status | functions (first line of each reference) |", "|---|---|"]
        for sc in union:
            refs = ", ".join(f"{k}:{min(e['statuses'][sc])}" for k, e in report.items() if sc in e["statuses"])
            L.append(f"| {sc} | {refs} |")
        pathlib.Path(sys.argv[sys.argv.index("--md") + 1]).write_text("\n".join(L) + "\n")
    if "--table" in sys.argv:  # include file for the hook: name -> sc_type, for EVERY status of enum sc_type (status.hpp); the referenced ones must be in it
        hpp = (ROOT / "src/map/status.hpp").read_text().split("\n")
        a = next(i for i, l in enumerate(hpp) if l.startswith("enum sc_type"))
        names = []
        for l in hpp[a + 1:]:
            if l.startswith("};"):
                break
            m = re.match(r'\s*(SC_[A-Z0-9_]+)\s*(=[^,/]*)?,?\s*(//.*|/\*.*)?$', l)
            if m and m.group(1) not in ("SC_NONE", "SC_MAX"):
                names.append(m.group(1))
        missing = [n for n in union if n not in names]
        if missing:
            sys.exit("referenced statuses missing from enum sc_type: " + ", ".join(missing))
        pathlib.Path(sys.argv[sys.argv.index("--table") + 1]).write_text("".join(f'\t{{ "{n}", {n} }},\n' for n in names))
        print(f"sc_type table: {len(names)} statuses")
    print(f"functions: {len(report)}; distinct statuses referenced (RENEWAL branches): {len(union)}")
    for k, e in report.items():
        print(f"  {k:10s} status.cpp:{e['lines'][0]}-{e['lines'][1]}  {len(e['statuses'])} statuses")


if __name__ == "__main__":
    main()
