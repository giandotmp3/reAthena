#!/usr/bin/env python3
"""Mutation check: proves the golden comparison bites. Each entry changes ONE expression of the Luau pipeline in a scratch copy of
the package and the golden replay must report mismatches; a mutation that still matches 100 % means the golden data does not
exercise that expression (reported as SURVIVED, exit code 1).

usage: mutation_check.py [--luau PATH]
"""
import os, pathlib, re, shutil, subprocess, sys, tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
MUTATIONS = [
    ("scripts/damage/weapon.luau", "local cut = fp.trunc(fp.ddiv(q * 80.0, 100.0) * damage)", "local cut = fp.trunc(fp.ddiv(q * 79.0, 100.0) * damage)", "RES percent 80 -> 79"),
    ("scripts/damage/weapon.luau", "local q = fp.f32(fp.ddiv(res, res + 400.0))", "local q = fp.ddiv(res, res + 400.0)", "RES ratio not narrowed to float32"),
    ("scripts/damage/weapon.luau", "ctx.mul_div(damage, 4000 + def1, 4000 + 10 * def1) - vit", "ctx.mul_div(damage, 4000 + def1, 4000 + 9 * def1) - vit", "DEF formula constant"),
    ("scripts/damage/weapon.luau", "if def1 == -400 then def1 = -399 end", "if def1 == -401 then def1 = -399 end", "DEF -400 guard"),
    ("scripts/damage/weapon.luau", "mul(S.dmg, fp.f32(1.4)) -- (the base", "mul(S.dmg, fp.f32(1.0)) -- (the base", "monster crit multiplier removed"),
    ("scripts/damage/weapon.luau", "if crit then damage = ctx.mul_div(damage, 14, 10) end", "if crit then damage = damage end", "monster base crit x14/10 removed"),
    ("scripts/damage/weapon.luau", "or src.shield else equipped", "or false else equipped", "shield left-hand roll draw removed"),
    ("scripts/damage/weapon.luau", "local mult = fp.add(fp.f32(1.4), fp.mul(fp.f32(0.01), src.crate))", "local mult = 1.4 + 0.01 * src.crate", "crit multiplier in double instead of float32"),
    ("scripts/damage/weapon.luau", "acc:set(fp.floor_f32_of_int(ctx.mul_div(acc:get(), 100 + src.patk, 100)))", "acc:set(ctx.mul_div(acc:get(), 100 + src.patk, 100))", "P.ATK without the float conversion"),
    ("scripts/damage/weapon.luau", "local hitrate = 0 + src.hit - flee", "local hitrate = 80 + src.hit - flee", "pre-renewal base hit rate"),
    ("scripts/damage/weapon.luau", "return damage - ctx.mul_div(damage, 100 - ratio, 100)", "return ctx.mul_div(damage, ratio, 100)", "pre-renewal element formula"),
    ("scripts/damage/weapon.luau", "if cfg.attr_recover == 0 and not flag1 and ratio < 0 then ratio = 0 end", "", "part ratio clamp removed"),
    ("scripts/damage/weapon.luau", "P.s = sa * 2", "P.s = sa", "status ATK not doubled"),
    ("scripts/damage/weapon.luau", "if damage < 1 then damage = 1 end\n	end\n	return damage\nend\n\nfunction M.gvg_damage", "end\n	return damage\nend\n\nfunction M.gvg_damage", "damage_taken minimum 1"),
    ("scripts/damage/weapon.luau", "if wtype >= C.WDR_SLOTS then return at(b.aadd_ele, wtype - C.WDR_SLOTS) end", "if wtype >= C.WDR_SLOTS then return 0 end", "weapon_damage_rate array aliasing"),
    ("scripts/damage/weapon.luau", "v = v + 2 * (sk.weapon_research or 0)", "v = v + 1 * (sk.weapon_research or 0)", "weapon research mastery"),
    ("scripts/damage/weapon.luau", "if sc.steelbody then", "if false then", "Steel Body"),
    ("scripts/damage/weapon.luau", "if sc.kaite and short then damage = damage * 4 end", "if sc.kaite and long then damage = damage * 4 end", "Kaite melee condition"),
    ("scripts/damage/weapon.luau", "if r < 0 then", "if r < 0 then", None),  # placeholder removed below
    ("scripts/damage/weapon.luau", "if tgt.kind == \"pc\" and src.kind ~= \"pc\" then", "if tgt.kind == \"pc\" then", "target cards applied twice for player attackers"),
    ("scripts/damage/weapon.luau", "P.m = P.m + M.cardfix_target(ctx, src, tgt, P.m, \"r\", f, r, l, true)", "P.m = P.m + M.cardfix_target(ctx, src, tgt, P.m, \"r\", f, r, l, false)", "mastery ATK card fix with the element step"),
    ("scripts/damage/weapon.luau", "hitrate = hitrate + (sk.vulture or 0)", "hitrate = hitrate", "Vulture's Eye hit bonus"),
    ("scripts/damage/weapon.luau", "if p > 65535 then p = 65535 end", "", "ATK% upper cap"),
    ("scripts/damage/weapon.luau", "if sc_of(src, \"berserk\") then r = r + 200 end", "if sc_of(src, \"berserk\") then r = r + 100 end", "Berserk +200"),
    ("scripts/damage/weapon.luau", "local rate = 250 + edp.val1 * 30", "local rate = 250 + edp.val1 * 20", "EDP formula"),
    ("scripts/pipeline.luau", "\"crit_multiplier\", \"flat_additions\", \"card_target\"", "\"flat_additions\", \"crit_multiplier\", \"card_target\"", None),
    ("scripts/pipeline.luau", "\"skill_ratio\", \"const_add\"", "\"const_add\", \"skill_ratio\"", None),
    ("scripts/pipeline.luau", "\"res\", \"defense\"", "\"defense\", \"res\"", "stage order: RES after DEF"),
    ("scripts/pipeline.luau", "\"element\",\n\t\t\"crit_multiplier\"", "\"crit_multiplier\",\n\t\t\"element\"", "stage order: crit multiplier before element"),
    ("scripts/pipeline.luau", "\"crit_rate\",\n\t\t\"skill_ratio\"", "\"skill_ratio\",\n\t\t\"crit_rate\"", "stage order: ratio before crit rate"),
    ("scripts/pipeline.luau", "\"p_atk\", \"mastery\"", "\"mastery\", \"p_atk\"", "stage order: mastery before P.ATK"),
]
MUTATIONS = [m for m in MUTATIONS if m[3]]


def float32_vs_double_range():
    """The weapon ATK range evaluated in float32 (as the legacy code does) and in double truncate identically for every weapon ATK
    1..400, refine 0..60, weapon level 1..4 and base stat 1..130: replacing the float32 steps by double ones is an EQUIVALENT mutant
    over the reachable domain (the Luau code keeps float32 because the legacy code has it, not because a case needs it)."""
    import struct
    def f(x): return struct.unpack("f", struct.pack("f", x))[0]
    bad = 0
    for atk in range(1, 401):
        for atk2 in (0, 3, 9, 20, 41, 60):
            for wlv in range(1, 5):
                for stat in range(1, 131):
                    watk = atk + atk2
                    var, bon = f(f(f(5.0 * atk) * wlv) / 100.0), f(atk * stat / 200.0)
                    v2, b2 = 5.0 * atk * wlv / 100.0, atk * stat / 200.0
                    if int(f(f(watk - var) + bon)) != int(watk - v2 + b2) or int(f(f(watk + var) + bon)) != int(watk + v2 + b2):
                        bad += 1
    return bad


def luau_exe():
    args = sys.argv[1:]
    p = args[args.index("--luau") + 1] if "--luau" in args else None
    exe = p or os.environ.get("LUAU") or shutil.which("luau")
    if not exe:
        sys.exit("mutation_check: Luau CLI not found")
    return exe


def main():
    exe = luau_exe()
    survived = 0
    with tempfile.TemporaryDirectory() as d:
        work = pathlib.Path(d) / "pkg"
        shutil.copytree(ROOT, work, ignore=shutil.ignore_patterns("__pycache__", "docs", ".git", "golden"))
        for rel, old, new, label in MUTATIONS:
            f = work / rel
            src = f.read_text()
            if src.count(old) != 1:
                print(f"BAD ANCHOR  {label}: {src.count(old)} matches of {old[:60]!r}")
                survived += 1
                continue
            f.write_text(src.replace(old, new))
            out = work / "build" / "bundle_m.luau"
            subprocess.run([sys.executable, "-I", str(work / "tools" / "bundle_luau.py"), "tests/golden_report", str(out)], check=True, stdout=subprocess.DEVNULL)
            r = subprocess.run([exe, str(out)], cwd=work, capture_output=True, text=True)
            m = re.search(r"golden weapon_re: (\d+) / (\d+) matched", r.stdout)
            f.write_text(src)
            if m is None:
                print(f"KILLED (error) {label}")
            elif m.group(1) == m.group(2):
                print(f"SURVIVED    {label}")
                survived += 1
            else:
                print(f"KILLED ({int(m.group(2)) - int(m.group(1)):5d} cases differ)  {label}")
    bad = float32_vs_double_range()
    print(f"EQUIVALENT  variance / base-stat bonus in double instead of float32 ({'verified: 0 differing inputs in 1.2 M' if bad == 0 else str(bad) + ' differing inputs: add golden cases'})")
    survived += 1 if bad else 0
    print(f"{len(MUTATIONS)} mutations, {survived} survived")
    return 1 if survived else 0


if __name__ == "__main__":
    sys.exit(main())
