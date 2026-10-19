#!/usr/bin/env python3
"""Architecture proof, part 2: how much of the RENEWAL status layer is data-only and how much is logic that differs from PRE?

Everything is measured on the legacy source and the migrated data, which are the specification of both rulesets (the two Luau packages stay independent):

  * the status functions of src/map/status.cpp are parsed twice, with the RENEWAL branches (renewal.hpp: RENEWAL, RENEWAL_ASPD, RENEWAL_STAT) and without;
    every statement is attached to the statuses it references. For a (function, status) site the contribution is "identical by structure" when the statement
    text is the same in both modes, and a status is identical when all its sites are and they keep their relative order;
  * the skeleton of a function (its statements that name no status: guards, clamps, early returns, formulas) decides whether the stage list of the attribute
    differs: equal skeleton = same stage list, only the contributions are data; different skeleton = logic;
  * the status records (db/re/status.yml + db/status.yml against db/pre-re/status.yml through rathena-migrate): count, common, identical, which field differs.

usage: status_overlap.py [--repo <legacy checkout>] [--migrated <dir with prerenewal/ and renewal/>] [--md OUT] [--json OUT]
"""
import argparse, json, pathlib, re, sys

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(ROOT / "conformance" / "golden_gen"))
sys.path.insert(0, str(HERE))
import enum_statuses as E  # noqa: E402
import gen_status_records as R  # noqa: E402

MODES = {"RE": {"RENEWAL", "RENEWAL_ASPD", "RENEWAL_STAT"}, "PRE": set()}


def active_at(n, defined):
    """is source line n (0-based) compiled when the macros in `defined` are defined? (#ifdef / #ifndef / #else / #endif of status.cpp)"""
    stack, active = [], True
    for i in range(n):
        m = re.match(r'\s*#\s*(ifdef|ifndef|if|else|elif|endif)\b\s*(\S*)', E.SRC[i])
        if not m:
            continue
        d, arg = m.groups()
        if d in ("ifdef", "ifndef"):
            cond = (arg in defined) == (d == "ifdef")
            stack.append((active, cond)); active = active and cond
        elif d == "else":
            par, cond = stack[-1]; active = par and not cond
        elif d == "endif":
            par, cond = stack.pop(); active = par
        elif d == "if":
            stack.append((active, True))  # the #if of status.cpp inside these functions guard unrelated code
    return active


def live(key, defined):
    """a status_calc_* function is live in a mode when its definition is compiled AND some compiled line calls it (status_calc_pow exists in pre-renewal
    builds too, but only the RENEWAL block of status_calc_bl_main calls it)"""
    text = E.FUNCS[key]
    first = text.split("\n")[0]
    di = next((i for i, l in enumerate(E.SRC) if l.startswith(first) and not l.rstrip().endswith(";")), None)
    if di is None or not active_at(di, defined):
        return False
    name = re.search(r"(status_calc_\w+|status_get_\w+)\(", first)
    if key in ("maxhp", "maxsp", "maxap"):
        return True
    if key == "matk_flat":
        return True
    callee = name.group(1) if name else None
    for i, l in enumerate(E.SRC):
        if i != di and callee and re.search(r"\b" + callee + r"\(", l) and not l.lstrip().startswith(("static ", "//")) and not re.match(r"^[a-z].*\)\s*;\s*$", l) \
                and active_at(i, defined) and not (i < 200):
            return True
    return False


def statements(root, mode):
    """{function key: [(first line, normalized text, set of statuses)]} for one mode"""
    E.load(root)
    E.DEFINED = MODES[mode]
    out = {}
    for key, text in E.FUNCS.items():
        if not live(key, MODES[mode]):
            continue  # the function is not part of this build (status_calc_aspd needs RENEWAL_ASPD; status_calc_pow is called by RENEWAL code only)
        a, b = E.body_range(E.find_def(text))
        ranges = [(a, b)] + [E.body_range(E.find_def(h)) for h in E.HELPERS.get(key, [])]
        stmts = []
        for ra, rb in ranges:
            cur, first = "", None
            for ln, code in E.active_lines(ra, rb):
                code = code.strip()
                if not code:
                    continue
                if first is None:
                    first = ln
                cur = (cur + " " + code).strip()
                if cur.endswith((";", "{", "}")):
                    stmts.append((first, re.sub(r"\s+", " ", cur), set(re.findall(r"\bSC_[A-Z0-9_]+\b", cur)) | ({"SC_PUSH_CART"} if "pc_iscarton(" in cur else set())))
                    cur, first = "", None
            if cur:
                stmts.append((first, re.sub(r"\s+", " ", cur), set(re.findall(r"\bSC_[A-Z0-9_]+\b", cur))))
        # a statement that is only the closing/opening of a block carries no logic of its own
        out[key] = [s for s in stmts if s[1] not in ("{", "}")]
    return out


def sites(stmts):
    """{function: {status: [statement texts]}} and {function: [status order]}"""
    by, order = {}, {}
    for fn, lst in stmts.items():
        by[fn], order[fn] = {}, []
        for _, text, scs in lst:
            for sc in sorted(scs):
                by[fn].setdefault(sc, []).append(text)
                if sc not in order[fn]:
                    order[fn].append(sc)
    return by, order


def lcs_len(a, b):
    m = [[0] * (len(b) + 1) for _ in range(len(a) + 1)]
    for i in range(len(a)):
        for j in range(len(b)):
            m[i + 1][j + 1] = m[i][j] + 1 if a[i] == b[j] else max(m[i][j + 1], m[i + 1][j])
    return m[-1][-1]


def measure(repo):
    re_st, pre_st = statements(repo, "RE"), statements(repo, "PRE")
    re_by, re_order = sites(re_st)
    pre_by, pre_order = sites(pre_st)
    fns = sorted(set(re_st) | set(pre_st))
    per_fn, site_counts = {}, {"identical": 0, "differs": 0, "re_only": 0, "pre_only": 0}
    status_state = {}
    for fn in fns:
        r, p = re_by.get(fn, {}), pre_by.get(fn, {})
        skeleton_re = [t for _, t, scs in re_st.get(fn, []) if not scs]
        skeleton_pre = [t for _, t, scs in pre_st.get(fn, []) if not scs]
        c = {"identical": 0, "differs": 0, "re_only": 0, "pre_only": 0}
        for sc in set(r) | set(p):
            if sc in r and sc in p:
                k = "identical" if r[sc] == p[sc] else "differs"
            else:
                k = "re_only" if sc in r else "pre_only"
            c[k] += 1
            site_counts[k] += 1
            status_state.setdefault(sc, []).append((fn, k))
        common = [s for s in re_order.get(fn, []) if s in pre_order.get(fn, [])]
        common_pre = [s for s in pre_order.get(fn, []) if s in re_order.get(fn, [])]
        in_order = lcs_len(common, common_pre) == len(common)
        per_fn[fn] = {"in_re": fn in re_st, "in_pre": fn in pre_st, "skeleton_equal": skeleton_re == skeleton_pre, "sites": c, "relative_order_kept": in_order,
                      "re_statuses": len(r), "pre_statuses": len(p)}
    statuses_re = sorted({s for by in re_by.values() for s in by})
    statuses_pre = sorted({s for by in pre_by.values() for s in by})
    ident = [s for s, lst in status_state.items() if all(k == "identical" for _, k in lst) and s in statuses_re and s in statuses_pre
             and all(per_fn[fn]["relative_order_kept"] for fn, _ in lst)]
    return {"functions": per_fn, "site_counts": site_counts, "statuses_re": len(statuses_re), "statuses_pre": len(statuses_pre),
            "statuses_both": len(set(statuses_re) & set(statuses_pre)), "statuses_re_only": sorted(set(statuses_re) - set(statuses_pre)),
            "statuses_pre_only": sorted(set(statuses_pre) - set(statuses_re)), "statuses_identical": len(ident), "identical_list": sorted(ident)}


def records(migrated):
    re_rec = R.parse((migrated / "renewal" / "status.luau").read_text())
    pre_rec = R.parse((migrated / "prerenewal" / "status.luau").read_text())
    common = sorted(set(re_rec) & set(pre_rec))
    differ = {}
    for k in common:
        for f in R.KEEP:
            if f in ("duration_lookup",):
                continue
            if re_rec[k].get(f) != pre_rec[k].get(f):
                differ.setdefault(f, []).append(k)
    identical = [k for k in common if not any(k in v for v in differ.values())]
    return {"re": len(re_rec), "pre": len(pre_rec), "common": len(common), "identical": len(identical), "re_only": len(set(re_rec) - set(pre_rec)),
            "pre_only": len(set(pre_rec) - set(re_rec)), "differing_fields": {f: len(v) for f, v in sorted(differ.items(), key=lambda kv: -len(kv[1]))}}


def render(m, rec):
    sc = m["site_counts"]
    total = sum(sc.values())
    both = sc["identical"] + sc["differs"]
    L = ["# Status layer: data versus logic (renewal against pre-renewal)", "",
         "GENERATED by `tools/status_overlap.py` from `src/map/status.cpp` (parsed twice, with and without the RENEWAL branches) and the migrated status databases. "
         "Read-only inputs; neither Luau package is consulted.", "",
         "## 1. Status records (data)", "",
         f"* renewal records {rec['re']}, pre-renewal records {rec['pre']}; in both {rec['common']}; renewal only {rec['re_only']}; pre-renewal only {rec['pre_only']}",
         f"* identical in both (states, calc flags, flags, options, min rate / duration, Fail / EndOnStart / EndOnEnd / EndReturn, script): **{rec['identical']} of {rec['common']}** "
         f"({100 * rec['identical'] // max(1, rec['common'])} %)",
         "* records that differ, by field: " + ", ".join(f"{f} {n}" for f, n in rec["differing_fields"].items()), "",
         "## 2. Contributions of the status_calc_* functions (the statements of status.cpp)", "",
         f"* statuses referenced: renewal {m['statuses_re']}, pre-renewal {m['statuses_pre']}, both {m['statuses_both']}, renewal only {len(m['statuses_re_only'])}, pre-renewal only {len(m['statuses_pre_only'])}",
         f"* (function, status) sites: {total}; present in both modes {both}: statement text identical **{sc['identical']}**, different {sc['differs']}; renewal only {sc['re_only']}; pre-renewal only {sc['pre_only']}",
         f"* statuses whose contributions are identical by structure in every function (same statement text and the same relative order): **{m['statuses_identical']} of {m['statuses_both']}** "
         f"statuses referenced in both modes ({100 * m['statuses_identical'] // max(1, m['statuses_both'])} %)", "",
         "## 3. Functions: skeleton (the stage list) and contributions", "",
         "| function | in RE | in PRE | skeleton equal | sites identical | differ | RE only | PRE only | relative order kept |", "|---|---|---|---|---|---|---|---|---|"]
    for fn, f in m["functions"].items():
        s = f["sites"]
        L.append(f"| {fn} | {'yes' if f['in_re'] else 'no'} | {'yes' if f['in_pre'] else 'no'} | {'yes' if f['skeleton_equal'] else '**no**'} | {s['identical']} | {s['differs']} | {s['re_only']} | {s['pre_only']} | {'yes' if f['relative_order_kept'] else '**no**'} |")
    eq = sum(1 for f in m["functions"].values() if f["in_re"] and f["in_pre"] and f["skeleton_equal"])
    both_fn = sum(1 for f in m["functions"].values() if f["in_re"] and f["in_pre"])
    L += ["", f"{eq} of {both_fn} functions that exist in both modes have an identical skeleton (same stage list): for them renewal differs from pre-renewal only in its contributions. "
          f"{both_fn - eq} differ in logic and need a different stage list.", ""]
    return "\n".join(L)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo", default=str(ROOT.parents[2]))
    ap.add_argument("--migrated", default=str(ROOT.parent / "tooling" / "migrate" / "out"))
    ap.add_argument("--md")
    ap.add_argument("--json")
    a = ap.parse_args()
    m = measure(a.repo)
    mig = pathlib.Path(a.migrated)
    rec = records(mig) if (mig / "prerenewal" / "status.luau").exists() else None
    if a.json:
        pathlib.Path(a.json).write_text(json.dumps({"measure": m, "records": rec}, indent=1, sort_keys=True) + "\n")
    text = render(m, rec) if rec else None
    if a.md and text:
        pathlib.Path(a.md).write_text(text + "\n")
    sc = m["site_counts"]
    print(f"sites: {sc}; statuses RE {m['statuses_re']} PRE {m['statuses_pre']} both {m['statuses_both']} identical-by-structure {m['statuses_identical']}")
    if rec:
        print(f"records: {rec}")


if __name__ == "__main__":
    main()
