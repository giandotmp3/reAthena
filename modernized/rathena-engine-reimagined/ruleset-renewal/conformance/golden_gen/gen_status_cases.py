#!/usr/bin/env python3
"""Generates the RENEWAL status-calculation golden cases and records the REAL legacy result of each (the status_calc_* functions of status.cpp).

  gen_status_cases.py --server <build>/rA [--repo <legacy checkout>] [--out conformance/golden/status_re.jsonl.gz] [--seed N] [--scale 1.0]

A case is one synthetic unit (player / monster / homunculus with explicit facts) with an explicit set of active statuses {SC_NAME: [val1..val4]} and a
list of function inputs; the patched map-server (golden_status_hook.inc) calls the real static functions and the output of every function is recorded.
Which statuses a function reads is not typed in: enum_statuses.py parses status.cpp (the RENEWAL branches) and the cases are generated per
(status, function) pair from that list. The generator is deterministic (fixed seed, fixed gzip mtime): build.sh regenerates the file byte-identically.

Categories:  single   every referenced status x unit kind x ten val patterns (a level series, hot values taken from the conditions in the source,
                      random), every function that reads it
             pair     every two statuses that one statement of the source reads together (all val combinations of the hot values), then a random
                      sample of status pairs per function (stacking and order)
             override the statuses that `return` early (Berserk, Harmonize, Curse, Keeping, Defset, ...) against random other statuses of the function
             combo    random sets of 2..12 statuses on random units, every function they read
             extreme  wide inputs (negative, beyond the clamps) with one or two statuses
             facts    unit facts: mounts, skills, items, map penalty, cart, Madogear, Free Cast, maximum HP / SP / AP bonuses
             states   status_calc_state: every one of the 1,025 sc_type values alone on a player and on a monster, plus random sets
"""
import argparse, gzip, json, os, random, re, subprocess, sys, tempfile, pathlib

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import enum_statuses  # noqa: E402

SEED = 20261019

# function keys of the hook, in output order; `aspd` of the source is two functions (fixed / percentage) in the hook
HOOK_FNS = ["str", "agi", "vit", "int", "dex", "luk", "pow", "sta", "wis", "spl", "con", "crt", "batk", "watk", "matk_flat", "cri", "hit", "flee",
            "flee2", "def", "def2", "mdef", "mdef2", "speed", "aspd_fixed", "aspd_pct", "fix_aspd", "aspd_rate", "patk", "smatk", "res", "mres",
            "hplus", "crate", "maxhp", "maxsp", "maxap", "def_ele", "ele_lv", "atk_ele", "mode"]
NO_INPUT = {"aspd_fixed", "aspd_pct"}
# typical and wide ranges of the input value of each function (inclusive)
RANGE = {"stat": ((0, 150), (-40, 30000)), "batk": ((0, 2000), (-200, 30000)), "watk": ((0, 3000), (-200, 30000)), "matk_flat": ((0, 2000), (-100, 30000)),
         "cri": ((10, 250), (-100, 3000)), "hit": ((0, 400), (-100, 6000)), "flee": ((0, 500), (-100, 6000)), "flee2": ((0, 200), (-100, 3000)),
         "def": ((0, 150), (-400, 3000)), "def2": ((-20, 200), (-400, 3000)), "mdef": ((0, 100), (-200, 3000)), "mdef2": ((-20, 200), (-400, 3000)),
         "speed": ((50, 250), (1, 2000)), "fix_aspd": ((200, 1500), (-100, 9000)), "aspd_rate": ((700, 1100), (-100, 3000)),
         "small": ((0, 150), (-50, 3000)), "maxhp": ((1, 500000), (1, 4000000000)), "maxsp": ((1, 50000), (1, 4000000000)), "maxap": ((0, 1000), (0, 4000000000)),
         "ele": ((0, 9), (-5, 300)), "ele_lv": ((1, 4), (-3, 300)), "mode": ((0, 0x7FFFF), (0, 0x7FFFFFF))}
STAT_FNS = {"str", "agi", "vit", "int", "dex", "luk", "pow", "sta", "wis", "spl", "con", "crt"}
SMALL_FNS = {"patk", "smatk", "res", "mres", "hplus", "crate"}


def range_of(fn, wide):
    k = "stat" if fn in STAT_FNS else "small" if fn in SMALL_FNS else "ele" if fn in ("def_ele", "atk_ele") else fn
    return RANGE[k][1 if wide else 0]


def expand_fns(fns):
    out = []
    for f in fns:
        out += ["aspd_fixed", "aspd_pct"] if f == "aspd" else [f]
    return [f for f in HOOK_FNS if f in set(out)]


def sc_fns_from(enum):
    by_sc = {}
    for fn, e in enum["functions"].items():
        for sc, lines in e["statuses"].items():
            by_sc.setdefault(sc, {}).setdefault(fn, []).extend(lines)
    return by_sc


def parse_dump(text):
    consts, sc = {}, {}
    for l in text.splitlines():
        p = l.split()
        if p[0] == "const": consts[p[1]] = int(p[2])
        elif p[0] == "sc": sc[p[1]] = int(p[2])
    return consts, sc


class Gen:
    def __init__(self, repo, consts, sc_ids, seed, scale):
        self.R = random.Random(seed)
        self.scale = scale
        self.consts, self.sc_ids = consts, sc_ids
        self.enum = enum_statuses.enumerate_statuses(repo)
        self.src = (pathlib.Path(repo) / "src/map/status.cpp").read_text().split("\n")
        self.by_sc = sc_fns_from(self.enum)
        self.statuses = sorted(self.by_sc)
        self.fn_statuses = {}
        for sc, fns in self.by_sc.items():
            for fn in fns:
                for hf in expand_fns([fn]):
                    self.fn_statuses.setdefault(hf, []).append(sc)
        self.enum_order = {n: i for i, n in enumerate(sc_ids)}  # the dump lists the table in the order of enum sc_type
        self.cases = []
        self.hot = self.hot_values()
        self.override = self.override_statuses()

    # ---- what the source says about each status ---------------------------------------------------------------------------------------------
    def site_text(self, ln):
        t = self.src[ln - 1]
        if not t.rstrip().endswith((";", "}")) and ln < len(self.src):
            t += " " + self.src[ln]
        return t

    def hot_values(self):
        """per status and slot: the values the conditions of the source compare the field with (valN == X, valN > 0, val2 & BREAK_X, ...)"""
        hot = {}
        pat = re.compile(r'(SC_[A-Z0-9_]+)\)\s*->\s*val([1-4])\s*(==|!=|>=|<=|>|<|&)\s*(\(?[A-Za-z0-9_|() ]+?\)?)\s*(?:[)&|;?]|$)')
        for sc, fns in self.by_sc.items():
            for fn, lines in fns.items():
                for ln in lines:
                    for m in pat.finditer(self.site_text(ln)):
                        name, slot, op, rhs = m.group(1), int(m.group(2)), m.group(3), m.group(4).strip("() ")
                        vals = set()
                        parts = [x.strip() for x in rhs.split("|")]
                        for part in parts:
                            v = self.consts.get(part)
                            if v is None and re.fullmatch(r"-?\d+", part): v = int(part)
                            if v is not None: vals.add(v)
                        if op == "&" and len(parts) > 1:
                            vals.add(sum(self.consts.get(x, 0) for x in parts))
                        for v in list(vals):
                            vals |= {v, v + 1, v - 1} if op in (">", "<", ">=", "<=") else {v}
                        if op == "&": vals |= {0}
                        if vals: hot.setdefault(sc, {}).setdefault(slot, set()).update(vals)
        return {sc: {s: sorted(v) for s, v in d.items()} for sc, d in hot.items()}

    def override_statuses(self):
        out = {}
        for sc, fns in self.by_sc.items():
            for fn, lines in fns.items():
                for ln in lines:
                    if re.search(r'\breturn\b', self.site_text(ln)):
                        out.setdefault(fn, set()).add(sc)
        return {fn: sorted(v) for fn, v in out.items()}

    # ---- random pieces ---------------------------------------------------------------------------------------------------------------------
    def val(self, sc, slot, big=False, small=False):
        R = self.R
        hot = self.hot.get(sc, {}).get(slot)
        if hot and R.random() < 0.4: return R.choice(hot)
        x = R.random()
        if x < 0.14: return 0
        if x < 0.24: return 1
        if x < 0.50: return R.randint(2, 12)
        if x < 0.74: return R.randint(13, 120)
        if x < 0.82: return R.randint(121, 300) if not small else R.randint(13, 120)
        if x < 0.90: return -R.randint(1, 12)
        if x < 0.96 and sc in PACKED and slot in PACKED[sc]:  # packed byte triplets (Marionette, Spirit): also negative ones (arithmetic shift of an int)
            return R.randint(0, 0xFFFFFF) if R.random() < 0.7 else -R.randint(1, 0x7FFFFFFF)
        return R.randint(0, 100) if not big else R.choice([255, 1000, 4095, 20000, 65535])

    def vals(self, sc, big=False, small=False):
        return [self.val(sc, s, big, small) for s in (1, 2, 3, 4)]

    def base(self, fn, wide=False, safe=True):
        lo, hi = range_of(fn, wide)
        R = self.R
        if fn == "mode":
            v = 0
            for b in R.sample(range(19), R.randint(0, 6)): v |= 1 << b
            return v
        if not wide and R.random() < 0.5: hi = min(hi, 60 if fn not in ("speed", "maxhp", "maxsp", "maxap", "fix_aspd", "aspd_rate") else hi)
        return R.randint(lo, hi)

    def unit(self, kind, rich=False):
        R = self.R
        u = {"kind": kind, "id": {"pc": 150000, "mob": 200000, "hom": 250000}[kind], "vit": R.choice([0, 1, 9, 30, 99, 150, 255])}
        if kind == "pc":
            # battle.conf values the functions read: ALWAYS explicit (the hook would otherwise keep the server's defaults, 20 / 20 / ...)
            u["cfg"] = {"gvg_flee_penalty": R.choice([0, 20, 50]), "bg_flee_penalty": R.choice([0, 20, 50]),
                        "rental_mount_speed_boost": R.choice([0, 25, 25, 40])}
            u["sex"] = R.randint(0, 1)
            u["weapon"] = R.choice([0, 1, 2, 11, 12, 17, 18, 19, 20, 21])
            u["class_"] = R.choice([0, 0, self.consts["MAPID_ASSASSIN"], self.consts["MAPID_ASSASSIN"] + (1 << 24)])
            if rich or R.random() < 0.25:
                u["option"] = R.choice([0, 0, self.consts["OPTION_RIDING"], self.consts["OPTION_WUGRIDER"], self.consts["OPTION_MADOGEAR"],
                                        self.consts["OPTION_DRAGON"], self.consts["OPTION_RIDING"] | self.consts["OPTION_MADOGEAR"]])
                u["perm_speed"] = int(R.random() < 0.08)
                u["skilltimer"] = int(R.random() < 0.25)
                u["ud_skill_id"] = R.choice([0, self.consts["LG_EXEEDBREAK"], 28])
                u["ud_skill_lv"] = R.randint(0, 5)
                u["bonus_speed_rate"] = R.choice([0, 0, 10, 25, -10, -30])
                u["bonus_speed_add_rate"] = R.choice([0, 0, 5, -5])
                u["map"] = R.choice([0, 0, 1, 2])
                u["bonus_hp"] = R.choice([0, 0, 500, 5000, -300]); u["bonus_sp"] = R.choice([0, 0, 50, 300, -20]); u["bonus_ap"] = R.choice([0, 0, 40, 100])
                skills = {}
                for name in R.sample(PC_SKILLS, R.randint(0, 7)):
                    skills[name] = R.choice([1, 2, 3, 4, 5, 5, 6, 8, 10])
                if R.random() < 0.12:
                    lv = R.choice([1, 2, 3, 3, 4, 40])
                    for name in SEA_SKILLS: skills[name] = lv
                u["skills"] = skills
        if R.random() < 0.04 and kind == "pc": u["nosc"] = 1
        return u

    def add(self, cat, unit, sc, fns, wide=False, extra_in=None, states=False):
        R = self.R
        inp = {}
        for f in fns:
            inp[f] = 0 if f in NO_INPUT else self.base(f, wide)
        if extra_in: inp.update(extra_in)
        case = {"id": len(self.cases) + 1, "cat": cat, "unit": unit, "sc": sc, "in": inp}
        if states: case["states"] = 1
        self.cases.append(case)

    def kinds_for(self, sc, fns):
        return ["pc", "mob", "hom"]

    # ---- the categories ------------------------------------------------------------------------------------------------------------------------
    def build(self):
        R = self.R
        n = lambda x: max(1, int(x * self.scale))
        # single: a level series (val1 = 1..10) for the first ten patterns, hot / random values for the rest
        for sc in self.statuses:
            fns = expand_fns(self.by_sc[sc].keys())
            for kind in ("pc", "mob", "hom"):
                for k in range(n(10)):
                    v = self.vals(sc)
                    if k < 4: v[0] = [1, 5, 10, 3][k]
                    self.add("single", self.unit(kind, rich=(k == 0)), {sc: v}, fns)
        # pair: statuses read by one statement, then a sample per function
        for ln_text in self.same_site_pairs():
            a, b = ln_text
            fns = expand_fns(list(self.by_sc[a].keys()) + list(self.by_sc[b].keys()))
            fns = [f for f in fns if f in set(expand_fns(self.by_sc[a].keys())) & set(expand_fns(self.by_sc[b].keys()))] or fns
            for k in range(n(6)):
                kind = R.choice(["pc", "mob", "hom"])
                self.add("pair_site", self.unit(kind, rich=True), {a: self.vals(a, small=True), b: self.vals(b, small=True)}, fns)
        for fn in HOOK_FNS:
            pool = self.fn_statuses.get(fn, [])
            if len(pool) < 2: continue
            for k in range(n(min(140, len(pool) * (len(pool) - 1) // 2))):
                a, b = R.sample(pool, 2)
                kind = R.choice(["pc", "mob", "hom"])
                self.add("pair", self.unit(kind, rich=True), {a: self.vals(a, small=True), b: self.vals(b, small=True)}, [fn])
        for fn, ovr in self.override.items():
            for hf in expand_fns([fn]):
                pool = self.fn_statuses.get(hf, [])
                for o in ovr:
                    for k in range(n(8)):
                        others = R.sample([p for p in pool if p != o], min(len(pool) - 1, R.randint(0, 4)))
                        sc = {o: self.vals(o, small=True)}
                        for p in others: sc[p] = self.vals(p, small=True)
                        self.add("override", self.unit(R.choice(["pc", "mob", "hom"]), rich=True), sc, [hf])
        for k in range(n(3600)):
            cnt = R.randint(2, 12)
            sc = {}
            for s in R.sample(self.statuses, cnt): sc[s] = self.vals(s, small=True)
            fns = sorted({hf for s in sc for hf in expand_fns(self.by_sc[s].keys())}, key=HOOK_FNS.index)
            self.add("combo", self.unit(R.choice(["pc", "mob", "hom"]), rich=R.random() < 0.5), sc, fns)
        for k in range(n(2200)):
            cnt = R.randint(1, 2)
            sc = {}
            for s in R.sample(self.statuses, cnt): sc[s] = self.vals(s, big=True)
            fns = sorted({hf for s in sc for hf in expand_fns(self.by_sc[s].keys())}, key=HOOK_FNS.index)
            self.add("extreme", self.unit(R.choice(["pc", "mob", "hom"])), sc, fns, wide=True)
        for fn in ("speed", "flee", "maxhp", "maxsp", "maxap", "aspd_pct", "aspd_fixed", "aspd_rate", "matk_flat", "def2"):
            pool = self.fn_statuses.get(fn, [])
            for k in range(n(380)):
                sc = {}
                for s in R.sample(pool, min(len(pool), R.randint(0, 4))): sc[s] = self.vals(s, small=True)
                extra = ["maxhp", "maxsp"] if fn.startswith("max") and fn != "maxap" else []
                self.add("facts", self.unit("pc" if R.random() < 0.85 else R.choice(["mob", "hom"]), rich=True), sc, expand_fns([fn] + extra), wide=R.random() < 0.15)
        # targeted: the unit-fact paths that random sampling reaches rarely (cart with Pushcart, mounts with the matching skills, item speed, free cast)
        for k in range(n(500)):
            u = self.unit("pc", rich=True)
            u["perm_speed"] = 0; u["nosc"] = 0; u.pop("nosc", None)
            u["option"] = R.choice([0, 0, self.consts["OPTION_RIDING"], self.consts["OPTION_WUGRIDER"], self.consts["OPTION_DRAGON"]])
            u["bonus_speed_rate"] = R.choice([0, 30, -30, 60, -60]); u["bonus_speed_add_rate"] = R.choice([0, 10, -10])
            u["skills"] = dict(u.get("skills", {}), MC_PUSHCART=R.randint(0, 10), RA_WUGRIDER=R.randint(0, 5), SA_FREECAST=R.choice([0, 0, 3, 10]),
                               TF_MISS=R.randint(0, 10), RG_TUNNELDRIVE=R.randint(0, 10), BA_MUSICALLESSON=R.randint(0, 10), DC_DANCINGLESSON=R.randint(0, 10))
            sc = {"SC_PUSH_CART": [R.randint(0, 3), 0, 0, 0]}
            for s_ in R.sample(["SC_INCREASEAGI", "SC_QUAGMIRE", "SC_HIDING", "SC_DANCING", "SC_SPIRIT", "SC_ALL_RIDING", "SC_FUSION", "SC_STEELBODY", "SC_DEFENDER", "SC_WALKSPEED",
                                "SC_PARALYSE", "SC_CHASEWALK", "SC_CLOAKING", "SC_MARSHOFABYSS", "SC_ACCELERATION", "SC_AGIUP", "SC_CURSE"], R.randint(0, 3)):
                sc[s_] = self.vals(s_, small=True)
            if R.random() < 0.5: sc.pop("SC_PUSH_CART")
            self.add("speed_facts", u, sc, ["speed"])
        # worked examples: E-4b of BR_RULESET.md (speed 187) and the semantic points the acceptance tests pin by name (tests/acceptance/status_golden_test.luau)
        for entry in WORKED:
            name, kind, sc, inp = entry[:4]
            u = self.unit(kind)
            u.update({"option": 0, "perm_speed": 0, "skilltimer": 0, "bonus_speed_rate": 0, "bonus_speed_add_rate": 0, "bonus_hp": 0, "bonus_sp": 0, "bonus_ap": 0,
                      "map": 0, "skills": {}})
            u.pop("nosc", None)
            if kind == "pc": u["class_"] = 0
            u["vit"] = 0
            u.update(entry[4] if len(entry) > 4 else {})
            inp = dict(inp)
            want_states = inp.pop("states", None) is not None
            self.add("worked", u, sc, list(inp), extra_in=inp, states=want_states)
            self.cases[-1]["note"] = name
        # clamp: inputs at the edges of every function's range (the cap_value() bounds, the 16-bit wrap points), bare and with one status
        edge = [-70000, -32769, -32768, -1, 0, 1, 9, 10, 11, 255, 256, 32766, 32767, 32768, 65534, 65535, 65536, 70000, 2000000000]
        for fn in HOOK_FNS:
            if fn in NO_INPUT: continue
            pool = self.fn_statuses.get(fn, [])
            for k in range(n(24)):
                sc = {}
                if pool and k % 3 != 0:
                    st = R.choice(pool)
                    sc[st] = self.vals(st, small=True)
                base = R.choice(edge if not sc else edge[:-1])  # 2e9 only bare: a percentage of it overflows int32 (undefined behaviour in the legacy C++)
                if fn in ("maxhp", "maxsp", "maxap"): base = abs(base)
                self.add("clamp", self.unit(R.choice(["pc", "mob", "hom"])), sc, [fn], extra_in={fn: base})
        # wrap: maxhp / maxsp / maxap are uint64 sums: a negative fixed bonus larger than the base wraps around 2^64 and the cap makes it UINT_MAX
        for fn, neg in (("maxhp", ["SC_MARIONETTE"]), ("maxsp", []), ("maxap", [])):
            rates = [x for x in self.fn_statuses.get(fn, []) if x not in neg]
            for k in range(n(160)):
                u = self.unit("pc" if fn != "maxhp" or R.random() < 0.7 else "mob", rich=True)
                if u["kind"] == "pc": u["bonus_" + fn[3:]] = -R.choice([1, 50, 300, 1000, 4000, 100000])
                sc = {}
                if neg and R.random() < 0.7: sc[neg[0]] = self.vals(neg[0], small=True)
                for r in R.sample(rates, min(len(rates), R.randint(0, 2))): sc[r] = self.vals(r, small=True)
                self.add("wrap", u, sc, [fn], extra_in={fn: R.choice([0, 1, 2, 7, 99, 500, 999, 1000, 1001, 2500, 40000])})
        # states: every sc_type alone (player and monster, three val patterns), then random sets
        for name in self.canonical_sc():
            for kind in ("pc", "mob"):
                for k in range(n(3)):
                    v = self.state_vals(name)
                    self.add("states", self.unit(kind, rich=(k == 0)), {name: v}, [], states=True)
        for k in range(n(600)):
            sc = {}
            for s in R.sample(self.canonical_sc(), R.randint(2, 8)): sc[s] = self.state_vals(s)
            self.add("states_combo", self.unit(R.choice(["pc", "mob"]), rich=True), sc, [], states=True)
        return self.cases

    def canonical_sc(self):
        """one name per sc_type value (status.hpp has aliases such as SC_COMMON_MIN = SC_STONE); the first name in enum order"""
        seen, out = set(), []
        for name, v in sorted(self.sc_ids.items(), key=lambda kv: (kv[1], self.enum_order.get(kv[0], 1 << 30))):
            if v not in seen:
                seen.add(v); out.append(name)
        return sorted(out)

    def state_vals(self, name):
        R = self.R
        hot = STATE_HOT.get(name)
        if hot and R.random() < 0.7:
            return [R.choice(hot.get(i, [0, 1, 2])) for i in (1, 2, 3, 4)]
        return [R.choice([0, 1, 2, 3, 5, 150000, 200000, 250000, 131072, 65536]) for _ in range(4)]

    def same_site_pairs(self):
        """pairs of statuses that one statement of the source (or its immediate continuation) reads"""
        pairs = set()
        for fn, e in self.enum["functions"].items():
            by_line = {}
            for sc, lines in e["statuses"].items():
                for ln in lines: by_line.setdefault(ln, set()).add(sc)
            for ln, scs in by_line.items():
                scs = sorted(scs)
                for i in range(len(scs)):
                    for j in range(i + 1, len(scs)):
                        pairs.add((scs[i], scs[j]))
        return sorted(pairs)


# (name, unit kind, statuses, inputs): inputs not produced by the random categories because the acceptance tests pin their exact results
WORKED = [
    ("E-4b speed 150 under Quagmire + Increase AGI", "pc", {"SC_QUAGMIRE": [1, 10, 0, 0], "SC_INCREASEAGI": [1, 12, 0, 0]}, {"speed": 150}),
    ("E-4b speed 150 under Quagmire + Increase AGI", "mob", {"SC_QUAGMIRE": [1, 10, 0, 0], "SC_INCREASEAGI": [1, 12, 0, 0]}, {"speed": 150}),
    ("E-4b speed 150 under Quagmire + Increase AGI", "hom", {"SC_QUAGMIRE": [1, 10, 0, 0], "SC_INCREASEAGI": [1, 12, 0, 0]}, {"speed": 150}),
    ("speed haste group is a maximum, not a sum", "mob", {"SC_INCREASEAGI": [10, 12, 0, 0], "SC_BERSERK": [1, 0, 0, 0], "SC_RUN": [1, 0, 0, 0], "SC_WINDWALK": [10, 0, 0, 0]}, {"speed": 150}),
    ("speed rate floor 40 percent", "mob", {"SC_AGIUP": [80, 0, 0, 0]}, {"speed": 150}),
    ("Marsh of Abyss caps the slow at 150", "mob", {"SC_MARSHOFABYSS": [1, 10, 200, 0], "SC_CURSE": [1, 0, 0, 0]}, {"speed": 150}),
    ("Steel Body speed 200", "mob", {"SC_STEELBODY": [1, 0, 0, 0], "SC_INCREASEAGI": [1, 12, 0, 0]}, {"speed": 150}),
    ("Harmonize returns before everything", "mob", {"SC_HARMONIZE": [1, 10, 0, 0], "SC_BLESSING": [10, 5, 0, 0], "SC_CHEERUP": [1, 0, 0, 0]}, {"str": 100, "agi": 100}),
    ("Blessing adds its val2", "mob", {"SC_BLESSING": [10, 7, 0, 0]}, {"str": 80, "int": 80, "dex": 80, "hit": 100}),
    ("Blessing halves the stat when val2 is 0", "mob", {"SC_BLESSING": [10, 0, 0, 0]}, {"str": 81, "int": 80, "dex": 80, "hit": 100}),
    ("Curse luk 0 even with Gloria", "mob", {"SC_CURSE": [1, 0, 0, 0], "SC_GLORIA": [1, 0, 0, 0]}, {"luk": 60}),
    ("stat order: add, then a rate term of the sum", "mob", {"SC_CHEERUP": [1, 0, 0, 0], "SC_FULL_THROTTLE": [1, 0, 20, 0]}, {"str": 100}),
    ("Berserk and Eternal Chaos zero the defense", "mob", {"SC_BERSERK": [1, 0, 0, 0], "SC_PROVOKE": [10, 0, 50, 0]}, {"def": 50, "def2": 30, "mdef": 20, "mdef2": 40}),
    ("Steel Body gives no DEF in renewal", "mob", {"SC_STEELBODY": [1, 0, 0, 0]}, {"def": 50, "mdef": 20}),
    ("Provoke does not alter a player's DEF", "pc", {"SC_PROVOKE": [10, 0, 50, 0]}, {"def": 50, "def2": 30}),
    ("Provoke lowers a monster's DEF", "mob", {"SC_PROVOKE": [10, 0, 50, 0]}, {"def": 50, "def2": 30}),
    ("Defset above the int16 range wraps", "mob", {"SC_DEFSET": [33000, 0, 0, 0]}, {"def": 50, "def2": 30}),
    ("negative soft DEF is kept in renewal", "mob", {"SC_SKA": [1, 0, 0, 0]}, {"def2": -200}),
    ("ASPD: best of the quicken family", "pc", {"SC_TWOHANDQUICKEN": [10, 300, 0, 0], "SC_ADRENALINE2": [5, 0, 300, 0], "SC_FLEET": [5, 100, 0, 0]}, {"aspd_fixed": 0}),
    ("ASPD: Quagmire voids the quicken family", "pc", {"SC_TWOHANDQUICKEN": [10, 300, 0, 0], "SC_QUAGMIRE": [5, 10, 0, 0]}, {"aspd_fixed": 0}),
    ("ASPD: only the best potion adds", "pc", {"SC_ASPDPOTION3": [9, 150, 0, 0], "SC_ASPDPOTION2": [7, 100, 0, 0]}, {"aspd_fixed": 0}),
    ("ASPD: Berserk 15 after the quicken family", "pc", {"SC_ADRENALINE": [5, 1, 300, 0], "SC_BERSERK": [1, 0, 0, 0]}, {"aspd_fixed": 0}),
    ("maxhp: fixed bonus first, then the rate", "mob", {"SC_INCMHP": [500, 0, 0, 0], "SC_INCMHPRATE": [20, 0, 0, 0]}, {"maxhp": 10000}),
    ("maxhp: a negative fixed bonus wraps around 2^64 and is capped", "mob", {"SC_MARIONETTE": [1, 0, 0, 0]}, {"maxhp": 500}),
    ("flee: Poison Mist returns 0, Overed Boost returns its value", "mob", {"SC_POISON_MIST": [1, 0, 0, 0], "SC_OVERED_BOOST": [1, 77, 0, 0]}, {"flee": 200}),
    ("flee: Overed Boost is final", "mob", {"SC_OVERED_BOOST": [1, 77, 0, 0], "SC_INCFLEE": [30, 0, 0, 0], "SC_BERSERK": [1, 0, 0, 0]}, {"flee": 200}),
    ("flee: fixed terms before the rate terms", "mob", {"SC_INCFLEE": [30, 0, 0, 0], "SC_BERSERK": [1, 0, 0, 0], "SC_BLIND": [1, 0, 0, 0]}, {"flee": 200}),
    ("mode: set, add, delete in that order", "mob", {"SC_MODECHANGE": [1, 0x1, 0x6, 0x2]}, {"mode": 0x3008}),
    ("element: Enchant Arms first, then water, earth, fire", "pc", {"SC_ENCHANTARMS": [7, 0, 0, 0], "SC_FIREWEAPON": [1, 0, 0, 0], "SC_WATERWEAPON": [1, 0, 0, 0]}, {"atk_ele": 0}),
    ("element: Freeze sets water level 1", "mob", {"SC_FREEZE": [1, 0, 0, 0]}, {"def_ele": 3, "ele_lv": 4}),
    ("states: Stone restrains moving, casting and attacking", "mob", {"SC_STONE": [1, 0, 0, 0]}, {"states": 1}),
    ("states: Hiding restrains a monster's movement; the item restrictions are for players", "mob", {"SC_HIDING": [1, 0, 0, 0]}, {"states": 1}),
    ("states: Hiding without Tunnel Drive restrains a player's movement and items", "pc", {"SC_HIDING": [1, 0, 0, 0]}, {"states": 1}, {"skills": {}}),
    ("states: Hiding with Tunnel Drive lets a player move", "pc", {"SC_HIDING": [1, 0, 0, 0]}, {"states": 1}, {"skills": {"RG_TUNNELDRIVE": 3}}),
    ("states: Gospel restrains only its caster (BCT_SELF)", "pc", {"SC_GOSPEL": [1, 0, 0, 65536]}, {"states": 1}),
    ("states: Gospel of the enemy side does not restrain movement", "pc", {"SC_GOSPEL": [1, 0, 0, 131072]}, {"states": 1}),
    ("states: an ensemble restrains movement only for Moonlit Serenade and Hermode", "pc", {"SC_DANCING": [395, 0, 0, 7]}, {"states": 1}),
    ("states: an ensemble of another song does not restrain movement", "pc", {"SC_DANCING": [28, 0, 0, 7]}, {"states": 1}),
    ("states: Camouflage below level 3 restrains movement", "pc", {"SC_CAMOUFLAGE": [2, 0, 0, 0]}, {"states": 1}),
    ("states: Camouflage level 3 does not", "pc", {"SC_CAMOUFLAGE": [3, 0, 0, 0]}, {"states": 1}),
    ("states: a manner penalty restrains chat and items of players only", "pc", {"SC_NOCHAT": [9, 0, 0, 0]}, {"states": 1}),
    ("states: a manner penalty on a monster restrains chat only", "mob", {"SC_NOCHAT": [9, 0, 0, 0]}, {"states": 1}),
    ("states: the union of the active statuses", "pc", {"SC_STONE": [1, 0, 0, 0], "SC_WINKCHARM": [1, 0, 0, 0], "SC_FEAR": [1, 1, 0, 0]}, {"states": 1}),
    ("critical: Cloaking doubles the value", "mob", {"SC_CLOAKING": [5, 0, 0, 0], "SC_TRUESIGHT": [10, 40, 20, 0]}, {"cri": 30}),
]

PACKED = {"SC_MARIONETTE": (3, 4), "SC_MARIONETTE2": (3, 4), "SC_SPIRIT": (3, 4), "SC_DANCING": (1,)}
PC_SKILLS = ["NC_MADOLICENCE", "SA_FREECAST", "RA_WUGRIDER", "RG_TUNNELDRIVE", "TF_MISS", "MC_PUSHCART", "BA_MUSICALLESSON", "DC_DANCINGLESSON",
             "RG_PLAGIARISM", "CR_TRUST", "SU_SPRITEMABLE", "SU_POWEROFSEA", "NV_BREAKTHROUGH", "NV_TRANSCENDENCE", "SL_KAINA", "RA_RESEARCHTRAP",
             "WM_LESSON", "HP_MEDITATIO", "HW_SOULDRAIN"]
SEA_SKILLS = ["SU_TUNABELLY", "SU_TUNAPARTY", "SU_BUNCHOFSHRIMP", "SU_FRESHSHRIMP", "SU_GROOMING", "SU_PURRING", "SU_SHRIMPARTY"]
# values that decide the conditional restrictions of status_calc_state (status.cpp:5592-5830), for the targeted cases of the states category
STATE_HOT = {
    "SC_GOSPEL": {4: [65536, 131072, 0]}, "SC_CAMOUFLAGE": {1: [1, 2, 3, 5]}, "SC_MAGNETICFIELD": {2: [0, 1, 150000, 200000, 250000]},
    "SC_FEAR": {2: [0, 1, 5]}, "SC_DANCING": {1: [395, 488, 395 | (3 << 16), 28, 500], 4: [0, 7]}, "SC_OBLIVIONCURSE": {3: [0, 1]},
    "SC_NOCHAT": {1: [0, 1, 8, 9, 2]}, "SC_GRAVITATION": {3: [0, 65536, 131072]},
}


def case_tokens(c):
    u = c["unit"]
    kind = {"pc": 0, "mob": 1, "hom": 2}[u["kind"]]
    t = [f"id={c['id']}", f"bl={kind}", f"vit={u.get('vit', 0)}"]
    for k in ("weapon", "sex", "class_", "option", "perm_speed", "skilltimer", "ud_skill_id", "ud_skill_lv", "bonus_speed_rate", "bonus_speed_add_rate",
              "bonus_hp", "bonus_sp", "bonus_ap", "map", "nosc"):
        if u.get(k): t.append(f"{k}={u[k]}")
    for k, v in sorted(u.get("cfg", {}).items()):
        t.append(f"cfg.{k}={v}")
    for k, v in sorted(u.get("skills", {}).items()):
        t.append(f"skill.{k}={v}")
    for sc, v in sorted(c["sc"].items()):
        t.append(f"sc.{sc}=" + ",".join(str(x) for x in v))
    for f, v in c["in"].items():
        t.append(f"in.{f}={v}")
    if c.get("states"): t.append("in.states=1")
    return " ".join(t)


def run_server(server, cases, work):
    kv, out, dump = work / "cases.kv", work / "cases.out", work / "consts.dump"
    kv.write_text("\n".join(case_tokens(c) for c in cases) + "\n")
    env = dict(os.environ, RA_GOLDEN_ST_CASES=str(kv), RA_GOLDEN_ST_OUT=str(out), RA_GOLDEN_ST_DUMP=str(dump))
    r = subprocess.run([str(server / "map-server"), "--run-once"], cwd=server, env=env, capture_output=True, text=True, timeout=3600)
    if not out.exists():
        sys.exit("status golden hook produced no output:\n" + r.stdout[-2000:] + r.stderr[-2000:])
    return out.read_text().splitlines()


def dump_only(server, work):
    dump = work / "consts.dump"
    env = dict(os.environ, RA_GOLDEN_ST_DUMP=str(dump))
    subprocess.run([str(server / "map-server"), "--run-once"], cwd=server, env=env, capture_output=True, text=True, timeout=3600)
    return dump.read_text()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--server", required=True, help="<build>/rA of the patched RENEWAL copy")
    ap.add_argument("--repo", default=str(HERE.parents[4]), help="legacy checkout (status.cpp is parsed, read-only)")
    ap.add_argument("--out", default=str(HERE.parent / "golden" / "status_re.jsonl.gz"))
    ap.add_argument("--seed", type=int, default=SEED)
    ap.add_argument("--scale", type=float, default=1.0)
    args = ap.parse_args()
    server = pathlib.Path(args.server)
    with tempfile.TemporaryDirectory() as d:
        work = pathlib.Path(d)
        consts, sc_ids = parse_dump(dump_only(server, work))
        gen = Gen(args.repo, consts, sc_ids, args.seed, args.scale)
        cases = gen.build()
        lines = run_server(server, cases, work)
    res = {}
    for ln in lines:
        toks = ln.split()
        rec = dict(t.split("=") for t in toks)
        res[int(rec.pop("id"))] = {k: int(v) for k, v in rec.items()}
    assert len(res) == len(cases), (len(res), len(cases))
    out = pathlib.Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    meta = {"meta": 1, "seed": args.seed, "consts": consts, "sc_ids": sc_ids, "referenced": gen.statuses,
            "functions": {fn: sorted(e["statuses"]) for fn, e in gen.enum["functions"].items()},
            "function_lines": {fn: e["lines"] for fn, e in gen.enum["functions"].items()}, "override": gen.override}
    with open(out, "wb") as raw, gzip.GzipFile(filename="", mode="wb", fileobj=raw, mtime=0, compresslevel=9) as gz:
        gz.write((json.dumps(meta, sort_keys=True, separators=(",", ":")) + "\n").encode())
        for c in cases:
            c = dict(c)
            c["out"] = res[c["id"]]
            gz.write((json.dumps(c, sort_keys=True, separators=(",", ":")) + "\n").encode())
    cats = {}
    for c in cases: cats[c["cat"]] = cats.get(c["cat"], 0) + 1
    print(f"{len(cases)} cases -> {out} ({out.stat().st_size} bytes)")
    for k in sorted(cats): print(f"  {k:14s} {cats[k]}")


if __name__ == "__main__":
    main()
