#!/usr/bin/env python3
"""Generates the pre-renewal basic-weapon-attack golden cases and records the REAL legacy result for each.

  gen_cases.py --server <build>/rA [--out conformance/golden/weapon_pre.jsonl.gz] [--scale 1.0]

1. builds the case list (deterministic, seed below) in "unit form" (the table shape the Luau pipeline consumes),
2. writes it as `key=value` lines for the golden hook, runs the patched pre-renewal map-server once
   (RA_GOLDEN_CASES / RA_GOLDEN_OUT; see golden_hook.inc), parses the recorded result, every RNG draw and the stage probes,
3. writes one JSON object per case (inputs + legacy output), gzip-compressed with a fixed mtime so reruns are byte-identical.
"""
import argparse, gzip, json, os, random, subprocess, sys, tempfile, pathlib

SEED = 20261018  # default; --seed generates a different (uncommitted) fuzz set

# ---------------------------------------------------------------------------------------------------------------------
# unit form -> hook key=value tokens
# ---------------------------------------------------------------------------------------------------------------------
BONUS_SCALAR = ["arrow_atk", "crit_atk_rate", "non_crit_atk_rate", "short_atk", "long_atk", "near_def", "long_def", "crit_def",
                "crit_def_rate", "overrefine_r", "overrefine_l", "perfect_hit", "arrow_hit", "arrow_cri", "crit_range", "skill_ratio"]
BONUS_MAPS = {"add_race": "add.race", "add_ele": "add.ele", "add_size": "add.size", "add_class": "add.class",
              "ladd_race": "ladd.race", "ladd_ele": "ladd.ele", "ladd_size": "ladd.size",
              "aadd_race": "aadd.race", "aadd_ele": "aadd.ele", "aadd_size": "aadd.size", "aadd_class": "aadd.class",
              "sub_race": "sub.race", "sub_ele": "sub.ele", "sub_size": "sub.size", "sub_class": "sub.class",
              "sub_defele": "sub.defele", "critadd_race": "critadd.race"}


def unit_tokens(p, u):
    t = [f"{p}type={0 if u['kind'] == 'pc' else 1}"]
    for k in ["level", "str", "agi", "vit", "int", "dex", "luk", "batk", "hit", "cri", "flee", "flee2", "def", "def2", "size", "race",
              "ele_lv", "targeted", "wtype1", "wtype2"]:
        if k in u: t.append(f"{p}{k}={u[k]}")
    if "element" in u: t.append(f"{p}ele={u['element']}")
    if u.get("boss"): t.append(f"{p}boss=1")
    if u.get("job") in ("thief", "kagerou"): t.append(f"{p}job={ {'thief': 1, 'kagerou': 2}[u['job']] }")
    for k, v in u.get("skills", {}).items():
        t.append(f"{p}skill.{k}={v}")
    if u.get("arrow"): t.append(f"{p}bow=1")
    if u.get("no_sizefix"): t.append(f"{p}no_sizefix=1")
    for h in ("rhw", "lhw"):
        for k, v in u.get(h, {}).items():
            t.append(f"{p}{h}.{k}={v}")
    b = u.get("bonus", {})
    for k in BONUS_SCALAR:
        if k in b: t.append(f"{p}{k}={b[k]}")
    for k, pref in BONUS_MAPS.items():
        for idx, v in b.get(k, {}).items():
            t.append(f"{p}{pref}.{idx}={v}")
    for name, vals in u.get("sc", {}).items():
        t.append(f"{p}sc.{name}=" + ",".join(str(vals.get(f'val{i}', 0)) for i in range(1, 5)))
    return t


def case_tokens(c):
    t = [f"id={c['id']}", f"seed={c['seed']}", f"map={ {'normal': 0, 'pvp': 1, 'gvg': 2}[c['map']] }"]
    for k, v in c.get("cfg", {}).items():
        t.append(f"cfg.{k}={v}")
    return " ".join(t + unit_tokens("a.", c["a"]) + unit_tokens("t.", c["t"]))


# ---------------------------------------------------------------------------------------------------------------------
# case construction
# ---------------------------------------------------------------------------------------------------------------------
R = random.Random(SEED)
CASES = []


def reseed(seed):
    global R
    R = random.Random(seed)

MELEE_WT = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 13, 14, 15]  # no bow, no katar, no fist
BOW = 11
ONE_HAND_LEFT = [1, 2, 6]


def add(cat, a, t, mp="normal", cfg=None, seed=None):
    CASES.append({"id": len(CASES) + 1, "cat": cat, "seed": seed if seed is not None else R.randrange(1, 2**31),
                  "map": mp, "cfg": cfg or {}, "a": a, "t": t})


def pc(**kw):
    u = {"kind": "pc", "level": R.randint(1, 99), "str": R.randint(1, 120), "agi": R.randint(1, 120), "vit": R.randint(1, 120),
         "int": R.randint(1, 120), "dex": R.randint(1, 150), "luk": R.randint(1, 120), "batk": R.randint(0, 150),
         "hit": R.randint(60, 350), "cri": R.choice([0, 0, 10, 50, 100, 250, 500]), "flee": R.randint(20, 300),
         "def": R.randint(0, 80), "def2": R.randint(0, 120), "size": 1, "race": 7, "element": 0, "ele_lv": 1,
         "wtype1": R.choice(MELEE_WT)}
    wt = u["wtype1"]
    u["rhw"] = {"atk": R.randint(10, 700), "atk2": R.randint(0, 50), "ele": R.randint(0, 9), "wlv": R.randint(1, 4)}
    u.update(kw)
    return u


def mob(**kw):
    atk = R.randint(5, 1500)
    u = {"kind": "mob", "level": R.randint(1, 99), "str": R.randint(1, 120), "agi": R.randint(1, 120), "vit": R.randint(1, 120),
         "int": R.randint(1, 120), "dex": R.randint(1, 150), "luk": R.randint(1, 120), "batk": R.randint(0, 100),
         "hit": R.randint(60, 350), "cri": R.choice([0, 0, 10, 50, 100]), "flee": R.randint(20, 300),
         "def": R.randint(0, 80), "def2": R.randint(0, 120), "size": R.randint(0, 2), "race": R.randint(0, 9),
         "element": R.randint(0, 9), "ele_lv": R.randint(1, 4),
         "rhw": {"atk": atk, "atk2": atk + R.randint(0, 400), "ele": R.randint(0, 9)}}
    u.update(kw)
    return u


def target_pc(**kw):
    u = pc(**kw)
    u["size"] = R.randint(0, 2)
    u["race"] = R.choice([7, 10])
    u["element"] = R.randint(0, 9)
    u["ele_lv"] = R.randint(1, 4)
    u.update(kw)
    return u


def rand_target(**kw):
    return (target_pc if R.random() < 0.4 else mob)(**kw)


def sure_hit(a, t):
    a["hit"] = 400
    t["flee"] = 0
    t.pop("flee2", None)
    a["cri"] = a.get("cri", 0)


def never_crit(a):
    a["cri"] = 0


def small_bonus_map(n, lo, hi, keys):
    return {str(k): R.randint(lo, hi) for k in R.sample(keys, n)}


def random_cards(a, t, tgt_is_pc):
    b = a.setdefault("bonus", {})
    if a["kind"] == "pc":
        b["add_race"] = small_bonus_map(2, -20, 60, [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 12])
        b["add_ele"] = small_bonus_map(2, -20, 60, [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10])
        b["add_size"] = small_bonus_map(1, -20, 50, [0, 1, 2, 3])
        b["add_class"] = small_bonus_map(1, -20, 50, [0, 1, 6])
        b["short_atk"] = R.choice([0, 0, 10, 25])
        b["long_atk"] = R.choice([0, 0, 10, 25])
        if a.get("lhw"):
            b["ladd_race"] = small_bonus_map(2, -10, 40, [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 12])
            b["ladd_ele"] = small_bonus_map(1, -10, 40, [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10])
            b["ladd_size"] = small_bonus_map(1, -10, 40, [0, 1, 2, 3])
    if tgt_is_pc:
        tb = t.setdefault("bonus", {})
        tb["sub_ele"] = small_bonus_map(2, -30, 60, [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10])
        tb["sub_race"] = small_bonus_map(2, -30, 60, [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 12])
        tb["sub_size"] = small_bonus_map(1, -30, 60, [0, 1, 2, 3])
        tb["sub_class"] = small_bonus_map(1, -30, 60, [0, 1, 6])
        tb["sub_defele"] = small_bonus_map(1, -30, 60, [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10])
        tb["near_def"] = R.choice([0, 0, 10, 30])
        tb["long_def"] = R.choice([0, 0, 10, 30])


def build(scale):
    n = lambda k: max(1, int(k * scale))

    # --- plain mixes (all four attacker/target kinds)
    for _ in range(n(300)):
        a = pc(); t = rand_target(); add("mix_pc_vs_any", a, t)
    for _ in range(n(300)):
        a = mob(); t = rand_target(); add("mix_mob_vs_any", a, t)

    # --- base damage edges
    for _ in range(n(100)):  # min == max: mob atk==atk2, PC dex so large that min is capped to max
        if R.random() < 0.5:
            a = mob(); v = R.randint(1, 1500); a["rhw"].update(atk=v, atk2=v)
        else:
            a = pc(dex=R.randint(120, 150)); a["rhw"].update(atk=R.randint(1, 60), wlv=R.randint(1, 4))
        t = rand_target(); sure_hit(a, t); never_crit(a); add("base_min_eq_max", a, t)
    for _ in range(n(60)):  # mob atk > atk2 (min is clamped to atk2)
        a = mob(); a["rhw"].update(atk=R.randint(200, 900), atk2=R.randint(1, 199))
        t = rand_target(); sure_hit(a, t); never_crit(a); add("base_mob_atk_gt_atk2", a, t)
    for _ in range(n(60)):  # zero attack: total 0 -> MISS flagged, div 1
        a = R.choice([mob, pc])(); a["rhw"].update(atk=0, atk2=0 if a["kind"] == "mob" else a["rhw"]["atk2"])
        a["batk"] = 0
        if a["kind"] == "pc": a["rhw"]["atk2"] = 0
        t = rand_target(); sure_hit(a, t); never_crit(a); add("base_zero_atk", a, t)
    for _ in range(n(60)):  # unarmed player (no weapon item): min = dex
        a = pc(); a["rhw"]["wlv"] = 0; a["rhw"]["atk"] = R.randint(5, 80); a["wtype1"] = 0
        t = rand_target(); sure_hit(a, t); never_crit(a); add("base_unarmed", a, t)
    for _ in range(n(80)):  # base ATK, over-refine draw, no_sizefix
        a = pc(); a["batk"] = R.randint(0, 300); a.setdefault("bonus", {})["overrefine_r"] = R.randint(1, 8)
        if R.random() < 0.3: a["no_sizefix"] = True
        t = rand_target(); sure_hit(a, t); never_crit(a); add("base_overrefine_batk", a, t)
    for wt in range(0, 24):  # size modifier for every weapon type x every target size
        if wt in (16, 11): continue
        for sz in range(3):
            a = pc(wtype1=wt); a["rhw"]["wlv"] = R.randint(1, 4) if wt else 0
            t = mob(size=sz); sure_hit(a, t); never_crit(a); add("size_fix", a, t)
    for _ in range(n(60)):  # MAXIMIZE POWER, WEAPON PERFECTION
        a = pc() if R.random() < 0.6 else mob()
        a["sc"] = {R.choice(["maximize", "weaponperfection"]): {}}
        t = rand_target(); sure_hit(a, t); never_crit(a); add("base_maximize_wp", a, t)

    # --- DEF
    for d1, d2 in [(0, 0), (0, 1), (99, 0), (99, 1), (99, 99), (0, 99), (100, 20), (127, 127), (-50, 30), (-128, 5), (10, 0), (50, 400)]:
        for _ in range(n(12)):
            a = R.choice([pc, mob])(); t = rand_target(); t["def"] = d1; t["def2"] = d2
            sure_hit(a, t); never_crit(a); add("def_edge", a, t)
    for _ in range(n(120)):  # soft DEF larger than the damage: min 1 after the refine bonus
        a = R.choice([pc, mob])(); a["rhw"]["atk"] = R.randint(1, 30)
        if a["kind"] == "mob": a["rhw"]["atk2"] = a["rhw"]["atk"] + R.randint(0, 10)
        a["batk"] = 0
        t = rand_target(); t["def2"] = R.randint(30, 300); t["def"] = R.randint(0, 99)
        sure_hit(a, t); never_crit(a); add("def_softdef_gt_damage", a, t)
    for _ in range(n(120)):  # a crit ignores DEF but still draws the soft-DEF random
        a = R.choice([pc, mob]); a = a(); a["cri"] = 1000
        t = rand_target(); t["luk"] = 0; sure_hit(a, t); a["cri"] = 1000; add("def_crit_ignores", a, t)
    for _ in range(n(60)):  # weapon_defense_type: hard DEF moves into soft DEF
        a = R.choice([pc, mob])(); t = rand_target(); sure_hit(a, t); never_crit(a)
        add("def_weapon_defense_type", a, t, cfg={"weapon_defense_type": R.choice([1, 2])})
    for _ in range(n(120)):  # crowd penalties: attackers on a player target
        a = R.choice([pc, mob])(); t = target_pc(); t["targeted"] = R.randint(0, 30); t["def"] = R.randint(0, 99)
        sure_hit(a, t); a["hit"] = R.randint(100, 400); t["flee"] = R.randint(50, 400); never_crit(a)
        add("crowd_penalty", a, t)
    for _ in range(n(40)):  # crowd on a monster target: no penalty (target mask is players), soft DEF is not floored
        a = R.choice([pc, mob])(); t = mob(); t["targeted"] = R.randint(0, 30); t["def2"] = R.randint(0, 60)
        sure_hit(a, t); never_crit(a); add("crowd_mob_target", a, t)

    # --- element table: every (level, attack element, defense element) once with a PC, plus statuses and endow
    for lv in range(1, 5):
        for ae in range(10):
            for de in range(10):
                a = pc(); a["rhw"]["ele"] = ae
                t = mob(element=de, ele_lv=lv); sure_hit(a, t); never_crit(a); add("element_table", a, t)
    for _ in range(n(120)):
        a = pc(); t = mob(); a["rhw"]["ele"] = R.choice([1, 3, 4, 0, 2])
        name = {1: "deluge", 3: "volcano", 4: "violentgale"}.get(a["rhw"]["ele"], R.choice(["deluge", "volcano", "violentgale"]))
        a["sc"] = {name: {"val3": R.choice([5, 10, 15, 20, 25, 30])}}
        sure_hit(a, t); never_crit(a); add("element_status_pct", a, t)
    for _ in range(n(120)):  # endow overrides the weapon element
        a = R.choice([pc, mob])(); t = rand_target(); a["sc"] = {"enchantarms": {"val1": R.randint(0, 9)}}
        sure_hit(a, t); never_crit(a)
        add("element_endow", a, t, cfg={"attack_attr_none": R.choice([14, 14, 0])})
    for _ in range(n(120)):  # non-player basic attacks skip the table (attack_attr_none), unless the mask is cleared
        a = mob(); t = rand_target(); sure_hit(a, t); never_crit(a)
        add("element_attr_none", a, t, cfg={"attack_attr_none": R.choice([14, 14, 0, 2, 12])})
    for _ in range(n(120)):  # negative ratios: damage is capped at 0 (attr_recover 0) or stays negative (1)
        a = pc(); a["rhw"]["ele"] = 6 if R.random() < 0.5 else R.choice([1, 2, 3, 4, 5, 6, 7, 8, 9])
        t = mob(element=R.choice([5, 6, 7, 9]), ele_lv=R.choice([3, 4])); sure_hit(a, t); never_crit(a)
        add("element_negative_ratio", a, t, cfg={"attr_recover": R.choice([0, 1])})
    for _ in range(n(60)):  # EDP
        a = pc(); a["sc"] = {"edp": {"val3": R.choice([20, 30, 40, 50, 60])}}
        t = rand_target(); sure_hit(a, t); never_crit(a); add("element_edp", a, t)

    # --- crit
    for _ in range(n(200)):
        a = pc(); a["cri"] = R.choice([300, 600, 1000]); t = rand_target(); t["luk"] = R.randint(0, 20)
        a.setdefault("bonus", {})["crit_atk_rate"] = R.choice([0, 0, 20, 40])
        if t["kind"] == "pc" and R.random() < 0.5:
            t.setdefault("bonus", {})["crit_def"] = R.choice([10, 30, 60]); t["bonus"]["crit_def_rate"] = R.choice([0, 10, 25])
        sure_hit(a, t); a["cri"] = R.choice([300, 600, 1000]); add("crit_basic", a, t)
    for _ in range(n(120)):  # crit chance exactly around the draw: cri vs luk x2 / x3
        a = R.choice([pc, mob])(); t = rand_target(); a["cri"] = R.randint(1, 300); t["luk"] = R.randint(0, 150)
        add("crit_vs_luk", a, t)
    for _ in range(n(100)):  # crit off: cri 0 (no draw), cri negative after LUK (still draws)
        a = R.choice([pc, mob])(); t = rand_target(); a["cri"] = R.choice([0, 0, 1, 5]); t["luk"] = R.randint(0, 100)
        add("crit_off_or_negative", a, t)
    for _ in range(n(60)):
        a = pc(); t = rand_target(); a["cri"] = 1000
        a.setdefault("bonus", {})["critadd_race"] = {str(t["race"]): R.randint(10, 100)}
        add("crit_critaddrace", a, t)

    # --- hit / miss
    for _ in range(n(200)):
        a = R.choice([pc, mob])(); t = rand_target(); a["hit"] = R.randint(0, 250); t["flee"] = R.randint(0, 400); never_crit(a)
        add("hit_flee_sweep", a, t)
    for _ in range(n(100)):  # hit rate bounds 5..100 and their toggles
        a = R.choice([pc, mob])(); t = rand_target(); never_crit(a)
        a["hit"] = R.choice([0, 20, 400]); t["flee"] = R.choice([0, 150, 500])
        add("hit_rate_clamp", a, t, cfg={"min_hitrate": R.choice([5, 5, 0, 30]), "max_hitrate": R.choice([100, 100, 80, 95])})
    for _ in range(n(120)):  # lucky dodge (perfect dodge) on top of normal combat
        a = R.choice([pc, mob])(); t = rand_target(); t["flee2"] = R.choice([10, 50, 100, 300, 900, 1000]); never_crit(a)
        sure_hit(a, t); t["flee2"] = R.choice([10, 50, 100, 300, 900, 1000]); add("lucky_dodge", a, t)
    for _ in range(n(60)):  # perfect hit
        a = pc(); t = rand_target(); a["hit"] = 0; t["flee"] = 500; never_crit(a)
        a.setdefault("bonus", {})["perfect_hit"] = R.choice([10, 50, 99]); add("hit_perfect_hit", a, t)
    for _ in range(n(60)):  # miss paths forced
        a = R.choice([pc, mob])(); t = rand_target(); a["hit"] = 0; t["flee"] = 500; never_crit(a); add("miss_forced", a, t)

    # --- bows
    for _ in range(n(150)):
        a = pc(wtype1=BOW, arrow=True); a["rhw"]["atk"] = R.randint(20, 300)
        b = a.setdefault("bonus", {}); b["arrow_atk"] = R.choice([0, 15, 30, 55]); b["arrow_hit"] = R.choice([0, 5, 20]); b["arrow_cri"] = R.choice([0, 20, 50])
        b["crit_range"] = R.choice([0, 10]); a["cri"] = R.choice([0, 100, 300, 1000])
        if R.random() < 0.5:
            b["aadd_race"] = small_bonus_map(2, -10, 40, [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 12]); b["aadd_ele"] = small_bonus_map(1, -10, 40, [0, 3, 10])
            b["add_race"] = small_bonus_map(1, -10, 40, [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 12])
        t = rand_target(); add("bow_ranged", a, t)

    # --- cards
    for _ in range(n(300)):
        a = pc(); t = rand_target(); sure_hit(a, t); never_crit(a); random_cards(a, t, t["kind"] == "pc")
        add("cards_both", a, t, cfg={"left_cardfix_to_right": R.choice([1, 1, 0])})
    for _ in range(n(200)):  # mob attackers against card-wearing players
        a = mob(); t = target_pc(); sure_hit(a, t); never_crit(a); random_cards(a, t, True)
        add("cards_vs_player", a, t)
    for _ in range(n(60)):  # huge card sums: cardfix above 1000 and below 0
        a = pc(); t = target_pc(); sure_hit(a, t); never_crit(a)
        a.setdefault("bonus", {})["add_race"] = {str(t["race"]): R.choice([-150, 200, 400, -100])}
        t.setdefault("bonus", {})["sub_race"] = {str(a["race"]): R.choice([-100, 120, 200, 100])}
        add("cards_extreme", a, t)

    # --- dual wield
    for _ in range(n(200)):
        a = pc(job=R.choice(["thief", "thief", None])); a["wtype1"] = R.choice(ONE_HAND_LEFT); a["wtype2"] = R.choice(ONE_HAND_LEFT)
        a["lhw"] = {"atk": R.randint(5, 400), "atk2": R.randint(0, 40), "ele": R.randint(0, 9), "wlv": R.randint(1, 4)}
        if a["job"] is None: del a["job"]
        t = rand_target(); a["cri"] = R.choice([0, 0, 500, 1000])
        if R.random() < 0.6: sure_hit(a, t)
        if R.random() < 0.5: random_cards(a, t, t["kind"] == "pc")
        add("dual_wield", a, t, cfg={"left_cardfix_to_right": R.choice([1, 0])})
    for _ in range(n(40)):  # left hand weak enough that min 1 applies
        a = pc(job="thief"); a["wtype1"] = 1; a["wtype2"] = 1
        a["rhw"].update(atk=R.randint(1, 20), atk2=0); a["lhw"] = {"atk": R.randint(1, 5), "atk2": 0, "ele": 0, "wlv": 1}
        t = mob(); t["def2"] = R.randint(20, 100); sure_hit(a, t); never_crit(a); add("dual_wield_min1", a, t)

    for _ in range(n(200)):  # dual-wield passive levels, Assassin line and Kagerou/Oboro
        a = pc(job=R.choice(["thief", "kagerou"])); a["wtype1"] = R.choice(ONE_HAND_LEFT); a["wtype2"] = R.choice(ONE_HAND_LEFT)
        a["lhw"] = {"atk": R.randint(5, 400), "atk2": R.randint(0, 40), "ele": R.randint(0, 9), "wlv": R.randint(1, 4)}
        a["skills"] = {"as_right": R.randint(0, 5), "as_left": R.randint(0, 5), "ko_right": R.randint(0, 5), "ko_left": R.randint(0, 5)}
        t = rand_target(); sure_hit(a, t); never_crit(a); add("dual_wield_skills", a, t)

    # --- worked examples of BR_RULESET.md section 7, recreated end to end (damage 1000 at the stage under test):
    # a weapon of ATK 1 whose DEX-scaled minimum is capped to 1, plus 999 base ATK, hits a DEF-0 monster
    def exact_1000(**kw):
        a = pc(dex=150, wtype1=2, batk=999, cri=0, hit=400, **kw)  # one-hand sword: 100% vs medium
        a["rhw"] = {"atk": 1, "atk2": 0, "ele": 0, "wlv": 4}
        a["level"] = 50
        return a
    t = mob(size=1, race=0, element=0, ele_lv=1)
    t.update({"def": 0, "def2": 0, "flee": 0}); t.pop("flee2", None)
    a = exact_1000(); add("worked_example", a, dict(t))                                         # control: 1000 passes through
    a = exact_1000(); a["rhw"]["ele"] = 1; a["sc"] = {"deluge": {"val3": 15}}                   # E-1: water 1000 vs fire lv1, Deluge 15
    tt = dict(t); tt["element"] = 3; add("worked_example", a, tt)
    a = exact_1000(); a["bonus"] = {"add_race": {"0": 20}, "add_size": {"1": 15}}               # E-3: race +20 %, size +15 %
    add("worked_example", a, dict(t))
    a = exact_1000(job="thief"); a["wtype1"] = 1; a["wtype2"] = 1                               # E-6: Assassin, AS_RIGHT 5 / AS_LEFT 5
    a["lhw"] = {"atk": 1, "atk2": 0, "ele": 0, "wlv": 4}; a["skills"] = {"as_right": 5, "as_left": 5}
    tt = dict(t); tt["size"] = 0; add("worked_example", a, tt)                                  # dagger: 100% vs small

    # --- statuses on the attacker
    for _ in range(n(200)):
        a = R.choice([pc, mob])(); t = rand_target(); sure_hit(a, t); never_crit(a)
        sc = {}
        if R.random() < 0.4: sc["berserk"] = {}
        if R.random() < 0.3: sc["curse"] = {}
        if R.random() < 0.3: sc["provoke"] = {"val2": R.choice([2, 5, 8, 11, 14])}
        if R.random() < 0.3: sc["concentration"] = {"val2": R.choice([5, 10, 15, 20, 25])}
        if R.random() < 0.3: sc["overthrust"] = {"val3": R.choice([5, 10, 15, 20, 25])}
        a["sc"] = sc
        add("status_atk_pct", a, t)
    # --- statuses on the target (and map modes)
    for _ in range(n(300)):
        a = R.choice([pc, mob])(); t = rand_target(); sure_hit(a, t); never_crit(a)
        sc = {}
        if R.random() < 0.5: sc["assumptio"] = {}
        if R.random() < 0.2: sc["soundofdestruction"] = {}
        if R.random() < 0.2: sc["shadow_clock"] = {}
        if R.random() < 0.3: sc["defender"] = {"val2": R.choice([20, 35, 50, 65, 80])}
        if R.random() < 0.2: sc["adjustment"] = {}
        if R.random() < 0.2: sc["smokepowder"] = {}
        if R.random() < 0.2: sc["water_barrier"] = {}
        t["sc"] = sc
        if a["kind"] == "pc" and R.random() < 0.3: a["wtype1"] = BOW; a["arrow"] = True
        add("target_status_map", a, t, mp=R.choice(["normal", "pvp", "gvg"]),
            cfg={"gvg_short_damage_rate": R.choice([80, 80, 100, 50]), "gvg_long_damage_rate": R.choice([80, 80, 100, 50])})
    for _ in range(n(120)):
        a = R.choice([pc, mob])(); t = rand_target(); sure_hit(a, t); never_crit(a)
        if a["kind"] == "pc" and R.random() < 0.5: a["wtype1"] = BOW; a["arrow"] = True
        add("map_gvg_rates", a, t, mp="gvg", cfg={"gvg_short_damage_rate": R.choice([80, 1, 10, 0]), "gvg_long_damage_rate": R.choice([80, 1, 10, 0])})


def run_server(server, cases, out_dir):
    kv = out_dir / "cases.kv"; out = out_dir / "cases.out"; dump = out_dir / "dump.txt"
    kv.write_text("\n".join(case_tokens(c) for c in cases) + "\n")
    env = dict(os.environ, RA_GOLDEN_CASES=str(kv), RA_GOLDEN_OUT=str(out), RA_GOLDEN_DUMP=str(dump))
    r = subprocess.run([str(server / "map-server"), "--run-once"], cwd=server, env=env, capture_output=True, text=True, timeout=1800)
    if not out.exists():
        sys.exit("golden hook produced no output:\n" + r.stdout[-2000:] + r.stderr[-2000:])
    return out.read_text().splitlines(), dump.read_text()


def parse_result(line):
    head, _, rest = line.partition(" R")
    fields = dict(tok.split("=") for tok in head.split())
    rpart, _, spart = rest.partition(" S")
    draws = []
    for tok in rpart.split():
        if tok.startswith("r:"): draws.append(int(tok[2:]))
        else:
            _, lo, hi, v = tok.split(":"); draws.append([int(lo), int(hi), int(v)])
    stages = []
    for tok in spart.split():
        n, a, b = tok.split(":"); stages.append([n, int(a), int(b)])
    return {"id": int(fields["id"]),
            "out": {"damage": int(fields["dmg"]), "damage2": int(fields["dmg2"]), "type": int(fields["type"]), "div": int(fields["div"]),
                    "flag": int(fields["flag"]), "dmg_lv": int(fields["dmglv"]), "draws": draws, "stages": stages}}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--server", required=True, help="<build>/rA of the patched pre-renewal copy")
    ap.add_argument("--out", default=str(pathlib.Path(__file__).resolve().parents[1] / "golden" / "weapon_pre.jsonl.gz"))
    ap.add_argument("--scale", type=float, default=1.0)
    ap.add_argument("--seed", type=int, default=SEED)
    args = ap.parse_args()
    reseed(args.seed)
    build(args.scale)
    with tempfile.TemporaryDirectory() as d:
        lines, dump = run_server(pathlib.Path(args.server), CASES, pathlib.Path(d))
    res = {}
    for ln in lines:
        r = parse_result(ln); res[r["id"]] = r["out"]
    assert len(res) == len(CASES), (len(res), len(CASES))
    out = pathlib.Path(args.out); out.parent.mkdir(parents=True, exist_ok=True)
    with open(out, "wb") as raw, gzip.GzipFile(filename="", mode="wb", fileobj=raw, mtime=0, compresslevel=9) as gz:
        for c in CASES:
            c = dict(c); c["out"] = res[c["id"]]
            gz.write((json.dumps(c, sort_keys=True, separators=(",", ":")) + "\n").encode())
    cats = {}
    for c in CASES: cats[c["cat"]] = cats.get(c["cat"], 0) + 1
    print(f"{len(CASES)} cases -> {out} ({out.stat().st_size} bytes)")
    for k in sorted(cats): print(f"  {k:28s} {cats[k]}")


if __name__ == "__main__":
    main()
