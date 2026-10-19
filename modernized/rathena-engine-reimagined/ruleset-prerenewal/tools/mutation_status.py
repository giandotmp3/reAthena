#!/usr/bin/env python3
"""Mutation check of the status golden comparison: every mutant breaks one detail of the Luau status calculation (a flooring division, a
swapped order, a wrong constant, a dropped clamp, a missing narrowing, ...) in a COPY of the package; the status golden replay
(tests/status_golden_report) must FAIL on it. A mutant that survives would mean the golden cases cannot see that behaviour.

usage: mutation_status.py --luau PATH [--list] [--only NAME]      exit 0 when every mutant is killed
The copy lives in a temporary directory; nothing in the package is touched.
"""
import argparse, os, pathlib, shutil, subprocess, sys, tempfile, time

ROOT = pathlib.Path(__file__).resolve().parent.parent
S = "scripts/status/"

# (name, file, old, new)  -- `old` must occur exactly once in the file
MUTANTS = [
    ("floor-mul_pct", S + "aggregate.luau", "mul_pct = function(e, acc, v) return acc + e.idiv(acc * v, 100) end", "mul_pct = function(e, acc, v) return acc + math.floor(acc * v * 0.01) end"),
    ("floor-scale", S + "aggregate.luau", "scale = function(e, acc, v, v2) return e.idiv(acc * v, v2) end", "scale = function(e, acc, v, v2) return math.floor(acc * v / v2) end"),
    ("floor-div", S + "aggregate.luau", "div = function(e, acc, v) return e.idiv(acc, v) end", "div = function(e, acc, v) return math.floor(acc / v) end"),
    ("floor-pct_over", S + "aggregate.luau", "return acc + e.idiv((acc - v) * v2, 100) end", "return acc + math.floor((acc - v) * v2 / 100) end"),
    ("order-reversed", S + "aggregate.luau", "if a.at ~= b.at then return a.at < b.at end", "if a.at ~= b.at then return a.at > b.at end"),
    ("no-clamp", S + "aggregate.luau", "if cfg.clamp then return ops.cap(x, cfg.clamp[1], cfg.clamp[2]) end", "if false then return ops.cap(x, cfg.clamp[1], cfg.clamp[2]) end"),
    ("final-not-narrowed", S + "aggregate.luau", 'if op == "final" then return ops.narrow(c.v, cfg.ret[1], cfg.ret[2])', 'if op == "final" then return c.v'),
    ("stop-unclamped", S + "aggregate.luau", 'elseif op == "stop" then return clamp(cfg, acc)', 'elseif op == "stop" then return acc'),
    ("override-stage-dropped", S + "aggregate.luau", 'elseif c.stage == "override" or define.TERMINAL[c.op] then over[#over + 1] = c', 'elseif define.TERMINAL[c.op] then over[#over + 1] = c'),
    ("group-min-instead-of-max", S + "aggregate.luau", "if c.v > cur then cur = c.v end", "if c.v < cur then cur = c.v end"),
    ("hit-floor-0", S + "attrs.luau", 'hit = std(1, S16, 16, true, "status_calc_hit")', 'hit = std(0, S16, 16, true, "status_calc_hit")'),
    ("def-range-wide", S + "attrs.luau", 'def = std(K.DEFTYPE_MIN, K.DEFTYPE_MAX, 8, true, "status_calc_def")', 'def = std(-32768, 32767, 16, true, "status_calc_def")'),
    ("def2-allows-0", S + "attrs.luau", 'def2 = std(1, S16, 16, true, "status_calc_def2")', 'def2 = std(0, S16, 16, true, "status_calc_def2")'),
    ("band-is-bor", S + "ops.luau", "function M.band(a, b) return to_i32(band(to_i32(a), to_i32(b))) end", "function M.band(a, b) return to_i32(bor(to_i32(a), to_i32(b))) end"),
    ("blessing-half-rounds-up", S + "defs/defs_a_f.luau", "{ at = 6802, op = \"mul_frac\", when = function(s, e) return not (s.stat_inc ~= 0) end, v = -1, v2 = 2 }", "{ at = 6802, op = \"scale\", when = function(s, e) return not (s.stat_inc ~= 0) end, v = 1, v2 = 2 }"),
    ("steelbody-def-80", S + "defs/defs_s_z.luau", '{ at = 7784, op = "final", v = 90 }', '{ at = 7784, op = "final", v = 80 }'),
    ("berserk-def2-1", S + "defs/defs_a_f.luau", '{ at = 7888, op = "final", v = 0 }', '{ at = 7888, op = "final", v = 1 }'),
    ("harmonize-no-override-stage", S + "defs/defs_g_l.luau", '{ at = 6779, op = "add", stage = "override", v = function(s, e) return -s.stat_dec end }', '{ at = 6779, op = "add", v = function(s, e) return -s.stat_dec end }'),
    ("speed-floor-41", S + "speed.luau", "if speed_rate < 40 then speed_rate = 40 end", "if speed_rate < 40 then speed_rate = 41 end"),
    ("speed-marsh-cap-151", S + "speed.luau", "speed_rate > 150 then speed_rate = 150 end", "speed_rate > 150 then speed_rate = 151 end"),
    ("speed-slow-force-ignored", S + "speed.luau", "local val = g.slow_force", "local val = nil"),
    ("speed-mount-ignored", S + "speed.luau", "speed_rate = speed_rate - (g.mount or 0)", "speed_rate = speed_rate - 0"),
    ("speed-cart-4", S + "defs_hand.luau", "v = function(s, e) return 50 - 5 * e.skill(\"MC_PUSHCART\") end", "v = function(s, e) return 50 - 4 * e.skill(\"MC_PUSHCART\") end"),
    ("speed-defender-min", S + "aggregate.luau", "max = function(e, acc, v) if v > acc then return v end return acc end", "max = function(e, acc, v) if v < acc then return v end return acc end"),
    ("aspd-quicken-added", S + "aspd.luau", "local acc = base - (g.quicken or 0)", "local acc = base + (g.quicken or 0)"),
    ("aspd-potion-order", S + "defs_hand.luau", "for j = 1, i - 1 do", "for j = i + 1, #POTIONS do"),
    ("aspd-assncros-bow", S + "defs_hand.luau", "local RANGED = { [K.W_BOW] = true, ", "local RANGED = { "),
    ("nibelungen-wlv-ge-4", S + "defs_hand.luau", "return wlv == 4 -- 7387", "return wlv >= 4 -- 7387"),
    ("flee-gvg-uses-bg-rate", S + "defs_hand.luau", "v = function(s, e) return -e.cfg.gvg_flee_penalty end", "v = function(s, e) return -e.cfg.bg_flee_penalty end"),
    ("maxhp-wrap-to-1", S + "maxhp.luau", "return UINT_MAX\n\tend", "return 1\n\tend"),
    ("maxhp-rate-floor-99", S + "maxhp.luau", "if kind == \"rate\" and b < -100 then b = -100 end", "if kind == \"rate\" and b < -99 then b = -99 end"),
    ("chain-katar-dropped", S + "chain.luau", "if pc and unit.weapon == K.W_KATAR then st.cri = i16(st.cri * 2) end", "if false then st.cri = i16(st.cri * 2) end"),
    ("chain-batk-delta-sign", S + "chain.luau", "st.batk = st.batk + (b.batk - base_atk(e, b))", "st.batk = st.batk - (b.batk - base_atk(e, b))"),
    ("chain-cri-luk-delta", S + "chain.luau", "b.cri + e.idiv((st.luk - b.luk) * 10, 3)", "b.cri + (st.luk - b.luk) * 3"),
    ("chain-str-not-narrowed", S + "chain.luau", 'if flag.STR then st.str = i16(calc("str", b.str))', 'if flag.STR then st.str = calc("str", b.str)'),
    ("chain-mdef2-vit-half", S + "chain.luau", "+ e.idiv(st.vit - b.vit, 2)))", "+ (st.vit - b.vit)))"),
    ("chain-def2-delta-sign", S + "chain.luau", 'st.def2 = i16(calc("def2", b.def2 + (st.vit - b.vit)))', 'st.def2 = i16(calc("def2", b.def2 + (b.vit - st.vit)))'),
    ("chain-hit-ignores-dex", S + "chain.luau", 'st.hit = i16(calc("hit", b.hit + (st.dex - b.dex)))', 'st.hit = i16(calc("hit", b.hit))'),
    ("chain-baseatk-no-ranged-swap", S + "chain.luau", "if u.type == \"pc\" and RANGED_BASE[u.weapon] then str, dex = st.dex, st.str end", "if false then str, dex = st.dex, st.str end"),
    ("states-empty-not-cleared", S + "states.luau", "if next(e.sc) == nil then", "if false then"),
    ("states-dancing-longing", S + "states.luau", "if e.sc.LONGING ~= nil and song ~= K.CG_MOONLIT and song ~= K.CG_HERMODE then return true, false end", "if false then return true, false end"),
    ("mode-del-before-add", S + "mode.luau", "if mc.mode_add ~= 0 then mode = ops.bor(mode, mc.mode_add) end -- 8903 add mode", "if false then end"),
    ("const-break-knee", S + "consts.luau", "BREAK_KNEE = 4,", "BREAK_KNEE = 2,"),
    ("const-opt-riding", S + "consts.luau", "OPTION_RIDING = 32,", "OPTION_RIDING = 64,"),
    ("raw-valN-dropped", S + "define.luau", 'out["val" .. n] = v', 'out["val" .. n] = 0'),
]


def build(tree, luau):
    r = subprocess.run([sys.executable, "-I", str(tree / "tools" / "bundle_luau.py"), "tests/status_golden_report", str(tree / "build" / "b.luau")],
                       cwd=tree, capture_output=True, text=True)
    if r.returncode != 0:
        return None, r.stderr
    r = subprocess.run([luau, str(tree / "build" / "b.luau")], cwd=tree, capture_output=True, text=True, timeout=300)
    return r, None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--luau", required=True)
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--only")
    a = ap.parse_args()
    muts = [m for m in MUTANTS if m[2] is not None]
    if a.list:
        for m in muts: print(m[0], m[1])
        return 0
    tmp = pathlib.Path(tempfile.mkdtemp(prefix="stmut-"))
    tree = tmp / "pkg"
    for d in ("scripts", "data", "tests", "tools"):
        shutil.copytree(ROOT / d, tree / d, ignore=shutil.ignore_patterns("__pycache__"))
    (tree / "conformance").mkdir()
    for f in ("index.luau", "pending.luau", "required.luau"):
        shutil.copy(ROOT / "conformance" / f, tree / "conformance" / f)
    shutil.copytree(ROOT / "conformance" / "cases", tree / "conformance" / "cases")
    (tree / "build").mkdir()
    shutil.copy(ROOT / "build" / "golden_status_pre.luau", tree / "build" / "golden_status_pre.luau")
    # the baseline must pass
    r, err = build(tree, a.luau)
    if r is None or r.returncode != 0:
        print("baseline FAILS:", err or r.stdout[-500:]); return 2
    killed = survived = 0
    for name, rel, old, new in muts:
        if a.only and a.only != name:
            continue
        f = tree / rel
        orig = f.read_text()
        if orig.count(old) != 1:
            print(f"{name:34s} NOT APPLICABLE ({orig.count(old)} matches of the old text in {rel})"); survived += 1; continue
        f.write_text(orig.replace(old, new))
        t0 = time.time()
        r, err = build(tree, a.luau)
        f.write_text(orig)
        if r is None:
            print(f"{name:34s} killed (does not compile: {err.strip()[:60]})"); killed += 1; continue
        out = r.stdout + r.stderr
        line = [l for l in out.split("\n") if l.startswith("golden status_pre")]
        if r.returncode != 0:
            print(f"{name:34s} killed   {line[0] if line else out.strip().split(chr(10))[-1][:90]}"); killed += 1
        else:
            print(f"{name:34s} SURVIVED"); survived += 1
    print(f"\n{killed} mutants killed, {survived} survived or not applicable (of {killed + survived})")
    shutil.rmtree(tmp, ignore_errors=True)
    return 0 if survived == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
