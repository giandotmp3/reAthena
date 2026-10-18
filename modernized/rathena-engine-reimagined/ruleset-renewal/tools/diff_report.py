#!/usr/bin/env python3
"""Architecture-proof report: how much of the RENEWAL weapon pipeline is the same code as the PRE-RENEWAL one.

Reads (never writes) ../ruleset-prerenewal and this package, and writes
  docs/PIPELINE_DIFF.md     human-readable report (all numbers below are produced here, nothing is typed in by hand)
  docs/pipeline_diff.json   the same numbers, for tests/test_ruleset_renewal.py

Measures
  1. lines: per file pair and aggregate (code lines = non-blank, non-comment lines; shared = lines in difflib matching blocks)
  2. functions: same-named functions of scripts/damage/weapon.luau: identical / constants-only / logic differs / RE-only / PRE-only
  3. stages: each stage of either order classified data-only (same code, other constants or tables) / override (other logic) /
     new in RE / removed from PRE, plus the stage ORDER difference (common stages, inversions) and the pipeline.define/insert_after/
     replace/cancel operations that turn the PRE order into the RE order (simulated here, result checked against the RE order)
  4. mechanism audit: engine APIs the RE package calls, cross-ruleset references, files outside ruleset-renewal changed in git
  5. data: element cells and size rows that differ between the two databases

usage: diff_report.py [--check]      (--check: exit 1 if docs/pipeline_diff.json is out of date)
"""
import difflib, json, pathlib, re, subprocess, sys

RE = pathlib.Path(__file__).resolve().parent.parent
PRE = RE.parent / "ruleset-prerenewal"
DOCS = RE / "docs"

# ---------------------------------------------------------------------------------------------------------------------
# helpers
# ---------------------------------------------------------------------------------------------------------------------
def strip_comment(line):
    out, q = [], None
    i = 0
    while i < len(line):
        c = line[i]
        if q:
            out.append(c)
            if c == "\\":
                i += 1
                if i < len(line): out.append(line[i])
            elif c == q:
                q = None
        elif c in "\"'":
            q = c
            out.append(c)
        elif line.startswith("--", i):
            break
        else:
            out.append(c)
        i += 1
    return "".join(out)


def code_lines(text, comment="--"):
    lines = []
    for raw in text.splitlines():
        s = strip_comment(raw) if comment == "--" else raw.split("#", 1)[0] if comment == "#" else raw
        if comment == "//":
            s = raw.split("//", 1)[0]
        s = s.strip()
        if s and not s.startswith("--!"):
            lines.append(s)
    return lines


def shared_count(a, b):
    sm = difflib.SequenceMatcher(None, a, b, autojunk=False)
    return sum(m.size for m in sm.get_matching_blocks())


def mask_numbers(s):
    return re.sub(r"(?<![A-Za-z_])-?\d+(\.\d+)?", "N", s)


FUNC_RE = re.compile(r"^(?:local\s+)?function\s+([A-Za-z_][\w.:]*)\s*\(")


def functions(text):
    """top-level functions: name -> normalized body (comments and blank lines removed)."""
    out, cur, name = {}, None, None
    for raw in text.splitlines():
        m = FUNC_RE.match(raw)
        if m and cur is None:
            name, cur = m.group(1), [raw]
            if raw.rstrip().endswith("end") and raw.count("function") == 1 and raw.strip().endswith("end"):
                # one-liner
                out[name] = [strip_comment(raw).strip()]
                cur = None
            continue
        if cur is not None:
            cur.append(raw)
            if raw.startswith("end"):
                out[name] = [l for l in (strip_comment(x).strip() for x in cur) if l]
                cur = None
    return out


def read(p):
    return p.read_text() if p.exists() else None


# ---------------------------------------------------------------------------------------------------------------------
# 1. files
# ---------------------------------------------------------------------------------------------------------------------
FILE_GROUPS = {
    "pipeline code": [
        ("scripts/damage/weapon.luau", "scripts/damage/weapon.luau"),
        ("scripts/damage/consts.luau", "scripts/damage/consts.luau"),
        ("scripts/damage/unit.luau", "scripts/damage/unit.luau"),
        ("scripts/pipeline.luau", "scripts/pipeline.luau"),
        ("scripts/init.luau", "scripts/init.luau"),
        (None, "scripts/damage/fp.luau"),
        (None, "scripts/formulas.luau"),
    ],
    "data": [
        ("data/damage_cfg.luau", "data/damage_cfg.luau"),
        ("data/damage_tables.luau", "data/damage_tables.luau"),
    ],
    "test support": [
        ("tests/support/fake_ctx.luau", "tests/support/fake_ctx.luau"),
        ("tests/support/scripted_rng.luau", "tests/support/scripted_rng.luau"),
        ("tests/support/harness.luau", "tests/support/harness.luau"),
        ("tests/support/fake_host.luau", "tests/support/fake_host.luau"),
        ("tests/support/golden_runner.luau", "tests/support/golden_runner.luau"),
        ("tests/acceptance/weapon_golden_test.luau", "tests/acceptance/weapon_golden_test.luau"),
        ("tests/bench_weapon.luau", "tests/bench_weapon.luau"),
    ],
    "tools": [
        ("tools/bundle_luau.py", "tools/bundle_luau.py"),
        ("tools/build_golden.py", "tools/build_golden.py"),
        ("tools/lint_numeric.py", "tools/lint_numeric.py"),
        ("tools/run_tests.py", "tools/run_tests.py"),
    ],
    "golden generator": [
        ("conformance/golden_gen/golden_stage.hpp", "conformance/golden_gen/golden_stage.hpp"),
        ("conformance/golden_gen/golden_hook.inc", "conformance/golden_gen/golden_hook.inc"),
        ("conformance/golden_gen/apply_edits.py", "conformance/golden_gen/apply_edits.py"),
        ("conformance/golden_gen/gen_cases.py", "conformance/golden_gen/gen_cases.py"),
        ("conformance/golden_gen/gen_tables.py", "conformance/golden_gen/gen_tables.py"),
        ("conformance/golden_gen/build.sh", "conformance/golden_gen/build.sh"),
    ],
}


def comment_style(path):
    if path.endswith((".py", ".sh")): return "#"
    if path.endswith((".hpp", ".inc")): return "//"
    return "--"


def file_rows():
    groups = {}
    for g, pairs in FILE_GROUPS.items():
        rows = []
        for pre_rel, re_rel in pairs:
            ret = read(RE / re_rel)
            pret = read(PRE / pre_rel) if pre_rel else None
            style = comment_style(re_rel)
            rl = code_lines(ret, style) if ret else []
            pl = code_lines(pret, style) if pret else []
            sh = shared_count(pl, rl) if pret and ret else 0
            shm = shared_count([mask_numbers(x) for x in pl], [mask_numbers(x) for x in rl]) if pret and ret else 0
            if pret is None:
                kind = "RE only"
            elif ret is None:
                kind = "PRE only"
            elif pret == ret:
                kind = "byte-identical"
            elif pl == rl:
                kind = "identical code (comments differ)"
            else:
                kind = "modified"
            rows.append({"file": re_rel, "kind": kind, "pre_lines": len(pl), "re_lines": len(rl), "shared": sh, "shared_masked": shm})
        groups[g] = rows
    return groups


# ---------------------------------------------------------------------------------------------------------------------
# 2./3. functions and stages
# ---------------------------------------------------------------------------------------------------------------------
def classify(pre_body, re_body):
    if pre_body == re_body:
        return "identical"
    if [mask_numbers(l) for l in pre_body] == [mask_numbers(l) for l in re_body]:
        return "constants-only"
    return "logic differs"


PRE_STAGE_FUNCS = {
    "init": ["M.lucky_dodge", "M.crit_roll", "M.hit_roll", "M.crowd_penalty", "M.weapon_element"],
    "base_attack": ["M.base_range", "M.base_damage", "M.base_rate_pct"],
    "skill_ratio": ["M.skill_ratio", "M.atk_percent"],
    "const_add": [],
    "skill_atk_bonus": [],
    "defense": ["M.vit_def", "M.def_reduce", "defense_stage"],
    "post_defense": ["M.post_defense", "post_defense_stage"],
    "element": ["M.attr_fix", "M.element_damage", "M.recover_cap", "element_stage"],
    "element_bonus": [],
    "card_attacker": ["M.cardfix_attacker"],
    "card_target": ["M.cardfix_target"],
    "div_hands": ["M.dual_wield"],
    "target_modifiers": ["M.target_modifiers"],
    "map_rates": ["M.gvg_damage"],
    "final": ["M.finish"],
}
RE_STAGE_FUNCS = {
    "init": ["M.lucky_dodge", "M.crit_roll", "M.hit_roll", "M.crowd_penalty", "M.weapon_element"],
    "base_attack": ["M.weapon_atk_range", "M.weapon_atk_part", "M.mob_base_damage", "STAGES.base_attack"],
    "card_attacker": ["M.cardfix_attacker"],
    "card_target_parts": ["STAGES.card_target_parts"],
    "sc_bonus": ["STAGES.sc_bonus"],
    "sum": ["STAGES.sum"],
    "p_atk": ["STAGES.p_atk"],
    "mastery": ["M.mastery_atk", "STAGES.mastery"],
    "crit_rate": ["crit_rate_pc", "STAGES.crit_rate"],
    "skill_ratio": ["M.skill_ratio", "M.atk_percent"],
    "const_add": [],
    "adv_katar_mastery": [],
    "res": ["M.res_reduction", "res_stage_value", "STAGES.res"],
    "defense": ["M.defense_values", "M.def_reduce", "STAGES.defense"],
    "post_defense": ["STAGES.post_defense"],
    "skill_atk_bonus": [],
    "element": ["M.attr_fix", "STAGES.element"],
    "crit_multiplier": ["STAGES.crit_multiplier"],
    "flat_additions": [],
    "card_target": ["M.cardfix_target"],
    "div_hands": ["M.dual_wield"],
    "target_modifiers": ["M.target_modifiers"],
    "map_rates": ["M.gvg_damage"],
    "final": ["M.finish"],
}
# PRE function that is the counterpart of an RE function under another name (the RE code is a rewrite of the same role)
RENAMED = {"M.defense_values": "M.vit_def", "M.mob_base_damage": "M.base_damage", "M.weapon_atk_part": "M.base_damage",
           "STAGES.element": "element_stage", "STAGES.defense": "defense_stage", "STAGES.post_defense": "post_defense_stage",
           "M.res_reduction": None}


def order_of(text, kind="weapon"):
    m = re.search(kind + r"\s*=\s*\{(.*?)\}", text, re.S)
    return re.findall(r'"([a-z_]+)"', strip_comment_block(m.group(1)))


def strip_comment_block(s):
    return "\n".join(strip_comment(l) for l in s.splitlines())


def stage_report(pre_funcs, re_funcs, pre_order, re_order):
    rows = []
    for stage in re_order:
        re_names = RE_STAGE_FUNCS.get(stage, [])
        pre_names = PRE_STAGE_FUNCS.get(stage)
        if stage not in pre_order:
            rows.append({"stage": stage, "class": "new in RE", "re_functions": re_names, "pre_functions": [], "similarity": None})
            continue
        # compare the shared pure functions (same name in both files) first; stage glue (STAGES.*) is RE-only structure
        shared = [n for n in re_names if n in pre_funcs and n in re_funcs and not n.startswith("STAGES.")]
        pre_body = [l for n in (pre_names or []) if n in pre_funcs for l in pre_funcs[n]]
        re_body = [l for n in re_names if n in re_funcs for l in re_funcs[n]]
        if not pre_body and not re_body:
            cls = "empty in both (no basic-attack logic)"
            sim = None
        else:
            # strip RE-only glue to make the comparison like for like when every RE function has a PRE counterpart of the same name
            if all((n in pre_funcs) for n in re_names if not n.startswith("STAGES.")) and not any(n.startswith("STAGES.") for n in re_names):
                cls = classify(pre_body, re_body)
            else:
                cls = "logic differs"
                if [mask_numbers(l) for l in pre_body] == [mask_numbers(l) for l in re_body]:
                    cls = "constants-only"
            sim = round(difflib.SequenceMatcher(None, pre_body, re_body, autojunk=False).ratio(), 3)
        rows.append({"stage": stage, "class": cls, "re_functions": re_names, "pre_functions": pre_names or [], "similarity": sim})
    removed = [s for s in pre_order if s not in re_order]
    return rows, removed


def function_report(pre_funcs, re_funcs):
    rows = []
    for n in sorted(set(pre_funcs) | set(re_funcs)):
        if n in pre_funcs and n in re_funcs:
            rows.append({"name": n, "class": classify(pre_funcs[n], re_funcs[n]), "pre_lines": len(pre_funcs[n]), "re_lines": len(re_funcs[n]),
                         "similarity": round(difflib.SequenceMatcher(None, pre_funcs[n], re_funcs[n], autojunk=False).ratio(), 3)})
        elif n in re_funcs:
            rows.append({"name": n, "class": "RE only", "pre_lines": 0, "re_lines": len(re_funcs[n]), "similarity": None})
        else:
            rows.append({"name": n, "class": "PRE only", "pre_lines": len(pre_funcs[n]), "re_lines": 0, "similarity": None})
    return rows


# ---------------------------------------------------------------------------------------------------------------------
# order and API operations
# ---------------------------------------------------------------------------------------------------------------------
def lcs(a, b):
    sm = difflib.SequenceMatcher(None, a, b, autojunk=False)
    return [a[i + k] for i, _, n in sm.get_matching_blocks() for k in range(n)]


def inversions(pre, re_):
    common = [s for s in pre if s in re_]
    pos = {s: i for i, s in enumerate(re_)}
    inv = [(common[i], common[j]) for i in range(len(common)) for j in range(i + 1, len(common)) if pos[common[i]] > pos[common[j]]]
    return common, inv


class FakePipeline:
    """Semantics of the architecture 6.4 API (define / insert_after / replace / cancel). An id is a stage name; cancel marks a stage skipped."""
    def __init__(self):
        self.order, self.skipped, self.body, self.calls = [], set(), {}, []
    def define(self, kind, order):
        self.order, self.skipped = list(order), set(); self.calls.append("define")
    def insert_after(self, kind, anchor, new_id):
        i = self.order.index(anchor); self.order.insert(i + 1, new_id); self.calls.append("insert_after")
    def replace(self, kind, stage, body_id):
        self.body[stage] = body_id; self.calls.append("replace")
    def cancel(self, kind, stage):
        self.skipped.add(stage); self.calls.append("cancel")
    def effective(self):
        return [s for s in self.order if s not in self.skipped]


def api_ops(pre_order, re_order, stage_classes):
    """Turn the PRE order into the RE order using only insert_after / cancel / replace. Returns the operations (the check is exact)."""
    keep = lcs(pre_order, re_order)
    ops, p = [], FakePipeline()
    p.define("weapon", pre_order)
    # 1. PRE stages that are not in the longest common subsequence are cancelled (removed or moved)
    for s in pre_order:
        if s not in keep:
            ops.append(("cancel", s)); p.cancel("weapon", s)
    # 2. RE stages that are not in the LCS are inserted after their RE predecessor (a moved stage comes back under a new id)
    for i, s in enumerate(re_order):
        if s in keep: continue
        anchor = None
        for j in range(i - 1, -1, -1):
            cand = re_order[j]
            if cand in keep or ("ruleset.renewal:" + cand) in p.order:
                anchor = cand if cand in keep else "ruleset.renewal:" + cand
                break
        new_id = "ruleset.renewal:" + s
        if anchor is None:
            # first stage of the order: insert after the init stage that always leads both orders
            anchor = re_order[0]
        ops.append(("insert_after", anchor, new_id)); p.insert_after("weapon", anchor, new_id)
    # 3. kept stages whose logic differs get a replaced body
    for s in keep:
        if stage_classes.get(s) == "logic differs":
            ops.append(("replace", s)); p.replace("weapon", s, "ruleset.renewal:" + s)
    eff = [s.replace("ruleset.renewal:", "") for s in p.effective()]
    return ops, eff == re_order, eff


# ---------------------------------------------------------------------------------------------------------------------
# data
# ---------------------------------------------------------------------------------------------------------------------
def table_cells(text):
    el, sizes = {}, {}
    section, cur_lv = None, None
    for line in text.splitlines():
        if line.strip().startswith("--"):
            continue
        if re.match(r"\s*element_pct = \{", line): section = "element"; continue
        if re.match(r"\s*size_pct = \{", line): section = "size"; continue
        if section == "element":
            m = re.match(r"\s*\[(\d+)\] = \{\s*$", line)
            if m: cur_lv = int(m.group(1)); continue
            m = re.match(r"\s*\[(\d+)\] = \{(.*)\},\s*$", line)
            if m and cur_lv is not None:
                for d, v in re.findall(r"\[(\d+)\] = (-?\d+)", m.group(2)):
                    el[(cur_lv, int(m.group(1)), int(d))] = int(v)
        elif section == "size":
            m = re.match(r"\s*\[(\d+)\] = \{ (\d+), (\d+), (\d+) \}", line)
            if m: sizes[int(m.group(1))] = (int(m.group(2)), int(m.group(3)), int(m.group(4)))
    return el, sizes


def data_report():
    pe, ps = table_cells(read(PRE / "data/damage_tables.luau"))
    re_, rs = table_cells(read(RE / "data/damage_tables.luau"))
    diff_cells = sum(1 for k in re_ if pe.get(k) != re_[k])
    diff_sizes = sum(1 for k in rs if ps.get(k) != rs[k])
    cfg_pre = dict(re.findall(r"(\w+) = (-?\d+)", read(PRE / "data/damage_cfg.luau")))
    cfg_re = dict(re.findall(r"(\w+) = (-?\d+)", read(RE / "data/damage_cfg.luau")))
    changed = sorted(k for k in cfg_re if cfg_pre.get(k) != cfg_re[k])
    return {"element_cells": len(re_), "element_cells_differ": diff_cells, "size_rows": len(rs), "size_rows_differ": diff_sizes,
            "cfg_keys": len(cfg_re), "cfg_keys_differ_or_new": changed}


# ---------------------------------------------------------------------------------------------------------------------
# mechanism audit
# ---------------------------------------------------------------------------------------------------------------------
def audit():
    scripts = [p for p in (RE / "scripts").rglob("*.luau")]
    engine_calls = sorted({m for p in scripts for m in re.findall(r"\bengine\.([A-Za-z_.]+)\s*\(", p.read_text())})
    cross = sorted({str(p.relative_to(RE)) for p in list((RE / "scripts").rglob("*.luau")) + list((RE / "data").rglob("*.luau"))
                    if re.search(r"prerenewal|ruleset-prerenewal|ruleset\.prerenewal", strip_comment_block(p.read_text()))})
    imports = sorted({m for p in scripts for m in re.findall(r'import\("([^"]+)"\)', p.read_text())})
    # the mechanism the package sits on: engine-core, script-host, native-defaults, rathena.base (mods/base), the PRE package
    watched = ["engine-core", "script-host", "native-defaults", "mods/base", "ruleset-prerenewal"]
    try:
        root = subprocess.run(["git", "rev-parse", "--show-toplevel"], cwd=RE, capture_output=True, text=True, check=True).stdout.strip()
        base = RE.parent.relative_to(root)
        st = subprocess.run(["git", "status", "--porcelain", "--untracked-files=all", "--"] + [str(base / w) for w in watched],
                            cwd=root, capture_output=True, text=True, check=True).stdout.splitlines()
        outside = [l[3:] for l in st]
    except Exception as e:  # not a git checkout
        outside = [f"(git unavailable: {e})"]
    return {"engine_api_calls": engine_calls, "cross_ruleset_references": cross, "imports": imports, "changed_outside_package": outside}


# ---------------------------------------------------------------------------------------------------------------------
def main():
    pre_w, re_w = read(PRE / "scripts/damage/weapon.luau"), read(RE / "scripts/damage/weapon.luau")
    pre_f, re_f = functions(pre_w), functions(re_w)
    pre_order = order_of(read(PRE / "scripts/pipeline.luau"))
    re_order = order_of(read(RE / "scripts/pipeline.luau"))
    groups = file_rows()
    stages, removed = stage_report(pre_f, re_f, pre_order, re_order)
    funcs = function_report(pre_f, re_f)
    common, inv = inversions(pre_order, re_order)
    classes = {r["stage"]: r["class"] for r in stages}
    ops, ok, eff = api_ops(pre_order, re_order, classes)
    data = data_report()
    au = audit()

    def agg(rows):
        return {"pre_lines": sum(r["pre_lines"] for r in rows), "re_lines": sum(r["re_lines"] for r in rows), "shared": sum(r["shared"] for r in rows),
                "shared_masked": sum(r["shared_masked"] for r in rows), "files": len(rows), "byte_identical": sum(1 for r in rows if r["kind"] == "byte-identical"),
                "identical_code": sum(1 for r in rows if r["kind"] in ("byte-identical", "identical code (comments differ)")),
                "modified": sum(1 for r in rows if r["kind"] == "modified"), "re_only": sum(1 for r in rows if r["kind"] == "RE only")}
    aggs = {g: agg(r) for g, r in groups.items()}
    core = aggs["pipeline code"]
    w = [r for r in groups["pipeline code"] if r["file"] == "scripts/damage/weapon.luau"][0]
    counts = {}
    for r in stages: counts[r["class"]] = counts.get(r["class"], 0) + 1
    fcount = {}
    for r in funcs: fcount[r["class"]] = fcount.get(r["class"], 0) + 1
    flines = {}
    for x in funcs:
        flines[x["class"]] = flines.get(x["class"], 0) + (x["re_lines"] if x["class"] != "PRE only" else x["pre_lines"])
    result = {
        "function_lines": flines,
        "files": groups, "aggregate": aggs, "weapon_luau": w, "stages": stages, "stages_removed_from_pre": removed, "stage_class_counts": counts,
        "functions": funcs, "function_class_counts": fcount,
        "order": {"pre": pre_order, "re": re_order, "common": len(common), "pre_only": removed, "re_only": [s for s in re_order if s not in pre_order],
                  "lcs": len(lcs(pre_order, re_order)), "inversions": [list(x) for x in inv]},
        "api": {"ops": [list(o) for o in ops], "counts": {k: sum(1 for o in ops if o[0] == k) for k in ("cancel", "insert_after", "replace")},
                "define_only": 1, "reproduces_re_order": ok},
        "data": data, "audit": au,
    }
    out_json = DOCS / "pipeline_diff.json"
    new_json = json.dumps(result, indent=1, sort_keys=True) + "\n"
    if "--check" in sys.argv:
        # the architectural numbers must be current; line counts of tests/tools/generator files may drift without failing the check
        keys = ["stages", "stages_removed_from_pre", "stage_class_counts", "functions", "function_class_counts", "function_lines", "order", "api", "data", "audit"]
        cur = json.loads(out_json.read_text()) if out_json.exists() else {}
        new = json.loads(new_json)
        stale = [k for k in keys if cur.get(k) != new.get(k)]
        if cur.get("files", {}).get("pipeline code") != new["files"]["pipeline code"]:
            stale.append("files.pipeline code")
        if stale:
            print("docs/pipeline_diff.json is out of date (" + ", ".join(stale) + "): run tools/diff_report.py")
            return 1
        return 0
    DOCS.mkdir(exist_ok=True)
    out_json.write_text(new_json)
    (DOCS / "PIPELINE_DIFF.md").write_text(render(result))
    print(f"pipeline code: {core['shared']} of {core['re_lines']} RE lines shared with PRE ({100 * core['shared'] // core['re_lines']} %)")
    print("stages:", counts, "| removed from PRE:", removed)
    print("api ops:", result["api"]["counts"], "reproduces RE order:", ok)
    return 0


def pct(a, b):
    return f"{100 * a / b:.0f} %" if b else "-"


def render(r):
    L = ["# Pipeline diff: renewal vs pre-renewal weapon pipeline", "",
         "GENERATED by `tools/diff_report.py` from the two packages as they are in the working tree. Do not edit; rerun the tool.",
         "'Code lines' are non-blank, non-comment lines; 'shared' is the number of lines in difflib matching blocks between the PRE and the RE file.", ""]
    L += ["## 1. Lines and files", "", "| group | files | byte-identical | identical code | modified | RE-only | PRE code lines | RE code lines | shared | share of RE | shared if numbers are masked |", "|---|---|---|---|---|---|---|---|---|---|---|"]
    for g, a in r["aggregate"].items():
        L.append(f"| {g} | {a['files']} | {a['byte_identical']} | {a['identical_code']} | {a['modified']} | {a['re_only']} | {a['pre_lines']} | {a['re_lines']} | {a['shared']} | {pct(a['shared'], a['re_lines'])} | {a['shared_masked']} ({pct(a['shared_masked'], a['re_lines'])}) |")
    L += ["", "| file | kind | PRE | RE | shared | shared if numbers masked |", "|---|---|---|---|---|---|"]
    for g, rows in r["files"].items():
        for x in rows:
            L.append(f"| `{x['file']}` | {x['kind']} | {x['pre_lines']} | {x['re_lines']} | {x['shared']} | {x['shared_masked']} |")
    L += ["", "## 2. Functions of `scripts/damage/weapon.luau`", "", "Same-named top-level functions compared after removing comments and blank lines.", "",
          "| function | class | PRE lines | RE lines | similarity |", "|---|---|---|---|---|"]
    for x in r["functions"]:
        L.append(f"| `{x['name']}` | {x['class']} | {x['pre_lines']} | {x['re_lines']} | {x['similarity'] if x['similarity'] is not None else '-'} |")
    L += ["", "Counts: " + ", ".join(f"{k} {v}" for k, v in sorted(r["function_class_counts"].items())),
          "Lines (RE lines; PRE lines for 'PRE only'): " + ", ".join(f"{k} {v}" for k, v in sorted(r["function_lines"].items())), ""]
    L += ["## 3. Stages", "", "Class per RE stage (the PRE stages are the baseline). 'identical' / 'constants-only' mean the stage's pure functions are the same code "
          "(constants-only: the text differs only in numeric literals); 'logic differs' is a stage override; 'new in RE' is a stage the PRE order does not have. "
          "Stages with no basic-attack logic in either package (const_add, skill_atk_bonus, flat_additions, adv_katar_mastery) are empty for plain attacks.", "",
          "| RE stage | class | RE functions | PRE counterpart | similarity |", "|---|---|---|---|---|"]
    for x in r["stages"]:
        L.append(f"| `{x['stage']}` | {x['class']} | {', '.join('`' + f + '`' for f in x['re_functions']) or '-'} | {', '.join('`' + f + '`' for f in x['pre_functions']) or '-'} | {x['similarity'] if x['similarity'] is not None else '-'} |")
    L += ["", "Counts: " + ", ".join(f"{k} {v}" for k, v in sorted(r["stage_class_counts"].items())) + f". PRE stages with no RE counterpart: {', '.join(r['stages_removed_from_pre']) or 'none'}.", ""]
    o = r["order"]
    L += ["## 4. Stage order", "", f"* PRE order ({len(o['pre'])}): " + " > ".join(o["pre"]), f"* RE order ({len(o['re'])}): " + " > ".join(o["re"]),
          f"* common stages: {o['common']}; longest common subsequence: {o['lcs']}; PRE-only: {', '.join(o['pre_only']) or '-'}; RE-only: {', '.join(o['re_only'])}",
          f"* pairs of common stages in the opposite order ({len(o['inversions'])}): " + ", ".join(f"{a}/{b}" for a, b in o["inversions"]), ""]
    a = r["api"]
    L += ["### pipeline API needed to express RE on top of the PRE stage names", "",
          f"* with `pipeline.define`: **1 call per kind** (the whole RE order); no insert/replace/cancel needed",
          f"* with only the mod-facing edit calls on top of the PRE order (simulated by `FakePipeline`; result equals the RE order: **{a['reproduces_re_order']}**): "
          f"{a['counts']['cancel']} cancel, {a['counts']['insert_after']} insert_after, {a['counts']['replace']} replace", "",
          "| # | call |", "|---|---|"]
    for i, op in enumerate(a["ops"], 1):
        L.append(f"| {i} | `{op[0]}(" + ", ".join(repr(x) for x in op[1:]) + ")` |")
    d = r["data"]
    L += ["", "## 5. Data", "", f"* element table: {d['element_cells_differ']} of {d['element_cells']} cells differ", f"* size table: {d['size_rows_differ']} of {d['size_rows']} weapon rows differ",
          f"* battle config keys that differ or are new in RE ({len(d['cfg_keys_differ_or_new'])} of {d['cfg_keys']}): {', '.join(d['cfg_keys_differ_or_new']) or 'none'}", ""]
    au = r["audit"]
    L += ["## 6. Mechanism audit", "", f"* engine API calls in `scripts/`: {', '.join('`engine.' + c + '`' for c in au['engine_api_calls']) or 'none'}",
          f"* references to the pre-renewal package in `scripts/` or `data/`: {', '.join(au['cross_ruleset_references']) or 'none'}",
          f"* modules imported: {', '.join('`' + i + '`' for i in au['imports'])}",
          f"* files added or modified in the git working tree under the mechanism the package sits on (`engine-core`, `script-host`, `native-defaults`, `mods/base` = rathena.base, `ruleset-prerenewal`): {', '.join('`' + x + '`' for x in au['changed_outside_package']) or 'none'}", ""]
    return "\n".join(L) + "\n"


if __name__ == "__main__":
    sys.exit(main())
