#!/usr/bin/env python3
"""Mutation check of the status layer: proves the status golden comparison bites. Each entry changes ONE expression of scripts/status in a scratch copy of
the package and the replay of conformance/golden/status_re.jsonl.gz must report mismatches; a mutant that still matches 100 % means the golden data does
not exercise that expression (reported as SURVIVED, exit code 1). Companion of tools/mutation_check.py (the basic attack).

usage: mutation_status.py [--luau PATH]
"""
import os, pathlib, re, shutil, subprocess, sys, tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
A, S, N, X = "scripts/status/attrs_re.luau", "scripts/status/status.luau", "scripts/status/num.luau", "scripts/status/aggregate.luau"
MUTATIONS = [
    # ---- the fold: order, stop semantics, clamps ----
    (A, 'if a.at ~= b.at then return a.at < b.at end return a.idx < b.idx end', 'if a.at ~= b.at then return a.at > b.at end return a.idx < b.idx end', "ops applied in descending `at`"),
    (A, 'if stop == "end" then break end', 'if stop == "end" then end', "a stopping `end` op (Harmonize) no longer stops"),
    (A, 'if first_stop(contribs) == "ret" then return acc end', 'if first_stop(contribs) == "ret" then end', "an early return is capped like a normal result"),
    (A, 'pct_sub = function(a, v) return num.i32(a - num.mul_div(a, v, 100)) end', 'pct_sub = function(a, v) return num.i32(a + num.mul_div(a, v, 100)) end', "pct_sub adds"),
    (A, 'scale = function(a, v) return num.mul_div(a, v, 100) end', 'scale = function(a, v) return num.mul_div(a, v, 101) end', "scale divides by 101"),
    (A, 'div_sub = function(a, v) return num.i32(a - num.tdiv(a, v)) end', 'div_sub = function(a, v) return num.i32(a - v) end', "Blessing half becomes a subtraction"),
    (A, 'pct_add_off = function(a, v, arg) return num.i32(a + num.tdiv(num.i32((a - arg) * v), 100)) end', 'pct_add_off = function(a, v, arg) return num.i32(a + num.tdiv(num.i32(a * v), 100)) end', "Improve Concentration ignores the item offset"),
    (A, 'pct_sub2 = function(a, v, arg) return num.i32(a - num.i32(num.mul_div(a, v, 100) + num.mul_div(a, arg, 100))) end', 'pct_sub2 = function(a, v, arg) return num.i32(a - num.mul_div(a, v + arg, 100)) end', "Joint Beat sums the percentages before truncating"),
    (A, 'chain("hit", { lo = 1,', 'chain("hit", { lo = 0,', "HIT floor 1 -> 0"),
    (A, 'chain("cri", { lo = 10,', 'chain("cri", { lo = 9,', "CRI floor 10 -> 9"),
    (A, 'chain("flee2", { lo = 10,', 'chain("flee2", { lo = 0,', "FLEE2 floor 10 -> 0"),
    (A, 'chain("def2", { lo = C.SHRT_MIN,', 'chain("def2", { lo = 1,', "DEF2 floor: the pre-renewal clamp to 1"),
    (A, 'chain("mode", { lo = C.MD_NONE,', 'chain("mode", { lo = 1,', "mode floor 0 -> 1"),
    (A, 'ret = function(_, v) return v, "ret" end,', 'ret = function(_, v) return v, "end" end,', "an early return is capped and later ops stop"),
    # ---- flee / speed ----
    (A, 'elseif F.map == 2 then acc = num.i32(acc - num.mul_div(acc, F.cfg_bg_flee_penalty, 100)) end', 'elseif F.map == 2 then acc = num.i32(acc - num.mul_div(acc, F.cfg_gvg_flee_penalty, 100)) end', "battleground uses the GvG penalty"),
    (A, 'if acc < 40 then acc = 40 end', 'if acc < 30 then acc = 30 end', "speed rate floor 40 -> 30"),
    (A, 'val = 15 + 5 * (F["skill.RA_WUGRIDER"] or 0)', 'val = 10 + 5 * (F["skill.RA_WUGRIDER"] or 0)', "Warg rider speed bonus"),
    (A, 'return 175 - 5 * (F["skill.SA_FREECAST"] or 0)', 'return 170 - 5 * (F["skill.SA_FREECAST"] or 0)', "Free Cast speed rate"),
    (A, 'if lv < 5 then val = 50 - 10 * lv else val = val - 25 end', 'if lv < 5 then val = 50 - 5 * lv else val = val - 25 end', "Madogear license slow"),
    (A, 'if has_op(contribs, "cap_marsh") and acc > 150 then return 150 end', 'if has_op(contribs, "cap_marsh") and acc > 150 then return 140 end', "Marsh of Abyss cap"),
    (A, 'acc = num.i32(acc + num.mul_div(acc, 50 - 5 * (F["skill.MC_PUSHCART"] or 0), 100))', 'acc = num.i32(acc + num.mul_div(acc, 50 - 4 * (F["skill.MC_PUSHCART"] or 0), 100))', "cart slow per Pushcart level"),
    (A, 'if v ~= nil and v > 0 then acc = num.mul_div(acc, 100, v) end', 'if v ~= nil and v >= 0 then acc = num.mul_div(acc, 100, v + 1) end', "Walk Speed divisor"),
    (A, 'if F.is_pc == 1 and item > 0 then val = num.max(val, item) end', 'if F.is_pc == 1 and item > 0 then val = num.max(val, item + 1) end', "item-based slow (bonus_speed_rate > 0)"),
    (A, 'if F.is_pc == 1 and item < 0 then val = num.max(val, -item) end', 'if F.is_pc == 1 and item < 0 then val = num.max(val, -item + 1) end', "item-based haste (bonus_speed_rate < 0)"),
    # ---- ASPD ----
    (A, 'if has_op(contribs, "quicken7") then return 7 end', 'if has_op(contribs, "quicken7") then return 6 end', "quicken tier 7 -> 6"),
    (A, 'if acc < 20 and has_op(contribs, "madness") then return 20 end', 'if acc < 20 and has_op(contribs, "madness") then return 19 end', "Madness Cancel 20 -> 19"),
    (A, 'if best == nil or tonumber(c.arg) < tonumber(best.arg) then best = c end\n\t\t\tend\n\t\t\tif best then acc = acc + best.value end', 'if best == nil or tonumber(c.arg) > tonumber(best.arg) then best = c end\n\t\t\tend\n\t\t\tif best then acc = acc + best.value end', "ASPD potion: the weakest one applies (fixed group)"),
    (A, 'if best == nil or tonumber(c.arg) < tonumber(best.arg) then best = c end\n\t\t\tend\n\t\t\tif best then acc = acc - best.value end', 'if best == nil or tonumber(c.arg) > tonumber(best.arg) then best = c end\n\t\t\tend\n\t\t\tif best then acc = acc - best.value end', "ASPD potion: the weakest one applies (rate)"),
    (A, 'return acc - m\n\t\tend },', 'return acc + m\n\t\tend },', "aspd_rate: the max bonus is added"),
    (A, 'if F.is_pc == 1 and F.sc_empty == 0 then', 'if F.is_pc == 1 then', "ASPD skills count without any status"),
    # ---- maximum HP / SP ----
    (A, 'v = u64.add_i32(v, fix)', 'v = u64.add_i32(v, num.max(fix, -acc))', "maxhp: no uint64 wrap-around"),
    (A, 'if r ~= 100 then v = u64.mul_div_small(v, r, 100) end', 'if r ~= 100 then v = u64.mul_div_small(v, r, 101) end', "maxhp rate divisor"),
    (A, 'return num.cap(acc, -100, C.INT_MAX) end', 'return num.cap(acc, -99, C.INT_MAX) end', "HP rate floor -100 -> -99"),
    (A, 'if lv > 0 then acc = acc + 350 * lv + (lv > 4 and 250 or 0) end\n\t\tlv = sk(F, "NV_TRANSCENDENCE")', 'if lv > 0 then acc = acc + 350 * lv + (lv > 3 and 250 or 0) end\n\t\tlv = sk(F, "NV_TRANSCENDENCE")', "Breakthrough HP threshold"),
    (A, 'local lesson = F.sex ~= 0 and sk(F, "BA_MUSICALLESSON") or sk(F, "DC_DANCINGLESSON")', 'local lesson = F.sex == 0 and sk(F, "BA_MUSICALLESSON") or sk(F, "DC_DANCINGLESSON")', "SP rate lesson by sex"),
    (A, 'if lv > 0 then acc = acc + 15 * lv + (lv > 4 and 25 or 0) end', 'if lv > 0 then acc = acc + 15 * lv + (lv > 4 and 20 or 0) end', "Transcendence flat MATK"),
    # ---- aggregation / facts ----
    (X, 'if cur == nil or (cand.c.rank ~= nil and (cur.c.rank == nil or cand.c.rank < cur.c.rank)) then chosen[g] = cand end', 'if cur == nil then chosen[g] = cand end', "group ranks ignored (the first member wins)"),
    (X, 'if unit.nosc and not attr.reads_unit_sc and not F then active = {} end', 'if false then active = {} end', "sc == nullptr still applies the statuses"),
    (X, 'if c.when == nil or c.when(vals, F) ~= 0 then', 'if true then', "conditions ignored"),
    (X, 'F.sc_empty = n == 0 and 1 or 0', 'F.sc_empty = 0', "sc_empty always false"),
    # ---- numerics ----
    (N, 'if (a < 0) ~= (b < 0) and q ~= 0 then return -q end', 'if false then return -q end', "division rounds toward zero only for positives (floors negatives)"),
    (N, 'function M.wrap_i16(v) return (v + 32768) % 65536 - 32768 end', 'function M.wrap_i16(v) return v end', "no int16 narrowing"),
    (N, 'function M.shr(a, n) return to_signed(bit32.arshift(a, n)) end', 'function M.shr(a, n) return to_signed(bit32.rshift(a, n)) end', "logical instead of arithmetic shift"),
    # ---- definitions (status.cpp data) ----
    ("scripts/status/defs/a_c.luau", 'hit = { "add", "level * 2", at = 7605 },', 'hit = { "add", "level * 3", at = 7605 },', "Blessing hit bonus"),
    ("scripts/status/defs/d_i.luau", 'hp_fix = { "add", "500", when = "insignia_type == 2", at = 3201 },', 'hp_fix = { "add", "400", when = "insignia_type == 2", at = 3201 },', "Earth Insignia HP"),
    ("scripts/status/defs/i_r.luau", '{ "add", "250", when = "(broken & BREAK_WRIST) != 0", at = 8523 },', '{ "add", "200", when = "(broken & BREAK_WRIST) != 0", at = 8523 },', "Joint Beat ASPD rate"),
    ("scripts/status/defs/i_r.luau", 'def = { "pct_sub", "def_pct", when = "u.is_pc == 0", at = 7825 },', 'def = { "pct_sub", "def_pct", at = 7825 },', "Provoke lowers a player's DEF"),
    ("scripts/status/defs/s_z.luau", 'luk = { "pct_sub", "pct", when = "u.is_pc == 0", at = 7164 },', 'luk = { "pct_sub", "pct", at = 7164 },', "Strip accessory on players"),
    ("scripts/status/defs/a_c.luau", 'cri = { "pct_add", "100", at = 7536 }, -- critical += critical', 'cri = { "pct_add", "100", at = 7500 }, -- critical += critical', "Cloaking doubles before the additive terms"),
    ("scripts/status/states.luau", 'local PLAYER_ONLY = { pickup = true, drop = true, equip = true, unequip = true, consume = true, deathpenalty = true, interact = true }', 'local PLAYER_ONLY = { pickup = true, drop = true, equip = true, unequip = true, deathpenalty = true, interact = true }', "consume restriction applies to monsters"),
    ("scripts/status/defs/states.luau", 'crystalize", { state_when = { nomove = "u.is_mob == 0" } }', 'crystalize", { state_when = { nomove = "u.is_mob == 1" } }', "Crystallize restriction inverted"),
]


def luau_exe():
    args = sys.argv[1:]
    p = args[args.index("--luau") + 1] if "--luau" in args else None
    exe = p or os.environ.get("LUAU") or shutil.which("luau")
    if not exe:
        sys.exit("mutation_status: Luau CLI not found")
    return exe


def main():
    exe = luau_exe()
    survived = unit_killed = 0
    subprocess.run([sys.executable, "-I", str(ROOT / "tools" / "build_golden.py")], check=True, stdout=subprocess.DEVNULL)
    with tempfile.TemporaryDirectory() as d:
        work = pathlib.Path(d) / "pkg"
        shutil.copytree(ROOT, work, ignore=shutil.ignore_patterns("__pycache__", "docs", ".git", "golden", "bundle_*.luau", "golden_weapon_re.luau"))
        for rel, old, new, label in MUTATIONS:
            f = work / rel
            src = f.read_text()
            if src.count(old) != 1:
                print(f"BAD ANCHOR  {label}: {src.count(old)} matches of {old[:70]!r}")
                survived += 1
                continue
            f.write_text(src.replace(old, new))
            out = work / "build" / "bundle_m.luau"
            subprocess.run([sys.executable, "-I", str(work / "tools" / "bundle_luau.py"), "tests/status_report", str(out)], check=True, stdout=subprocess.DEVNULL)
            r = subprocess.run([exe, str(out)], cwd=work, capture_output=True, text=True)
            m = re.search(r"status golden: (\d+) / (\d+) cases matched", r.stdout)
            f.write_text(src)
            if m is None:
                print(f"KILLED (error) {label}")
            elif m.group(1) == m.group(2):
                # not visible in the golden replay: the unit tests of the package (tests/extra) must catch it, else it is a survivor
                out2 = work / "build" / "bundle_m2.luau"
                subprocess.run([sys.executable, "-I", str(work / "tools" / "bundle_luau.py"), "tests/extra", str(out2)], check=True, stdout=subprocess.DEVNULL)
                r2 = subprocess.run([exe, str(out2)], cwd=work, capture_output=True, text=True)
                m2 = re.search(r"pass=(\d+) xfail=(\d+) fail=(\d+)", r2.stdout)
                if r2.returncode != 0 or (m2 and int(m2.group(3)) > 0):
                    print(f"KILLED (unit test, golden replay equal)  {label}")
                    unit_killed += 1
                else:
                    print(f"SURVIVED    {label}")
                    survived += 1
            else:
                print(f"KILLED ({int(m.group(2)) - int(m.group(1)):5d} cases differ)  {label}")
    print(f"{len(MUTATIONS)} mutations, {len(MUTATIONS) - survived} killed ({unit_killed} of them only by the unit tests, not by the golden replay), {survived} survived")
    return 1 if survived else 0


if __name__ == "__main__":
    sys.exit(main())
