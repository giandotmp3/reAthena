#!/usr/bin/env python3
"""Generates the RENEWAL basic-weapon-attack golden cases and records the REAL legacy result for each.

  gen_cases.py --server <build>/rA [--out conformance/golden/weapon_re.jsonl.gz] [--scale 1.0] [--seed N]

Same mechanics as ruleset-prerenewal/conformance/golden_gen/gen_cases.py (unit form -> key=value tokens -> one run of the patched
map-server -> inputs + legacy output + every RNG draw + stage probes, gzip with a fixed mtime), but the server is the RENEWAL build
and the case space covers the renewal ATK parts (status / weapon / equip / mastery / percent ATK), P.ATK, RES, the float32 crit
step, the renewal DEF formula and the renewal element handling.
"""
import argparse, gzip, json, os, random, subprocess, sys, tempfile, pathlib

SEED = 20261019  # default; --seed generates a different (uncommitted) fuzz set

BONUS_SCALAR = ["arrow_atk", "crit_atk_rate", "non_crit_atk_rate", "short_atk", "long_atk", "near_def", "long_def", "crit_def",
                "crit_def_rate", "overrefine_r", "overrefine_l", "perfect_hit", "arrow_hit", "arrow_cri", "crit_range", "skill_ratio",
                "atk_rate", "wdr_r", "wdr_l", "arrow_ele"]
BONUS_MAPS = {"add_race": "add.race", "add_ele": "add.ele", "add_size": "add.size", "add_class": "add.class",
              "ladd_race": "ladd.race", "ladd_ele": "ladd.ele", "ladd_size": "ladd.size",
              "aadd_race": "aadd.race", "aadd_ele": "aadd.ele", "aadd_size": "aadd.size", "aadd_class": "aadd.class",
              "sub_race": "sub.race", "sub_ele": "sub.ele", "sub_size": "sub.size", "sub_class": "sub.class",
              "sub_defele": "sub.defele", "critadd_race": "critadd.race", "ignore_res": "ignore_res"}
UNIT_SCALAR = ["level", "str", "agi", "vit", "int", "dex", "luk", "batk", "hit", "cri", "flee", "flee2", "def", "def2", "size", "race",
               "ele_lv", "targeted", "wtype1", "wtype2", "patk", "crate", "eatk", "res", "mres", "damagetaken"]


def unit_tokens(p, u):
    t = [f"{p}type={0 if u['kind'] == 'pc' else 1}"]
    for k in UNIT_SCALAR:
        if k in u: t.append(f"{p}{k}={u[k]}")
    if "element" in u: t.append(f"{p}ele={u['element']}")
    if u.get("boss"): t.append(f"{p}boss=1")
    if u.get("job") in ("thief", "kagerou"): t.append(f"{p}job={ {'thief': 1, 'kagerou': 2}[u['job']] }")
    for k, v in u.get("skills", {}).items():
        t.append(f"{p}skill.{k}={v}")
    if u.get("arrow"): t.append(f"{p}bow=1")
    if u.get("no_sizefix"): t.append(f"{p}no_sizefix=1")
    if u.get("shield"): t.append(f"{p}shield=1")
    if u.get("ammo"): t.append(f"{p}ammo={u['ammo']}")
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
R = random.Random(SEED)
CASES = []


def reseed(seed):
    global R
    R = random.Random(seed)


MELEE_WT = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 13, 14, 15]  # no bow, no katar, no fist, no guns
STR_WT = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 15]          # base stat STR in the renewal weapon roll
DEX_WT = [13, 14]                                          # musical / whip: DEX, but not arrow users unless arrow=True
BOW = 11
GUNS = [17, 18, 19, 20, 21]
ONE_HAND_LEFT = [1, 2, 6]


def add(cat, a, t, mp="normal", cfg=None, seed=None):
    CASES.append({"id": len(CASES) + 1, "cat": cat, "seed": seed if seed is not None else R.randrange(1, 2**31),
                  "map": mp, "cfg": cfg or {}, "a": a, "t": t})


def pc(**kw):
    u = {"kind": "pc", "level": R.randint(1, 99), "str": R.randint(1, 130), "agi": R.randint(1, 120), "vit": R.randint(1, 120),
         "int": R.randint(1, 120), "dex": R.randint(1, 150), "luk": R.randint(1, 120), "batk": R.randint(0, 400),
         "hit": R.randint(60, 350), "cri": R.choice([0, 0, 10, 50, 100, 250, 500]), "flee": R.randint(20, 300),
         "def": R.randint(0, 120), "def2": R.randint(0, 150), "size": 1, "race": 10, "element": 0, "ele_lv": 1,
         "wtype1": R.choice(STR_WT), "patk": R.choice([0, 0, 5, 12, 30, 55]), "crate": R.choice([0, 0, 10, 25, 50]),
         "eatk": R.choice([0, 0, 15, 60, 150]), "res": 0, "mres": 0}
    u["rhw"] = {"atk": R.randint(10, 700), "atk2": R.randint(0, 60), "ele": R.randint(0, 9), "wlv": R.randint(1, 4)}
    u.update(kw)
    return u


def mob(**kw):
    atk = R.randint(5, 1500)
    u = {"kind": "mob", "level": R.randint(1, 99), "str": R.randint(1, 120), "agi": R.randint(1, 120), "vit": R.randint(1, 120),
         "int": R.randint(1, 120), "dex": R.randint(1, 150), "luk": R.randint(1, 120), "batk": R.randint(0, 100),
         "hit": R.randint(60, 350), "cri": R.choice([0, 0, 10, 50, 100]), "flee": R.randint(20, 300),
         "def": R.randint(0, 99), "def2": R.randint(0, 120), "size": R.randint(0, 2), "race": R.randint(0, 9),
         "element": R.randint(0, 9), "ele_lv": R.randint(1, 4), "res": R.choice([0, 0, 0, 30, 120, 500]),
         "rhw": {"atk": atk, "atk2": atk + R.randint(0, 400), "ele": R.randint(0, 9)}}
    u.update(kw)
    return u


def target_pc(**kw):
    u = pc(**kw)
    u["size"] = R.randint(0, 2)
    u["race"] = R.choice([10, 11])
    u["element"] = R.randint(0, 9)
    u["ele_lv"] = R.randint(1, 4)
    u["res"] = R.choice([0, 0, 40, 150, 400])
    u["mres"] = R.choice([0, 100])
    u.update(kw)
    return u


def rand_target(**kw):
    return (target_pc if R.random() < 0.4 else mob)(**kw)


def sure_hit(a, t):
    a["hit"] = 500
    t["flee"] = 0
    t.pop("flee2", None)


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


def exact(**kw):
    """A player whose renewal weapon ATK part is exactly 1000 and every other part 0 (no batk/eatk/patk): the weapon is not
    equipped (wlv 0), so the part is the plain weapon ATK, with no roll and no size fix at 100%."""
    base = dict(wtype1=0, batk=0, eatk=0, patk=0, crate=0, cri=0, hit=500, dex=1, str=1)
    base.update(kw)
    a = pc(**base)
    a["rhw"] = {"atk": 1000, "atk2": 0, "ele": 0, "wlv": 0}
    return a


def build(scale):
    n = lambda k: max(1, int(k * scale))

    # --- plain mixes
    for _ in range(n(400)):
        a = pc(); t = rand_target(); add("mix_pc_vs_any", a, t)
    for _ in range(n(400)):
        a = mob(); t = rand_target(); add("mix_mob_vs_any", a, t)

    # --- ATK parts: weapon ATK roll (variance, base stat bonus), crit / maximize / weapon perfection / size
    for _ in range(n(200)):
        a = pc(wtype1=R.choice(STR_WT + DEX_WT)); a["rhw"]["wlv"] = R.randint(1, 4); a["batk"] = 0; a["eatk"] = 0; a["patk"] = 0
        t = rand_target(); sure_hit(a, t); never_crit(a); add("weapon_roll", a, t)
    for _ in range(n(120)):  # bows and guns: DEX instead of STR (unless Soul Attack), arrow ATK goes to equipAtk (ammo needed)
        wt = R.choice([BOW, BOW, BOW] + GUNS)
        a = pc(wtype1=wt, arrow=True); a["rhw"]["atk"] = R.randint(20, 300); a["rhw"]["wlv"] = R.randint(1, 4)
        a.setdefault("bonus", {})["arrow_atk"] = R.choice([0, 15, 30, 55, 120]); a["ammo"] = R.choice([1, 1, 1, 2, 0])
        a["str"] = R.randint(1, 130); a["dex"] = R.randint(1, 150)
        t = rand_target(); sure_hit(a, t); never_crit(a); add("bow_gun_dex", a, t)
    for _ in range(n(60)):  # DEX-based weapons that are NOT firing arrows (musical, whip) use DEX too
        a = pc(wtype1=R.choice(DEX_WT)); t = rand_target(); sure_hit(a, t); never_crit(a); add("weapon_roll_dex_nonarrow", a, t)
    for _ in range(n(80)):  # critical and Maximize Power take the top of the range (no weapon draw)
        a = pc(); t = rand_target(); sure_hit(a, t)
        if R.random() < 0.5:
            a["cri"] = 1000; t["luk"] = 0
        else:
            a["cri"] = 0; a["sc"] = {"maximize": {}}
        add("weapon_max_or_crit", a, t)
    for _ in range(n(60)):  # Weapon Perfection skips the size modifier of the weapon part
        a = pc(); a["sc"] = {"weaponperfection": {}}; t = rand_target(); t["size"] = R.randint(0, 2)
        sure_hit(a, t); never_crit(a); add("weapon_perfection", a, t)
    for wt in range(0, 24):  # size modifier (db/re/size_fix) for every weapon type x every target size
        if wt in (16,): continue
        for sz in range(3):
            a = pc(wtype1=wt); a["rhw"]["wlv"] = R.randint(1, 4) if wt else 0
            if wt in [11] + GUNS: a["arrow"] = True
            t = mob(size=sz); sure_hit(a, t); never_crit(a); add("size_fix", a, t)
    for _ in range(n(60)):  # near the uint16 limits of the weapon ATK range (atkmin wraps, atkmax is capped at 65535)
        a = pc(); a["rhw"].update(atk=R.randint(30000, 65000), atk2=R.randint(0, 500), wlv=R.randint(2, 4)); a["str"] = R.randint(60, 130)
        t = rand_target(); sure_hit(a, t); never_crit(a); add("weapon_atk_u16", a, t)
    for _ in range(n(60)):  # unarmed player: no weapon item, no roll; weapon ATK is the plain value
        a = pc(); a["rhw"]["wlv"] = 0; a["rhw"]["atk"] = R.randint(5, 80); a["wtype1"] = 0
        t = rand_target(); sure_hit(a, t); never_crit(a); add("unarmed", a, t)
    for _ in range(n(80)):  # over-refine draw and weapon_damage_rate (inside the weapon part, before the size modifier)
        a = pc(); b = a.setdefault("bonus", {}); b["overrefine_r"] = R.randint(0, 8); b["wdr_r"] = R.choice([0, 0, 10, 25, -20])
        if R.random() < 0.3: a["no_sizefix"] = True
        t = rand_target(); sure_hit(a, t); never_crit(a); add("overrefine_wdr", a, t)
    for _ in range(n(60)):  # legacy defect: weapon_damage_rate[16] is indexed by weapon type up to 23 (guns read arrow_addele[type - 16])
        wt = R.choice(GUNS); a = pc(wtype1=wt, arrow=True); a["rhw"]["atk"] = R.randint(30, 300); a["ammo"] = 1
        a.setdefault("bonus", {})["aadd_ele"] = {str(i): R.choice([0, 10, 20, -10]) for i in range(0, 8)}
        t = rand_target(); sure_hit(a, t); never_crit(a); add("gun_wdr_alias", a, t)
    for _ in range(n(100)):  # shield in the left hand: the left weapon roll (0..0) still draws; dual wield: both draws
        a = pc(shield=True); a["bonus"] = {"overrefine_l": R.choice([0, 0, 5])}
        t = rand_target(); sure_hit(a, t); never_crit(a); add("shield_draw", a, t)

    # --- ATK parts: status ATK (batk), equip ATK, percent ATK, mastery ATK
    for _ in range(n(120)):
        a = pc(batk=R.randint(0, 3000), eatk=R.randint(0, 600)); a.setdefault("bonus", {})["atk_rate"] = R.choice([0, 5, 10, 25, -10])
        t = rand_target(); sure_hit(a, t); never_crit(a); add("parts_batk_eatk_atkrate", a, t)
    for _ in range(n(150)):  # masteries: element-free, not carded by the attacker, size-free, added after P.ATK
        wt = R.choice([1, 2, 3, 4, 5, 6, 7, 8, 9])
        a = pc(wtype1=wt)
        a["skills"] = {k: R.randint(0, 10) for k in ["sm_sword", "sm_twohand", "mace", "spear", "axe", "demonbane", "weapon_research"] if R.random() < 0.6}
        t = rand_target(); sure_hit(a, t); never_crit(a); add("mastery_atk", a, t)

    # --- P.ATK and the float32 steps
    for _ in range(n(150)):  # values above 2^24: (float) conversion of the int64 loses bits
        a = pc(batk=R.randint(4_000_000, 30_000_000), patk=R.choice([1, 7, 13, 29, 55, 99]), eatk=R.randint(0, 32000))  # (status_data.eatk is an int16)
        t = rand_target(); sure_hit(a, t); never_crit(a); add("patk_float32", a, t)
    for _ in range(n(100)):  # P.ATK on ordinary values, including a negative P.ATK
        a = pc(patk=R.choice([-30, -5, 0, 3, 9, 40, 120])); t = rand_target(); sure_hit(a, t); never_crit(a); add("patk_basic", a, t)

    # --- crit
    for _ in range(n(250)):  # player crit: x(1.4f + 0.01f * crate) in float32, then crit_def_rate
        a = pc(crate=R.randint(0, 90)); a["cri"] = 1000; t = rand_target(); t["luk"] = R.randint(0, 20)
        a.setdefault("bonus", {})["crit_atk_rate"] = R.choice([0, 0, 20, 40, -10])
        if t["kind"] == "pc" and R.random() < 0.5:
            t.setdefault("bonus", {})["crit_def"] = R.choice([10, 30, 60]); t["bonus"]["crit_def_rate"] = R.choice([0, 10, 25])
        sure_hit(a, t); a["cri"] = 1000; add("crit_player", a, t)
    for _ in range(n(200)):  # monster crit: base damage x14/10 and again x1.4f after DEF (legacy double multiplier)
        a = mob(); a["cri"] = 1000; t = rand_target(); t["luk"] = R.randint(0, 20)
        if t["kind"] == "pc" and R.random() < 0.5:
            t.setdefault("bonus", {})["crit_def_rate"] = R.choice([0, 10, 25])
        sure_hit(a, t); a["cri"] = 1000; add("crit_mob", a, t)
    for _ in range(n(120)):  # crit chance around the draw: cri vs luk x2 / x3
        a = R.choice([pc, mob])(); t = rand_target(); a["cri"] = R.randint(1, 300); t["luk"] = R.randint(0, 150)
        add("crit_vs_luk", a, t)
    for _ in range(n(100)):  # crit off: cri 0 (no draw), cri negative after LUK (still draws)
        a = R.choice([pc, mob])(); t = rand_target(); a["cri"] = R.choice([0, 0, 1, 5]); t["luk"] = R.randint(0, 100)
        add("crit_off_or_negative", a, t)
    for _ in range(n(60)):
        a = pc(); t = rand_target(); a["cri"] = 1000
        a.setdefault("bonus", {})["critadd_race"] = {str(t["race"]): R.randint(10, 100)}
        add("crit_critaddrace", a, t)
    for _ in range(n(60)):  # crit does NOT ignore DEF in renewal
        a = R.choice([pc, mob])(); t = rand_target(); t["def"] = R.randint(30, 200); t["def2"] = R.randint(10, 100)
        sure_hit(a, t); t["luk"] = 0; a["cri"] = 1000; add("crit_def_applies", a, t)

    # --- RES
    for _ in range(n(200)):
        a = R.choice([pc, mob])(); t = rand_target(); t["res"] = R.choice([1, 20, 50, 100, 300, 800, 2000, 10000]); sure_hit(a, t); never_crit(a)
        if a["kind"] == "pc" and R.random() < 0.6:
            a.setdefault("bonus", {})["ignore_res"] = {str(R.choice([t["race"], 12])): R.choice([10, 30, 50, 80, 100])}
        if R.random() < 0.3:
            a["sc"] = {R.choice(["a_telum", "potent_venom"]): {"val2": R.choice([10, 25, 50, 70])}}
        add("res", a, t, cfg={"max_res_mres_ignored": R.choice([50, 50, 50, 20, 100])})
    for _ in range(n(40)):
        a = R.choice([pc, mob])(); t = rand_target(); t["res"] = R.choice([-50, 0, 0]); sure_hit(a, t); never_crit(a); add("res_nonpositive", a, t)

    # --- DEF
    for d1, d2 in [(0, 0), (0, 1), (99, 0), (99, 1), (99, 99), (0, 99), (100, 20), (127, 127), (-50, 30), (-128, 5), (10, 0), (50, 400),
                   (200, 50), (500, 100), (1200, 0), (4000, 10), (-399, 0), (-400, 0), (-401, 0), (-800, 3), (-3999, 0), (-4000, 0),
                   (-4001, 0), (32767, 5), (-32768, 5), (99, -20), (0, -50)]:
        for _ in range(n(10)):
            a = R.choice([pc, mob])(); t = rand_target(); t["def"] = d1; t["def2"] = d2
            sure_hit(a, t); never_crit(a); add("def_edge", a, t)
    for _ in range(n(120)):  # soft DEF larger than the damage: min 1
        a = R.choice([pc, mob])(); a["rhw"]["atk"] = R.randint(1, 30)
        if a["kind"] == "mob": a["rhw"]["atk2"] = a["rhw"]["atk"] + R.randint(0, 10)
        a["batk"] = 0; a["eatk"] = 0
        t = rand_target(); t["def2"] = R.randint(30, 600); t["def"] = R.randint(0, 99)
        sure_hit(a, t); never_crit(a); add("def_softdef_gt_damage", a, t)
    for _ in range(n(60)):  # weapon_defense_type: hard DEF moves into soft DEF
        a = R.choice([pc, mob])(); t = rand_target(); sure_hit(a, t); never_crit(a)
        add("def_weapon_defense_type", a, t, cfg={"weapon_defense_type": R.choice([1, 2, 3])})
    for _ in range(n(150)):  # crowd penalties: attackers on a player target
        a = R.choice([pc, mob])(); t = target_pc(); t["targeted"] = R.randint(0, 30); t["def"] = R.randint(0, 150)
        sure_hit(a, t); a["hit"] = R.randint(100, 400); t["flee"] = R.randint(50, 400); never_crit(a)
        add("crowd_penalty", a, t, cfg={"vit_penalty_type": R.choice([1, 1, 2, 0]), "agi_penalty_type": R.choice([1, 1, 2, 0])})
    for _ in range(n(40)):  # crowd on a monster target
        a = R.choice([pc, mob])(); t = mob(); t["targeted"] = R.randint(0, 30); t["def2"] = R.randint(0, 60)
        sure_hit(a, t); never_crit(a); add("crowd_mob_target", a, t)
    for _ in range(n(40)):  # Steel Body stops the hard DEF crowd penalty
        a = R.choice([pc, mob])(); t = target_pc(); t["targeted"] = R.randint(3, 30); t["def"] = R.randint(10, 150)
        t["sc"] = {"steelbody": {}}; sure_hit(a, t); never_crit(a); add("crowd_steelbody", a, t)

    # --- element: parts carry the element for players; the table is applied once per part
    for lv in range(1, 5):
        for ae in range(10):
            for de in range(10):
                a = pc(); a["rhw"]["ele"] = ae
                t = mob(element=de, ele_lv=lv); sure_hit(a, t); never_crit(a); add("element_table", a, t)
    for _ in range(n(150)):  # Volcano / Violent Gale / Deluge add to the ratio in renewal; also on dual wield
        a = pc(); t = mob(); a["rhw"]["ele"] = R.choice([1, 3, 4, 0, 2])
        name = {1: "deluge", 3: "volcano", 4: "violentgale"}.get(a["rhw"]["ele"], R.choice(["deluge", "volcano", "violentgale"]))
        a["sc"] = {name: {"val3": R.choice([5, 10, 15, 20, 25, 30, 120, -60])}}
        sure_hit(a, t); never_crit(a); add("element_status_ratio", a, t)
    for _ in range(n(120)):  # endow overrides the weapon element (and Mild Wind-less status ATK stays neutral)
        a = R.choice([pc, mob])(); t = rand_target(); a["sc"] = {"enchantarms": {"val1": R.randint(0, 9)}}
        sure_hit(a, t); never_crit(a)
        add("element_endow", a, t, cfg={"attack_attr_none": R.choice([14, 14, 0])})
    for _ in range(n(150)):  # monster basic attacks: neutral when attack_attr_none covers them, else the table (RE: after the base damage)
        a = mob(); t = rand_target(); sure_hit(a, t); never_crit(a)
        add("element_mob_attr_none", a, t, cfg={"attack_attr_none": R.choice([14, 14, 0, 2, 12, 1])})
    for _ in range(n(150)):  # negative ratios: part ratio clamps to 0 with attr_recover 0; element stage keeps the sign then caps at 0
        a = pc(); a["rhw"]["ele"] = R.choice([1, 2, 3, 4, 5, 6, 7, 8, 9])
        t = mob(element=R.choice([5, 6, 7, 9]), ele_lv=R.choice([3, 4])); sure_hit(a, t); never_crit(a)
        if R.random() < 0.3: a["sc"] = {"edp": {"val1": R.randint(1, 10)}}
        add("element_negative_ratio", a, t, cfg={"attr_recover": R.choice([0, 1])})
    for _ in range(n(120)):  # negative ratio from a mob attacker table with attack_attr_none cleared
        a = mob(); a["rhw"]["ele"] = R.choice([5, 6, 7, 9]); t = mob(element=R.choice([5, 6, 7, 9]), ele_lv=R.choice([3, 4]))
        sure_hit(a, t); never_crit(a); add("element_mob_negative", a, t, cfg={"attack_attr_none": 0, "attr_recover": R.choice([0, 1])})
    for _ in range(n(100)):  # bows: arrow element replaces the weapon element for the right hand
        a = pc(wtype1=BOW, arrow=True); a["ammo"] = 1; a.setdefault("bonus", {})["arrow_ele"] = R.randint(1, 9)
        t = rand_target(); sure_hit(a, t); never_crit(a); add("element_arrow", a, t)

    # --- cards (attacker cards on the weapon/equip part, target cards on every part, non-PC attacker: on the total)
    for _ in range(n(350)):
        a = pc(); t = rand_target(); sure_hit(a, t); never_crit(a); random_cards(a, t, t["kind"] == "pc")
        add("cards_both", a, t, cfg={"left_cardfix_to_right": R.choice([1, 1, 0])})
    for _ in range(n(250)):  # mob attackers against card-wearing players
        a = mob(); t = target_pc(); sure_hit(a, t); never_crit(a); random_cards(a, t, True)
        add("cards_vs_player_mob", a, t)
    for _ in range(n(150)):  # player attackers against card-wearing players: target cards on the parts, not on the total
        a = pc(); t = target_pc(); sure_hit(a, t); never_crit(a); random_cards(a, t, True)
        if R.random() < 0.4: a["wtype1"] = BOW; a["arrow"] = True; a["ammo"] = 1
        add("cards_vs_player_pc", a, t, cfg={"left_cardfix_to_right": R.choice([1, 1, 0])})
    for _ in range(n(60)):  # huge card sums: cardfix above 1000 and below 0
        a = pc(); t = target_pc(); sure_hit(a, t); never_crit(a)
        a.setdefault("bonus", {})["add_race"] = {str(t["race"]): R.choice([-150, 200, 400, -100])}
        t.setdefault("bonus", {})["sub_race"] = {str(a["race"]): R.choice([-100, 120, 200, 100])}
        add("cards_extreme", a, t)
    for _ in range(n(150)):  # short / long attack rates are percent additions on the sum in renewal (not card fixes)
        a = pc(); b = a.setdefault("bonus", {}); b["short_atk"] = R.choice([0, 10, 25, -10]); b["long_atk"] = R.choice([0, 10, 25, -10])
        b["non_crit_atk_rate"] = R.choice([0, 0, 15, -5])
        if R.random() < 0.5: a["wtype1"] = BOW; a["arrow"] = True; a["ammo"] = 1
        t = rand_target(); sure_hit(a, t); never_crit(a); add("atk_rates_short_long", a, t)

    # --- dual wield
    for _ in range(n(220)):
        a = pc(job=R.choice(["thief", "thief", None])); a["wtype1"] = R.choice(ONE_HAND_LEFT); a["wtype2"] = R.choice(ONE_HAND_LEFT)
        a["lhw"] = {"atk": R.randint(5, 400), "atk2": R.randint(0, 40), "ele": R.randint(0, 9), "wlv": R.randint(1, 4)}
        if a["job"] is None: del a["job"]
        t = rand_target(); a["cri"] = R.choice([0, 0, 500, 1000])
        if R.random() < 0.6: sure_hit(a, t)
        if R.random() < 0.5: random_cards(a, t, t["kind"] == "pc")
        add("dual_wield", a, t, cfg={"left_cardfix_to_right": R.choice([1, 0])})
    for _ in range(n(40)):  # left hand weak enough that min applies
        a = pc(job="thief"); a["wtype1"] = 1; a["wtype2"] = 1; a["batk"] = 0; a["eatk"] = 0
        a["rhw"].update(atk=R.randint(1, 20), atk2=0); a["lhw"] = {"atk": R.randint(1, 5), "atk2": 0, "ele": 0, "wlv": 1}
        t = mob(); t["def2"] = R.randint(20, 100); sure_hit(a, t); never_crit(a); add("dual_wield_min1", a, t)
    for _ in range(n(200)):  # dual-wield passive levels, Assassin line and Kagerou/Oboro
        a = pc(job=R.choice(["thief", "kagerou"])); a["wtype1"] = R.choice(ONE_HAND_LEFT); a["wtype2"] = R.choice(ONE_HAND_LEFT)
        a["lhw"] = {"atk": R.randint(5, 400), "atk2": R.randint(0, 40), "ele": R.randint(0, 9), "wlv": R.randint(1, 4)}
        a["skills"] = {"as_right": R.randint(0, 5), "as_left": R.randint(0, 5), "ko_right": R.randint(0, 5), "ko_left": R.randint(0, 5)}
        a.setdefault("bonus", {})["overrefine_l"] = R.choice([0, 3])
        t = rand_target(); sure_hit(a, t); never_crit(a); add("dual_wield_skills", a, t)

    # --- statuses on the attacker
    for _ in range(n(250)):
        a = R.choice([pc, mob])(); t = rand_target(); sure_hit(a, t); never_crit(a)
        sc = {}
        if R.random() < 0.4: sc["berserk"] = {}
        if R.random() < 0.3: sc["curse"] = {}
        if R.random() < 0.3: sc["provoke"] = {"val2": R.choice([2, 5, 8, 11, 14])}
        if R.random() < 0.3: sc["concentration"] = {"val2": R.choice([5, 10, 15, 20, 25])}
        if R.random() < 0.3: sc["overthrust"] = {"val3": R.choice([5, 10, 15, 20, 25])}
        if R.random() < 0.3: sc["edp"] = {"val1": R.randint(1, 10)}
        if a["kind"] == "pc" and R.random() < 0.3: a.setdefault("bonus", {})["skill_ratio"] = R.choice([5, 20, -30])
        a["sc"] = sc
        add("status_atk_ratio", a, t)
    for _ in range(n(60)):  # ATK% is capped at 0 from below (many debuffs) and the ratio keeps its other terms
        a = R.choice([pc, mob])(); t = rand_target(); sure_hit(a, t); never_crit(a)
        a["sc"] = {"curse": {}, "provoke": {"val2": -90}, "concentration": {"val2": -20}}
        add("status_atk_floor", a, t)

    # --- statuses on the target (and map modes)
    for _ in range(n(350)):
        a = R.choice([pc, mob])(); t = rand_target(); sure_hit(a, t); never_crit(a)
        sc = {}
        if R.random() < 0.3: sc["assumptio"] = {}      # RE: Assumptio raises DEF through status_calc, no damage reduction in battle_calc_damage
        if R.random() < 0.2: sc["soundofdestruction"] = {}
        if R.random() < 0.2: sc["shadow_clock"] = {}
        if R.random() < 0.3: sc["defender"] = {"val2": R.choice([20, 35, 50, 65, 80])}
        if R.random() < 0.2: sc["adjustment"] = {}
        if R.random() < 0.2: sc["smokepowder"] = {}
        if R.random() < 0.2: sc["water_barrier"] = {}
        if R.random() < 0.2: sc["kaite"] = {}
        if R.random() < 0.2: sc["raid"] = {}
        if R.random() < 0.2: sc["steelbody"] = {}
        if R.random() < 0.2: sc["groundgravity"] = {}
        t["sc"] = sc
        if t["kind"] == "mob" and R.random() < 0.3: t["boss"] = True
        if a["kind"] == "pc" and R.random() < 0.3: a["wtype1"] = BOW; a["arrow"] = True; a["ammo"] = 1
        add("target_status_map", a, t, mp=R.choice(["normal", "pvp", "gvg"]),
            cfg={"gvg_short_damage_rate": R.choice([80, 80, 100, 50]), "gvg_long_damage_rate": R.choice([80, 80, 100, 50])})
    for _ in range(n(120)):
        a = R.choice([pc, mob])(); t = rand_target(); sure_hit(a, t); never_crit(a)
        if a["kind"] == "pc" and R.random() < 0.5: a["wtype1"] = BOW; a["arrow"] = True; a["ammo"] = 1
        add("map_gvg_rates", a, t, mp="gvg", cfg={"gvg_short_damage_rate": R.choice([80, 1, 10, 0]), "gvg_long_damage_rate": R.choice([80, 1, 10, 0])})
    for _ in range(n(60)):  # mob target damage_taken (mob_db DamageTaken): max(damage * pct / 100, 1)
        a = R.choice([pc, mob])(); t = mob(damagetaken=R.choice([1, 10, 50, 100, 150, 400])); sure_hit(a, t); never_crit(a); add("damagetaken", a, t)

    # --- hit / flee / miss paths
    for _ in range(n(250)):
        a = R.choice([pc, mob])(); t = rand_target(); a["hit"] = R.randint(0, 250); t["flee"] = R.randint(0, 400); never_crit(a)
        add("hit_flee_sweep", a, t)
    for _ in range(n(100)):  # hit rate bounds 5..100 and their toggles
        a = R.choice([pc, mob])(); t = rand_target(); never_crit(a)
        a["hit"] = R.choice([0, 20, 400]); t["flee"] = R.choice([0, 150, 500])
        add("hit_rate_clamp", a, t, cfg={"min_hitrate": R.choice([5, 5, 0, 30]), "max_hitrate": R.choice([100, 100, 80, 95])})
    for _ in range(n(100)):  # Vulture's Eye and Weaponry Research add to the hit rate in renewal
        a = pc(); a["skills"] = {"vulture": R.randint(0, 10), "weapon_research": R.randint(0, 10)}
        if R.random() < 0.5: a["wtype1"] = BOW; a["arrow"] = True; a["ammo"] = 1
        t = rand_target(); never_crit(a); a["hit"] = R.randint(0, 200); t["flee"] = R.randint(0, 300); add("hit_skills", a, t)
    for _ in range(n(120)):  # lucky dodge
        a = R.choice([pc, mob])(); t = rand_target(); t["flee2"] = R.choice([10, 50, 100, 300, 900, 1000]); never_crit(a)
        sure_hit(a, t); t["flee2"] = R.choice([10, 50, 100, 300, 900, 1000]); add("lucky_dodge", a, t)
    for _ in range(n(60)):  # perfect hit
        a = pc(); t = rand_target(); a["hit"] = 0; t["flee"] = 500; never_crit(a)
        a.setdefault("bonus", {})["perfect_hit"] = R.choice([10, 50, 99]); add("hit_perfect_hit", a, t)
    for _ in range(n(60)):  # miss paths forced
        a = R.choice([pc, mob])(); t = rand_target(); a["hit"] = 0; t["flee"] = 500; never_crit(a); add("miss_forced", a, t)
    for _ in range(n(60)):  # zero attack: total 0 -> MISS flagged, div 1
        a = R.choice([mob, pc])(); a["rhw"].update(atk=0, atk2=0); a["batk"] = 0; a["eatk"] = 0; a["patk"] = 0
        if a["kind"] == "pc": a["rhw"]["wlv"] = 0
        t = rand_target(); sure_hit(a, t); never_crit(a); add("zero_atk", a, t)

    # --- ranged monsters (rhw.range > 3 makes a basic attack ranged: BF_LONG, long rates and defenses)
    for _ in range(n(120)):
        a = mob(); a["rhw"]["range"] = R.choice([1, 2, 3, 4, 9]); t = rand_target(); sure_hit(a, t); never_crit(a)
        if t["kind"] == "pc": random_cards(mob(), t, True)
        t["sc"] = {k: {} for k in R.sample(["adjustment", "smokepowder", "kaite"], R.randint(0, 2))}
        add("mob_ranged", a, t, mp=R.choice(["normal", "gvg"]))

    # --- worked examples of BR_RULESET.md section 7 for the renewal branch
    t = mob(size=1, race=0, element=0, ele_lv=1)
    t.update({"def": 0, "def2": 0, "flee": 0, "res": 0}); t.pop("flee2", None)
    a = exact(); add("worked_example", a, dict(t))                                            # control: 1000 passes through
    a = exact(); a["rhw"]["ele"] = 1; a["sc"] = {"deluge": {"val3": 15}}                      # E-1 RE: ratio 150 + 15 -> 1650
    tt = dict(t); tt["element"] = 3; add("worked_example", a, tt)
    a = exact(); tt = dict(t); tt["res"] = 100; add("worked_example", a, tt)                   # E-2: RES 100 -> 840
    a = exact(); a["bonus"] = {"ignore_res": {"0": 50}}; tt = dict(t); tt["res"] = 100; add("worked_example", a, tt)  # E-2: ignoring 50% -> 912
    a = exact(); a["bonus"] = {"add_race": {"0": 20}, "add_size": {"1": 15}}; add("worked_example", a, dict(t))  # E-3 on the weapon part -> 1380
    a = pc(wtype1=2, batk=0, eatk=0, patk=0, crate=0, str=80, dex=1, cri=0, hit=500); a["rhw"] = {"atk": 100, "atk2": 0, "ele": 0, "wlv": 4}
    add("worked_example", a, dict(t), seed=11)                                                 # E-9: weapon ATK 100, lv 4, STR 80 -> [120, 160]
    a = pc(wtype1=2, batk=0, eatk=0, patk=0, crate=0, str=80, dex=1, cri=1000, hit=500); a["rhw"] = {"atk": 100, "atk2": 0, "ele": 0, "wlv": 4}
    add("worked_example", a, dict(t), seed=12)                                                 # E-9 crit: top of the range
    t.update({"luk": 0})
    a = exact(crate=0); a["cri"] = 1000; add("worked_example", a, dict(t), seed=13)           # E-crit: 1000 x 1.4 = 1400 for a crate 0 player
    a = exact(crate=37); a["cri"] = 1000; add("worked_example", a, dict(t), seed=13)          # E-crit: 1000 x (1.4 + 0.37) = 1770

    # --- interaction cases added after a mutation check showed the ordering of these stages was not pinned by the cases above
    for _ in range(n(150)):  # crit_atk_rate / non_crit_atk_rate / short / long rates together with a skill ratio != 100
        a = pc(); b = a.setdefault("bonus", {})
        b["crit_atk_rate"] = R.choice([20, 40, 75]); b["non_crit_atk_rate"] = R.choice([0, 15, 30]); b["short_atk"] = R.choice([0, 10]); b["skill_ratio"] = R.choice([0, 25, -20])
        a["sc"] = {"provoke": {"val2": R.choice([5, 14])}, "overthrust": {"val3": R.choice([5, 25])}}
        if R.random() < 0.5: a["sc"]["berserk"] = {}
        t = rand_target(); sure_hit(a, t)
        if R.random() < 0.6:
            a["cri"] = 1000; t["luk"] = 0
        else:
            never_crit(a)
        add("rates_with_ratio", a, t)
    for _ in range(n(120)):  # monster crit with the element table active: the x1.4f comes after the table
        a = mob(); a["cri"] = 1000; t = mob(element=R.randint(0, 9), ele_lv=R.randint(1, 4)); t["luk"] = 0; a["rhw"]["ele"] = R.randint(0, 9)
        sure_hit(a, t); a["cri"] = 1000
        add("element_mob_crit", a, t, cfg={"attack_attr_none": R.choice([0, 0, 12])})
    for _ in range(n(80)):  # player crit against a target whose element table interacts with the part ratios (Ghost / Undead rows)
        a = pc(crate=R.randint(0, 60)); a["cri"] = 1000; a["rhw"]["ele"] = R.randint(0, 9)
        t = mob(element=R.choice([8, 9, 6, 7]), ele_lv=R.randint(1, 4)); t["luk"] = 0; sure_hit(a, t); a["cri"] = 1000
        add("element_pc_crit", a, t)
    for _ in range(n(100)):  # ATK% extremes: floor at 0 and the 65535 cap
        a = R.choice([pc, mob])(); t = rand_target(); sure_hit(a, t); never_crit(a)
        a["sc"] = {"provoke": {"val2": R.choice([70000, 66000, 65500, -200])}, "concentration": {"val2": R.choice([0, 30, -50])}}
        add("status_atk_cap", a, t)
    for _ in range(n(100)):  # mastery ATK against a card-wearing player (target cards without the element step), status ATK too
        a = pc(wtype1=R.choice([1, 2, 3, 4, 6, 8]), batk=R.randint(100, 2000)); a["skills"] = {k: R.randint(1, 10) for k in ["sm_sword", "sm_twohand", "mace", "spear", "axe", "weapon_research"]}
        t = target_pc(); sure_hit(a, t); never_crit(a); random_cards(a, t, True)
        add("cards_mastery_status", a, t)


def run_server(server, cases, out_dir):
    kv = out_dir / "cases.kv"; out = out_dir / "cases.out"; dump = out_dir / "dump.txt"
    kv.write_text("\n".join(case_tokens(c) for c in cases) + "\n")
    env = dict(os.environ, RA_GOLDEN_CASES=str(kv), RA_GOLDEN_OUT=str(out), RA_GOLDEN_DUMP=str(dump))
    r = subprocess.run([str(server / "map-server"), "--run-once"], cwd=server, env=env, capture_output=True, text=True, timeout=3600)
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
    ap.add_argument("--server", required=True, help="<build>/rA of the patched RENEWAL copy")
    ap.add_argument("--out", default=str(pathlib.Path(__file__).resolve().parents[1] / "golden" / "weapon_re.jsonl.gz"))
    ap.add_argument("--scale", type=float, default=1.0)
    ap.add_argument("--seed", type=int, default=SEED)
    ap.add_argument("--dump", help="also write the table/config dump of the binary here")
    args = ap.parse_args()
    reseed(args.seed)
    build(args.scale)
    with tempfile.TemporaryDirectory() as d:
        lines, dump = run_server(pathlib.Path(args.server), CASES, pathlib.Path(d))
    if args.dump: pathlib.Path(args.dump).write_text(dump)
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
