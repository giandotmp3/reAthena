#!/usr/bin/env python3
"""Generates the pre-renewal STATUS calculation golden cases and records the REAL legacy result of each.

  gen_status_cases.py --server <build>/rA [--out conformance/golden/status_pre.jsonl.gz] [--seed N] [--scale 1.0]

For every stat function of the PRE branch of status.cpp (status_calc_str ... status_calc_speed, aspd_rate, the max HP / SP status bonuses,
element / mode, status_calc_state) it builds synthetic units (PC and mob with explicit base values and an explicit active status set
{SC_NAME: val1..val4}) -- single statuses at several value levels, pairs and larger sets (stacking, ordering, overrides, clamps),
PC-only and mob-only branches, structured speed / ASPD / flee / watk cases, extreme bases -- writes them as `key=value` lines for the
patched map-server (golden_status_hook.inc), runs it once, and stores inputs + legacy outputs as gzip-compressed JSONL with a fixed
mtime, so a rerun is byte-identical. The statuses and the val fields each function reads come from the parse of status.cpp
(st_extract.py), not from a hand list. The first lines of the file are the status database dump (states, calc flags, flags, lists as the
real loader holds them) and the constants the Luau code uses.
"""
import argparse, collections, gzip, json, os, random, re, shutil, subprocess, sys, tempfile, pathlib

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import st_extract as X
import st_gen_defs as G

SEED = 20261019
FN_OF = {   # status.cpp function -> case fn
    "status_calc_str": "str", "status_calc_agi": "agi", "status_calc_vit": "vit", "status_calc_int": "int", "status_calc_dex": "dex",
    "status_calc_luk": "luk", "status_calc_batk": "batk", "status_calc_watk": "watk", "status_calc_critical": "cri",
    "status_calc_hit": "hit", "status_calc_flee": "flee", "status_calc_flee2": "flee2", "status_calc_def": "def", "status_calc_def2": "def2",
    "status_calc_mdef": "mdef", "status_calc_mdef2": "mdef2", "status_calc_speed": "speed", "status_calc_aspd_rate": "aspd_rate",
    "status_calc_fix_aspd": "fix_aspd", "status_calc_patk": "patk", "status_calc_smatk": "smatk", "status_calc_res": "res",
    "status_calc_mres": "mres", "status_calc_hplus": "hplus", "status_calc_crate": "crate", "status_calc_pow": "pow", "status_calc_sta": "sta",
    "status_calc_wis": "wis", "status_calc_spl": "spl", "status_calc_con": "con", "status_calc_crt": "crt",
    "status_calc_element": "element", "status_calc_element_lv": "element_lv", "status_calc_attack_element": "atk_element",
    "status_calc_mode": "mode", "status_get_hpbonus": "maxhp", "status_get_spbonus": "maxsp", "status_calc_pseudobuff_matk": "matk",
}
STAT_FNS = ["str", "agi", "vit", "int", "dex", "luk", "pow", "sta", "wis", "spl", "con", "crt"]
BASES = {   # typical and extreme base values per function (what the callers pass in)
    "stat": [1, 5, 9, 20, 30, 50, 77, 99, 120, 130, 200, 500, 30000, 65535, 70000, 0, -3],
    "batk": [0, 10, 50, 100, 300, 800, 2000, 20000], "watk": [0, 30, 100, 250, 600, 1500, 3000, 65535, 70000],
    "hit": [0, 5, 80, 150, 250, 400, 600, 5000, 32767, 40000], "cri": [0, 10, 50, 100, 300, 600, 1000, 20000, 32767],
    "flee": [0, 5, 60, 120, 250, 400, 700, 5000, 32767, 40000], "flee2": [0, 10, 50, 100, 300, 600],
    "def": [-130, -127, -51, -50, -9, -3, -1, 0, 1, 10, 40, 70, 90, 127, 128, 200], "def2": [0, 5, 20, 60, 120, 200, 400, 1000, 32767],
    "mdef": [-130, -127, -21, -20, -7, -1, 0, 1, 10, 40, 70, 90, 127, 128], "mdef2": [0, 5, 20, 60, 120, 200, 400, 1000, 32767],
    "speed": [20, 50, 100, 150, 200, 400, 1000, 2000], "aspd_rate": [0, 500, 800, 1000, 1200, 2000, 32767, 40000],
    "fix_aspd": [1, 100, 300, 500, 1000, 2000, 5000, 7999, 8000, 9000], "small": [0, 1, 5, 20, 100, 400, 1000, 30000, 32767, 40000],
    "element": [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 300], "element_lv": [1, 2, 3, 4, 0, 7, -2], "mode": [0, 1, 0x200, 0xFFFF, 0x10000, 0x7FFFFFFF, -5],
    "maxhp": [0, 1, 10, 100, 500, 1000, 5000, 100000, 2000000], "matk": [0, 10, 100, 1000, 30000, 65535, 70000],
}
HP_SKILLS = ["CR_TRUST", "SU_SPRITEMABLE", "SU_POWEROFSEA", "NV_BREAKTHROUGH", "NV_TRANSCENDENCE", "SL_KAINA", "RA_RESEARCHTRAP", "WM_LESSON",
             "HP_MEDITATIO", "HW_SOULDRAIN", "SU_TUNABELLY", "SU_TUNAPARTY", "SU_BUNCHOFSHRIMP", "SU_FRESHSHRIMP", "SU_GROOMING", "SU_PURRING",
             "SU_SHRIMPARTY"]
POOL = [0, 1, 2, 3, 4, 5, 6, 8, 10, 12, 15, 20, 25, 30, 40, 50, 75, 99, 100]
POOL_EXT = [-1, -5, -20, -100, 150, 255, 300, 1000]   # (products with a 32767 base must stay below 2^31: the legacy ints would overflow)


def base_pool(fn):
    if fn in ("hp_fix", "hp_rate", "sp_fix", "sp_rate"): return [0]
    if fn in STAT_FNS: return BASES["stat"]
    if fn in ("patk", "smatk", "res", "mres", "hplus", "crate"): return BASES["small"]
    if fn in ("maxsp",): return BASES["maxhp"]
    return BASES.get(fn, BASES["small"])


# ---------------------------------------------------------------------------------------------------------------------
class Model:
    """What the legacy source says about the statuses: per function the statuses, per status the vals that are read and the interesting
    constants they are compared with (from the parse of status.cpp)."""

    def __init__(self, cpp, consts):
        self.res, _ = G.build_all(cpp)
        self.K = consts
        self.fn_statuses = collections.OrderedDict()
        self.used = collections.defaultdict(set)         # (status, N) -> {fn}
        self.interesting = collections.defaultdict(set)  # (status, N) -> {values}
        self.unit_dep = set()                            # (fn, status) whose guards mention the unit type / sd
        self.finals = collections.defaultdict(set)       # fn -> statuses with terminal entries
        for name, r in self.res.items():
            fn = FN_OF.get(name)
            if fn is None:
                continue
            sts = []
            for s in X.sces(r["body"]):
                if s not in sts:
                    sts.append(s)
            self.fn_statuses[fn] = sorted(sts)
            for en in r["entries"]:
                self._scan(fn, en)
        self.fn_statuses["speed"] = sorted(set(self.fn_statuses["speed"]) | set(X.MACRO_STATUSES))
        for k in ("hp_fix", "hp_rate"): self.fn_statuses[k] = self.fn_statuses["maxhp"]      # status_get_hpbonus(FIX / RATE) called directly
        for k in ("sp_fix", "sp_rate"): self.fn_statuses[k] = self.fn_statuses["maxsp"]

    def const(self, ident):
        return self.K.get(ident)

    def _scan(self, fn, en):
        exprs = list(en["guard"])
        for k in ("rhs", "expr"):
            if en.get(k) is not None:
                exprs.append(en[k])
        for e in exprs:
            self._walk(fn, e, en)
        owners = set(x for g in en["guard"] for x in X.sces(g))
        txt = json.dumps(en["guard"])
        if ("BL_" in txt or '"sd"' in txt):
            for o in owners:
                self.unit_dep.add((fn, o))
        if en["kind"] == "return":
            for o in owners:
                self.finals[fn].add(o)

    def _val(self, e):
        e = X.strip_paren(e)
        if e[0] == "member" and e[1][0] == "sce" and re.fullmatch(r"val[1-4]", e[2]):
            return (e[1][1], int(e[2][3]))
        return None

    def _num(self, e):
        e = X.strip_paren(e)
        if e[0] == "num": return e[1]
        if e[0] == "id": return self.K.get(e[1])
        return None

    def _walk(self, fn, e, en):
        if isinstance(e, list):
            for x in e: self._walk(fn, x, en)
            return
        if not isinstance(e, tuple):
            return
        v = self._val(e)
        if v: self.used[v].add(fn)
        if e[0] == "bin" and e[1] in ("==", "!=", "<", ">", "<=", ">=", "&"):
            for a, b in ((e[2], e[3]), (e[3], e[2])):
                if self._val(a) is None and e[1] != "&":
                    # (valN & MASK) == x inside a bigger expression
                    pass
                vv = self._val(a)
                if vv is None and X.strip_paren(a)[0] == "bin" and X.strip_paren(a)[1] in ("&", ">>"):
                    inner = X.strip_paren(a)
                    vv = self._val(inner[2])
                    n = self._num(inner[3])
                    if vv and n is not None:
                        self.interesting[vv].update([n, 0, n << 1 if inner[1] == "&" else 0])
                n = self._num(b)
                if vv and n is not None:
                    self.interesting[vv].update([n, n - 1, n + 1, 0])
        for x in e[1:]:
            self._walk(fn, x, en)

    def pool(self, st, n):
        p = set(POOL) | set(self.interesting.get((st, n), ()))
        return sorted(p)


# ---------------------------------------------------------------------------------------------------------------------
SPECIAL_VALS = {   # (status, N): values that matter (flag fields, enum fields)
    ("JOINTBEAT", 2): [0, 1, 2, 4, 8, 16, 6, 3, 12, 24, 31, 5, 10, 20, 18],       # BREAK_ANKLE 1, WRIST 2, KNEE 4, SHOULDER 8, WAIST 16
    ("PARALYSE", 3): [0, 1, 2], ("GOSPEL", 4): [0, 65536, 131072, 196608], ("GRAVITATION", 3): [0, 65536, 131072],
    ("SPIRIT", 2): [0, 455, 494, 493, 1], ("CLOAKING", 4): [0, 1, 2, 3], ("CLOAKING", 1): [1, 2, 3, 4, 5, 8, 9, 10, 11],
    ("MODECHANGE", 2): [0, 1, 0x1, 0x4, 0x82, 0x8000, 0xFFFF, 0x10000], ("MODECHANGE", 3): [0, 1, 0x200, 0x8000, 0x10000, 0x40000000],
    ("MODECHANGE", 4): [0, 1, 0x2, 0x8000, 0xFFFF, 0x7FFFFFFF], ("ENDURE", 3): [0, 1], ("ENDURE", 4): [0, 1],
    ("NIBELUNGEN", 2): [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 20, 30],
    ("FIRE_INSIGNIA", 1): [0, 1, 2, 3], ("WATER_INSIGNIA", 1): [0, 1, 2, 3], ("WIND_INSIGNIA", 1): [0, 1, 2, 3], ("EARTH_INSIGNIA", 1): [0, 1, 2, 3],
    ("CHASEWALK", 3): [-50, -1, 0, 1, 20, 100], ("DANCING", 4): [0, 1, 2, 3], ("ELEMENTALCHANGE", 1): [1, 2, 3, 4, 0, 5], ("ELEMENTALCHANGE", 2): list(range(10)) + [300],
    ("SHAPESHIFT", 2): list(range(10)) + [300], ("ENCHANTARMS", 1): list(range(10)) + [300], ("WALKSPEED", 1): [-5, 0, 1, 10, 50, 100, 150, 200, 1000],
    ("OVERED_BOOST", 2): [-5, 0, 100, 40000, 70000], ("OVERED_BOOST", 3): [0, 5, 200, 1000], ("OVERED_BOOST", 4): [0, 10, 100],
    ("VENOMBLEED", 3): [0, 1, 2], ("PROMOTE_HEALTH_RESERCH", 3): [-500, 0, 200, 3000],
    ("MARIONETTE", 3): [0, 0x010203, 0x7F7F7F, 0xFFFFFF, 0x050A0F, -1, -65536, -257], ("MARIONETTE", 4): [0, 0x010203, 0x7F7F7F, 0xFFFFFF, 0x050A0F, -1, -65536, -257],
    ("MARIONETTE2", 3): [0, 0x010203, 0x7F7F7F, 0xFFFFFF, 0x050A0F, -1, -65536], ("MARIONETTE2", 4): [0, 0x010203, 0x7F7F7F, 0xFFFFFF, 0x050A0F, -1, -65536],
    ("SPIRIT", 3): [0, 0x010203, 0x7F7F7F, 0xFFFFFF, -1, -4096], ("SPIRIT", 4): [0, 0x010203, 0x7F7F7F, 0xFFFFFF, -1, -4096],
    ("BLESSING", 2): [0, 0, 1, 5, 10, 20], ("DEFSET", 1): [-200, -1, 0, 1, 50, 100, 127, 128, 200, 40000, 70000],
    ("MDEFSET", 1): [-200, -1, 0, 1, 50, 100, 127, 128, 200, 40000, 70000],
}


class Gen:
    def __init__(self, model, seed, scale):
        self.m = model
        self.R = random.Random(seed)
        self.scale = scale
        self.cases = []

    def n(self, k):
        return max(1, int(round(k * self.scale)))

    # ---- helpers
    def val(self, st, n, mode):
        sp = SPECIAL_VALS.get((st, n))
        if sp:
            return self.R.choice(sp)
        pool = self.m.pool(st, n)
        if mode == "typ":
            return self.R.choice([x for x in pool if 0 < x <= 30] or [1])
        if mode == "wide":
            return self.R.choice(pool + POOL_EXT[:3])
        if mode == "big":
            return self.R.choice([150, 255, 300, 1000, 100, 99])
        return self.R.choice(pool + POOL_EXT)

    def vals(self, fn, st, mode):
        v = [0, 0, 0, 0]
        for n in range(1, 5):
            if fn in self.m.used.get((st, n), ()) or (st, n) in SPECIAL_VALS or self.R.random() < 0.15:
                v[n - 1] = self.val(st, n, mode)
        return v

    def add(self, cat, fn, ut, base, sc, unit=None, cfg=None, extra=None):
        c = {"cat": cat, "fn": fn, "ut": ut, "base": base, "sc": sc, "unit": unit or {}, "cfg": cfg or {}}
        if extra:
            c.update(extra)
        self.cases.append(c)

    def unit(self, fn, ut, st_names=()):
        """Random unit parameters relevant for `fn`."""
        u = {}
        if ut != "pc":
            return u
        R = self.R
        u["level"] = R.choice([1, 50, 99])
        if fn in ("aspd_rate", "watk"):
            u["w"] = R.choice([0, 1, 2, 3, 4, 5, 6, 11, 12, 14, 17, 18, 19, 20, 21])
        if fn in ("hp_fix", "hp_rate", "sp_fix", "sp_rate", "maxhp", "maxsp"):
            u["bonus_hp"] = R.choice([0, 0, 100, 2500]); u["bonus_sp"] = R.choice([0, 0, 30, 400])
            u["sk"] = {k: R.randint(0, 10) for k in HP_SKILLS if R.random() < 0.5}
        if fn == "watk":
            u["lr"] = R.choice([0, 1]); u["rwlv"] = R.choice([0, 1, 2, 3, 4, 4, 5]); u["lwlv"] = R.choice([0, 1, 2, 3, 4, 4, 5])
        return u

    def states(self, fn, k, mode=None):
        pool = self.m.fn_statuses[fn]
        k = min(k, len(pool))
        return self.R.sample(pool, k)

    def sc_for(self, fn, names, mode):
        return {s: self.vals(fn, s, mode if mode else self.R.choice(["typ", "wide", "any"])) for s in names}

    def base(self, fn):
        return self.R.choice(base_pool(fn))

    # ---- generic generators
    def gen_function(self, fn):
        m, R = self.m, self.R
        sts = m.fn_statuses[fn]
        uts = ["pc", "mob"] if fn not in ("maxhp", "maxsp") else ["mob"]   # status_calc_maxhp is the non-player path (players: status_calc_maxhp_pc)
        # empty status set (the sc->empty() path): extreme bases
        for b in base_pool(fn)[: self.n(8)]:
            self.add("empty", fn, R.choice(uts), b, {}, self.unit(fn, "pc"))
        # one status at a time, several value levels, both unit types
        for i, st in enumerate(sts):
            modes = ["typ", "wide", "big", "any"]
            for k in range(self.n(4)):
                ut = uts[(i + k) % len(uts)]
                if (fn, st) in m.unit_dep and len(uts) == 2:
                    ut = uts[k % 2]
                    if k >= 2:
                        pass
                self.add("single", fn, ut, self.base(fn), {st: self.vals(fn, st, modes[k % 4])}, self.unit(fn, ut))
            if (fn, st) in m.unit_dep and len(uts) == 2:
                for k in range(self.n(2)):
                    for ut in uts:
                        self.add("single_unit", fn, ut, self.base(fn), {st: self.vals(fn, st, "typ")}, self.unit(fn, ut))
        # pairs
        for _ in range(self.n(max(60, len(sts) * 2))):
            names = R.sample(sts, 2) if len(sts) >= 2 else sts
            ut = R.choice(uts)
            self.add("pair", fn, ut, self.base(fn), self.sc_for(fn, names, None), self.unit(fn, ut))
        # larger sets
        for _ in range(self.n(max(40, len(sts)))):
            names = R.sample(sts, min(len(sts), R.randint(3, 7)))
            ut = R.choice(uts)
            self.add("multi", fn, ut, self.base(fn), self.sc_for(fn, names, None), self.unit(fn, ut))
        # early-return statuses together with others
        fin = sorted(m.finals.get(fn, ()))
        for st in fin:
            for _ in range(self.n(3)):
                names = [st] + R.sample([s for s in sts if s != st], min(len(sts) - 1, R.randint(1, 4)))
                ut = R.choice(uts)
                self.add("override", fn, ut, self.base(fn), self.sc_for(fn, names, None), self.unit(fn, ut))
        # clamps: extreme bases and values
        for _ in range(self.n(25)):
            names = R.sample(sts, min(len(sts), R.randint(1, 3)))
            ut = R.choice(uts)
            self.add("clamp", fn, ut, R.choice(base_pool(fn)), self.sc_for(fn, names, "big"), self.unit(fn, ut))
        # everything at once
        for _ in range(self.n(6)):
            ut = R.choice(uts)
            self.add("all", fn, ut, self.base(fn), self.sc_for(fn, sts, "typ"), self.unit(fn, ut))


# ---------------------------------------------------------------------------------------------------------------------
# structured generators
OPT_RIDING, OPT_WUG, OPT_MADO = 0x20, 0x200000, 0x400000
OPT_DRAGONS = [0x80000, 0x800000, 0x1000000, 0x2000000, 0x4000000]
SPEED_SKILLS = ["NC_MADOLICENCE", "RA_WUGRIDER", "RG_TUNNELDRIVE", "TF_MISS", "MC_PUSHCART", "BA_MUSICALLESSON", "DC_DANCINGLESSON", "SA_FREECAST"]


def gen_speed(g, n):
    R = g.R
    sts = g.m.fn_statuses["speed"]
    for i in range(g.n(n)):
        u = {"level": R.choice([1, 50, 99]), "sex": R.choice([0, 1]), "job": R.choice([0, 1, 1])}
        opt = 0
        if R.random() < 0.25: opt |= OPT_RIDING
        if R.random() < 0.15: opt |= R.choice(OPT_DRAGONS)
        if R.random() < 0.15: opt |= OPT_WUG
        if R.random() < 0.12: opt |= OPT_MADO
        if R.random() < 0.3: opt |= R.choice([0x8, 0x80, 0x100, 0x200, 0x400])   # cart option bits (inert with NEW_CARTS)
        u["opt"] = opt
        sk = {}
        for s in SPEED_SKILLS:
            if R.random() < 0.5: sk[s] = R.randint(0, 10)
        if sk: u["sk"] = sk
        if R.random() < 0.3:
            u["skilltimer"] = 1
            u["ud_skill"] = R.choice(["LG_EXEEDBREAK", "SA_FREECAST", "MC_PUSHCART"])
            u["ud_lv"] = R.randint(0, 10)
        if R.random() < 0.08: u["perm_speed"] = 1
        if R.random() < 0.35:
            u["bonus_speed_rate"] = R.choice([-50, -25, -10, 0, 10, 25, 50, 100]); u["bonus_speed_add_rate"] = R.choice([-20, -5, 0, 5, 30])
        cfg = {"rental_mount_speed_boost": R.choice([0, 10, 25, 40])}
        k = R.choice([0, 1, 1, 2, 2, 3, 4, 6])
        names = R.sample(sts, min(k, len(sts)))
        if R.random() < 0.3 and "PUSH_CART" not in names: names.append("PUSH_CART")
        for forced in ("FUSION", "ALL_RIDING", "HIDING", "CHASEWALK", "LONGING", "DANCING", "SPIRIT", "MARSHOFABYSS", "PARALYSE", "STEELBODY", "DEFENDER",
                       "ARMOR", "WALKSPEED", "ACCELERATION", "QUAGMIRE", "INCREASEAGI", "CLOAKING"):
            if R.random() < 0.06 and forced not in names and forced in sts: names.append(forced)
        ut = "pc" if R.random() < 0.8 else "mob"
        g.add("speed_struct", "speed", ut, R.choice([150, 150, 100, 120, 200, 400]), g.sc_for("speed", names, None), u if ut == "pc" else {}, cfg)


def gen_aspd(g, n):
    R = g.R
    fam = ["STAR_COMFORT", "TWOHANDQUICKEN", "ONEHAND", "MERC_QUICKEN", "ADRENALINE2", "ADRENALINE", "SPEARQUICKEN", "GATLINGFEVER", "FLEET",
           "INVINCIBLE", "ASSNCROS"]
    pots = ["ASPDPOTION0", "ASPDPOTION1", "ASPDPOTION2", "ASPDPOTION3"]
    pen = [s for s in g.m.fn_statuses["aspd_rate"] if s not in fam and s not in pots]
    for i in range(g.n(n)):
        names = R.sample(fam, R.randint(0, 5)) + R.sample(pots, R.choice([0, 0, 1, 2, 4])) + R.sample(pen, R.randint(0, 4))
        ut = "pc" if R.random() < 0.7 else "mob"
        u = {"w": R.choice([0, 1, 2, 4, 11, 12, 17, 18, 19, 20, 21])} if ut == "pc" else {}
        g.add("aspd_struct", "aspd_rate", ut, R.choice([1000, 800, 1200, 500, 950]), g.sc_for("aspd_rate", names, None), u)


def gen_flee(g, n):
    R = g.R
    sts = g.m.fn_statuses["flee"]
    for i in range(g.n(n)):
        ut = "pc" if R.random() < 0.8 else "mob"
        cfg = {"gvg_flee_penalty": R.choice([0, 20, 30, 50, 100, 120]), "bg_flee_penalty": R.choice([0, 20, 30, 50, 100])}
        k = R.choice([0, 0, 1, 2, 3])
        names = R.sample(sts, min(k, len(sts)))
        if R.random() < 0.2: names.append(R.choice(["POISON_MIST", "OVERED_BOOST"]))
        g.add("flee_struct", "flee", ut, R.choice(BASES["flee"]), g.sc_for("flee", names, None), {"map": R.choice([0, 1, 2])} if ut == "pc" else {"map": R.choice([0, 1, 2])}, cfg)


def gen_watk(g, n):
    R = g.R
    for i in range(g.n(n)):
        ut = "pc" if R.random() < 0.85 else "mob"
        names = ["NIBELUNGEN"] + R.sample([s for s in g.m.fn_statuses["watk"] if s != "NIBELUNGEN"], R.randint(0, 3))
        u = {"lr": R.choice([0, 1]), "rwlv": R.randint(0, 5), "lwlv": R.randint(0, 5), "w": R.choice([1, 2, 11, 16])} if ut == "pc" else {}
        g.add("watk_struct", "watk", ut, R.choice(BASES["watk"]), g.sc_for("watk", names, None), u)


def gen_maxhp(g, n):
    R = g.R
    for fn in ("maxhp", "maxsp"):
        sts = g.m.fn_statuses[fn]
        rate_neg = {"maxhp": ["EQC", "_WEAKNESS", "BEYONDOFWARCRY", "VENOMBLEED"], "maxsp": ["MELODYOFSINK"]}[fn]
        for i in range(g.n(n)):
            k = R.choice([1, 1, 2, 3, 5])
            names = R.sample(sts, min(k, len(sts)))
            if R.random() < 0.25:
                names.append("MARIONETTE" if fn == "maxhp" else R.choice(sts))
            names = sorted(set(names))
            g.add("max_struct", fn, "mob", R.choice(BASES["maxhp"]), g.sc_for(fn, names, None))
        for st in rate_neg:                      # -100 % and below, wrap-around of a negative flat sum
            if st in sts:
                for b in (0, 5, 500, 100000):
                    g.add("max_neg", fn, "mob", b, {st: [R.choice([50, 100, 150, 1000]), R.choice([50, 100, 150, 1000]), 100, 100]})


def gen_pc_bonus(g, n):
    """status_get_hpbonus / status_get_spbonus of players: item bonus, skills (Trust, Breakthrough, Transcendence, Summoner powers ...)."""
    R = g.R
    for fn in ("hp_fix", "sp_fix", "sp_rate"):
        for i in range(g.n(n)):
            names = R.sample(g.m.fn_statuses[fn], R.choice([0, 1, 2]))
            u = g.unit(fn, "pc")
            u["sk"] = {k: R.randint(0, 10) for k in HP_SKILLS if R.random() < 0.7}
            g.add("pc_bonus", fn, "pc", 0, g.sc_for(fn, names, None), u)


def scripted_statuses(records_path):
    """{NAME: script text} of the status records that carry a Script:, and the identifiers those scripts use."""
    txt = pathlib.Path(records_path).read_text()
    scripts = {}
    for m in re.finditer(r'\["([^"]+)"\] = \{[^\n]*?script = "((?:[^"\\]|\\.)*)"', txt):
        scripts[m.group(1)] = m.group(2).replace("\\n", "\n").replace('\\"', '"')
    idents = set()
    for src in scripts.values():
        for t in re.findall(r"[A-Za-z_][A-Za-z0-9_]*", re.sub(r"\.@\w+", "", src)):
            if t not in ("bonus", "bonus2", "bonus3", "bonus4", "bonus5", "if", "else", "getstatus") and not t.startswith("SC_"):
                idents.add(t)
    return scripts, sorted(idents)


def gen_script(g, scripted, n_single, n_combo):
    """BR-RULESET-075: the Script: of the active statuses -> the bonus commands it runs."""
    R = g.R
    names = sorted(scripted)
    for st in names:
        for i in range(g.n(n_single)):
            v = [R.choice([0, 1, 2, 5, 10, 20, 50, 100, -5, -20]) for _ in range(4)]
            g.add("script", "script", "pc", 0, {st: v}, {"level": 99})
    for i in range(g.n(n_combo)):
        k = R.randint(2, 6)
        picked = R.sample(names, k)
        if R.random() < 0.3 and "BERSERK" not in picked: picked.append("BERSERK")
        sc = {st: [R.choice([0, 1, 3, 10, 25, 100, -10]) for _ in range(4)] for st in picked}
        g.add("script_combo", "script", "pc", 0, sc, {"level": 99})


def gen_worked(g):
    """Hand-chosen inputs: the worked example E-4b and one illustration per converted rule (BR-RULESET-065 .. 077), and the legacy quirks
    the documentation names. Their `tag` is how tests/support/status_rule_slices.luau finds them; the legacy still computes every output."""
    A = g.add
    A("worked", "str", "mob", 65530, {"BLESSING": [10, 20, 0, 0]}, None, None, {"tag": "R065-clamp"})
    A("worked", "str", "mob", 90, {"BLESSING": [10, 20, 0, 0], "HARMONIZE": [0, 40, 0, 0], "TRUESIGHT": [10, 100, 30, 0]}, None, None, {"tag": "R065-harmonize"})
    A("worked", "luk", "pc", 60, {"CURSE": [0, 0, 0, 0], "GLORIA": [0, 0, 0, 0], "TRUESIGHT": [10, 100, 30, 0]}, {"level": 50}, None, {"tag": "R065-curse"})
    A("worked", "def2", "mob", -5, {}, None, None, {"tag": "R066-floor"})
    A("worked", "def2", "mob", 120, {"BERSERK": [1, 0, 0, 0]}, None, None, {"tag": "R066-berserk"})
    A("worked", "def", "pc", 40, {"STEELBODY": [0, 0, 0, 0], "SIGNUMCRUCIS": [10, 30, 0, 0]}, {"level": 50}, None, {"tag": "R066-steelbody"})
    A("worked", "def", "mob", 60, {"PROVOKE": [5, 17, 30, 0], "SKE": [0, 0, 0, 0]}, None, None, {"tag": "R066-provoke"})
    A("worked", "speed", "mob", 150, {"QUAGMIRE": [5, 25, 0, 0], "INCREASEAGI": [10, 12, 0, 0]}, None, None, {"tag": "E-4b"})
    A("worked", "aspd_rate", "mob", 1000, {"TWOHANDQUICKEN": [10, 30, 0, 0], "ADRENALINE": [5, 0, 20, 0]}, None, None, {"tag": "R068-quicken"})
    A("worked", "aspd_rate", "mob", 1000, {"TWOHANDQUICKEN": [10, 30, 0, 0], "BERSERK": [1, 0, 0, 0]}, None, None, {"tag": "R068-berserk"})
    A("worked", "hit", "mob", 100, {"INCHIT": [20, 0, 0, 0], "INCHITRATE": [10, 0, 0, 0], "BLIND": [0, 0, 0, 0]}, None, None, {"tag": "R069-hit"})
    A("worked", "batk", "mob", 200, {"VOLCANO": [5, 50, 0, 0], "ASH": [0, 0, 0, 20]}, None, None, {"tag": "R069-batk"})
    A("worked", "maxhp", "mob", 1000, {"INCMHP": [200, 0, 0, 0], "INCMHPRATE": [20, 0, 0, 0], "BERSERK": [1, 0, 0, 0]}, None, None, {"tag": "R069-maxhp"})
    A("worked", "element", "mob", 3, {"FREEZE": [0, 0, 0, 0]}, None, None, {"tag": "R071-element"})
    A("worked", "element_lv", "mob", 4, {"STONE": [0, 0, 0, 0]}, None, None, {"tag": "R071-element-lv"})
    A("worked", "atk_element", "pc", 0, {"ENCHANTARMS": [3, 0, 0, 0], "FIREWEAPON": [0, 0, 0, 0]}, {"level": 50}, None, {"tag": "R071-atk-element"})
    A("worked", "state", "pc", 0, {"STUN": [1, 0, 0, 0]}, {"level": 50}, None, {"tag": "R073-stun", "changed": "STUN", "start": 1, "cant": {c: 0 for c in CANT}})
    A("worked", "state", "pc", 0, {"HIDING": [1, 0, 0, 0]}, {"level": 50}, None, {"tag": "R073-hiding", "changed": "HIDING", "start": 1, "cant": {c: 0 for c in CANT}})
    A("worked", "script", "pc", 0, {"SPEEDUP0": [25, 0, 0, 0]}, {"level": 99}, None, {"tag": "R075-speedup0"})
    A("worked", "script", "pc", 0, {"S_LIFEPOTION": [3, 4, 0, 0]}, {"level": 99}, None, {"tag": "R075-lifepotion"})
    A("worked", "chain", "mob", 0, {"BLESSING": [10, 0, 0, 0]}, {"level": 40}, {"enable_baseatk": 3, "max_walk_speed": 100},
      {"tag": "R077-partial", "b": dict(str=50, agi=40, vit=30, int=20, dex=60, luk=10, batk=80, rhw_atk=100, rhw_atk2=140, lhw_atk=0, lhw_atk2=0, rhw_ele=0, lhw_ele=0, hit=150, flee=120, flee2=5, cri=20, **{"def": 10}, def2=40, mdef=5, mdef2=30, speed=150, amotion=1000, adelay=1500, aspd_rate=1000, max_hp=5000, max_sp=300, def_ele=0, ele_lv=1, matk_min=0, matk_max=0),
       "s": {"dex": 70, "hit": 999, "flee": 777}, "flags": ["DEX", "FLEE"]})
    # legacy quirks (docs/STATUS_FIELDS.md, README)
    A("quirk", "def", "mob", 50, {"DEFSET": [40000, 0, 0, 0]}, None, None, {"tag": "Q-defset-narrowing"})
    A("quirk", "maxhp", "mob", 500, {"MARIONETTE": [0, 0, 0, 0]}, None, None, {"tag": "Q-marionette-wrap"})
    A("quirk", "maxhp", "mob", 5000, {"MARIONETTE": [0, 0, 0, 0]}, None, None, {"tag": "Q-marionette-ok"})
    A("quirk", "flee", "mob", 100, {"OVERED_BOOST": [0, 40000, 0, 0]}, None, None, {"tag": "Q-overed-boost-narrowing"})
    A("quirk", "str", "mob", 40, {"HARMONIZE": [0, 100, 0, 0]}, None, None, {"tag": "Q-harmonize-below-zero"})
    A("quirk", "mdef", "mob", -7, {"STONE": [0, 0, 0, 0]}, None, None, {"tag": "Q-stone-mdef-truncation"})
    A("quirk", "def", "mob", -7, {"STONE": [0, 0, 0, 0]}, None, None, {"tag": "Q-stone-def-truncation"})


def gen_matk(g, n):
    R = g.R
    sts = g.m.fn_statuses["matk"]
    for i in range(g.n(n)):
        ut = "pc" if R.random() < 0.8 else "mob"
        names = R.sample(sts, R.choice([0, 1, 2, 4, 8]))
        u = {"sk": {"NV_TRANSCENDENCE": R.randint(0, 10)}} if ut == "pc" else {}
        g.add("matk_struct", "matk", ut, R.choice(BASES["matk"]), g.sc_for("matk", names, None), u)


def gen_mode(g, n):
    R = g.R
    for i in range(g.n(n)):
        ut = R.choice(["pc", "mob"])
        sc = {"MODECHANGE": [R.choice([0, 1]), g.val("MODECHANGE", 2, "any"), g.val("MODECHANGE", 3, "any"), g.val("MODECHANGE", 4, "any")]} if R.random() < 0.9 else {}
        g.add("mode_struct", "mode", ut, R.choice(BASES["mode"]), sc)


STATE_VALS = {("GOSPEL", 4): [0, 65536, 131072], ("BASILICA", 4): [0, 150000, 200000, 7], ("GRAVITATION", 3): [0, 65536, 131072],
              ("CAMOUFLAGE", 1): [1, 2, 3, 4, 5], ("MAGNETICFIELD", 2): [0, 150000, 200000], ("FEAR", 2): [-1, 0, 1, 5],
              ("DANCING", 4): [0, 1, 2], ("DANCING", 1): [395, 488, 100, 395 | 0x10000, 488 | 0x30000], ("OBLIVIONCURSE", 3): [0, 1, 2],
              ("NOCHAT", 1): [0, 1, 8, 9, 2, 15], }
CANT = ["move", "cast", "chat", "attack", "warp", "pickup", "drop", "equip", "unequip", "consume", "deathpenalty", "interact"]


def gen_state(g, dbstates, n_per):
    R = g.R
    with_state = sorted(k for k, v in dbstates.items() if v)
    cond = [k for k in with_state if any(x.endswith("COND") for x in dbstates[k])]
    for st in with_state:
        for i in range(g.n(n_per * (5 if st in ("DANCING", "LONGING") else 1))):
            ut = R.choice(["pc", "mob"])
            k = R.choice([0, 1, 2, 4])
            others = R.sample(with_state, min(k, len(with_state)))
            if R.random() < 0.5 and cond:
                others.append(R.choice(cond))
            if R.random() < 0.3 and "LONGING" not in others:
                others.append("LONGING")
            start = R.choice([0, 1])
            in_set = start == 1 or R.random() < 0.3
            names = sorted(set(others) | ({st} if in_set else set()))
            sc = {}
            for s in names:
                v = [R.choice([0, 1, 2, 3]) for _ in range(4)]
                for nn in range(1, 5):
                    sp = STATE_VALS.get((s, nn))
                    if sp: v[nn - 1] = R.choice(sp)
                sc[s] = v
            cant = {c: R.choice([0, 1]) for c in CANT}
            u = {"sk": {"RG_TUNNELDRIVE": R.choice([0, 0, 3])}} if ut == "pc" else {}
            g.add("state", "state", ut, 0, sc, u, None, {"changed": st, "start": start, "cant": cant})


CHAIN_FIELDS = ["str", "agi", "vit", "int", "dex", "luk", "batk", "rhw_atk", "rhw_atk2", "lhw_atk", "lhw_atk2", "rhw_ele", "lhw_ele", "hit", "flee",
                "flee2", "cri", "def", "def2", "mdef", "mdef2", "speed", "amotion", "adelay", "aspd_rate", "max_hp", "max_sp", "def_ele", "ele_lv",
                "matk_min", "matk_max"]
CHAIN_FLAGS_MOB = ["STR", "AGI", "VIT", "INT", "DEX", "LUK", "BATK", "WATK", "HIT", "FLEE", "DEF", "DEF2", "MDEF", "MDEF2", "SPEED", "ASPD", "CRI",
                   "FLEE2", "ATK_ELE", "DEF_ELE", "MAXHP", "MAXSP", "MATK"]
CHAIN_FLAGS_PC = ["STR", "LUK", "BATK", "WATK", "HIT", "FLEE", "DEF", "DEF2", "MDEF", "MDEF2", "SPEED", "CRI", "FLEE2", "ATK_ELE", "DEF_ELE", "MATK"]


def gen_chain(g, n):
    """status_calc_bl_main: base status + stale battle status + a flag set + active statuses -> the recalculated battle status."""
    R = g.R
    pool = sorted(set(sum((g.m.fn_statuses[f] for f in ("str", "agi", "vit", "int", "dex", "luk", "batk", "watk", "hit", "flee", "flee2", "cri", "def", "def2",
                                                        "mdef", "mdef2", "speed", "aspd_rate", "fix_aspd", "matk", "maxhp", "maxsp", "element", "element_lv",
                                                        "atk_element")), [])) | {"RECOGNIZEDSPELL", "MAGICPOWER"})
    for i in range(g.n(n)):
        ut = "mob" if R.random() < 0.65 else "pc"
        flags = CHAIN_FLAGS_MOB if ut == "mob" else CHAIN_FLAGS_PC
        fl = R.sample(flags, R.randint(1, 7)) if R.random() < 0.7 else list(flags)
        if ut == "pc" and R.random() < 0.5:                    # players: stale stats model what the earlier calculations left behind
            pass
        names = R.sample(pool, R.choice([0, 1, 2, 3, 5, 8]))
        if R.random() < 0.3: names.append(R.choice(["RECOGNIZEDSPELL", "MAGICPOWER"]))
        sc = g.sc_for("chain", sorted(set(names)), None)
        if "MAGICPOWER" in sc and R.random() < 0.7: sc["MAGICPOWER"][3] = 1
        b = {"str": R.randint(1, 130), "agi": R.randint(1, 130), "vit": R.randint(1, 130), "int": R.randint(1, 130), "dex": R.randint(1, 130),
             "luk": R.randint(1, 130), "batk": R.choice([0, 0, 10, 80, 300]), "rhw_atk": R.randint(0, 400), "rhw_atk2": R.randint(0, 600),
             "lhw_atk": R.choice([0, 0, 50, 200]), "lhw_atk2": R.choice([0, 100, 300]), "rhw_ele": R.randint(0, 9), "lhw_ele": R.randint(0, 9),
             "hit": R.randint(0, 400), "flee": R.randint(0, 400), "flee2": R.choice([0, 0, 10, 50]), "cri": R.choice([0, 0, 10, 30, 90]),
             "def": R.randint(-20, 100), "def2": R.randint(0, 300), "mdef": R.randint(-20, 100), "mdef2": R.randint(0, 300),
             "speed": R.choice([100, 150, 200, 400]), "amotion": R.choice([500, 1000, 1500, 2000]), "adelay": R.choice([500, 1000, 1800, 2500]),
             "aspd_rate": R.choice([1000, 1000, 800, 1200]), "max_hp": R.choice([100, 5000, 120000]), "max_sp": R.choice([10, 500, 3000]),
             "def_ele": R.randint(0, 9), "ele_lv": R.randint(1, 4), "matk_min": 0, "matk_max": 0}
        s = {}
        for k in ("str", "agi", "vit", "int", "dex", "luk"):          # stale battle stats differ from the base ones (an earlier calculation)
            if R.random() < 0.6: s[k] = max(0, b[k] + R.randint(-20, 40))
        for k in ("batk", "hit", "flee", "cri", "def2", "mdef2", "flee2", "speed", "max_hp", "max_sp"):
            if R.random() < 0.5: s[k] = max(0, b[k] + R.randint(-30, 50))
        u = {"level": R.choice([1, 40, 99])}
        if ut == "pc":
            u["w"] = R.choice([0, 1, 2, 11, 12, 13, 14, 15, 16, 17, 19, 21])
            u["bonus_weapon_atk_rate"] = R.choice([0, 0, 10, 25])
            if R.random() < 0.15: u["perm_speed"] = 1
        cfg = {"enable_baseatk": R.choice([0, 3, 0x3FF, 1, 2]), "max_walk_speed": R.choice([100, 20, 150])}
        g.add("chain", "chain", ut, 0, sc, u, cfg, {"b": b, "s": s, "flags": fl})
    # int16 storage of the stats: a calculated 40000 / 65535 is stored as a negative number; products of stale and base values
    for i in range(g.n(80)):
        ut = "mob" if R.random() < 0.6 else "pc"
        flags = CHAIN_FLAGS_MOB if ut == "mob" else CHAIN_FLAGS_PC
        fl = R.sample(flags, R.randint(2, 9))
        big = [30000, 32000, 32700, 32767]   # int16 members: the base values fit, the calculated ones (base + status) overflow
        b = {k: R.choice([1, 50, 130]) for k in ("str", "agi", "vit", "int", "dex", "luk")}
        for k in ("str", "agi", "vit", "int", "dex", "luk"):
            if R.random() < 0.5: b[k] = R.choice(big)
        b.update({"batk": R.choice([0, 100, 20000]), "rhw_atk": R.choice([100, 40000, 65535]), "rhw_atk2": R.choice([0, 65535]), "lhw_atk": R.choice([0, 60000]),
                  "lhw_atk2": 0, "rhw_ele": 1, "lhw_ele": 2, "hit": R.choice([100, 32000, 32767]), "flee": R.choice([100, 32767]), "flee2": R.choice([0, 50, 32000]),
                  "cri": R.choice([0, 50, 32000]), "def": R.choice([-100, 50, 120]), "def2": R.choice([0, 32000]), "mdef": R.choice([-100, 50, 120]),
                  "mdef2": R.choice([0, 32000]), "speed": R.choice([150, 1000]), "amotion": R.choice([500, 8000]), "adelay": R.choice([500, 8000]),
                  "aspd_rate": R.choice([1000, 32767]), "max_hp": R.choice([100, 4000000000]), "max_sp": 3000, "def_ele": 0, "ele_lv": 1,
                  "matk_min": 0, "matk_max": 0})
        names = R.sample(pool, R.choice([0, 1, 3]))
        sc = g.sc_for("chain", sorted(set(names)), "big")
        if R.random() < 0.6: sc["INCALLSTATUS"] = [R.choice([2000, 3000, 7000, 30000]), 0, 0, 0]   # +val1 on all six stats: pushes the int16 member over 32767
        u = {"level": 99}
        if ut == "pc": u["w"] = R.choice([1, 11, 16])
        g.add("chain_extreme", "chain", ut, 0, sc, u, {"enable_baseatk": 0x3FF, "max_walk_speed": 100}, {"b": b, "s": {}, "flags": fl})


# ---------------------------------------------------------------------------------------------------------------------
UNIT_KEYS = {"level": "level", "w": "w", "lr": "lr", "rwlv": "rwlv", "lwlv": "lwlv", "opt": "opt", "job": "job", "sex": "sex",
             "perm_speed": "perm_speed", "skilltimer": "skilltimer", "ud_lv": "ud.lv", "bonus_speed_rate": "bonus.speed_rate",
             "bonus_speed_add_rate": "bonus.speed_add_rate", "map": "map", "bonus_hp": "bonus.hp", "bonus_sp": "bonus.sp",
             "bonus_weapon_atk_rate": "bonus.weapon_atk_rate"}


def line(c):
    t = [f"id={c['id']}", f"fn={c['fn']}", f"ut={0 if c['ut'] == 'pc' else 1}", f"base={c['base']}"]
    u = c["unit"]
    for k, tok in UNIT_KEYS.items():
        if k in u and u[k] != 0:
            t.append(f"{tok}={u[k]}")
    for k, v in sorted(u.get("sk", {}).items()):
        if v:
            t.append(f"sk.{k}={v}")
    if "ud_skill" in u:
        t.append(f"ud.skill={u['ud_skill']}")
    for k, v in sorted(c["cfg"].items()):
        t.append(f"cfg.{k}={v}")
    for st, v in sorted(c["sc"].items()):
        t.append(f"sc.{st}=" + ",".join(str(x) for x in v))
    if c["fn"] == "chain":
        for k, v in c["b"].items(): t.append(f"b.{k}={v}")
        for k, v in c["s"].items(): t.append(f"s.{k}={v}")
        t.append("flags=" + ",".join(c["flags"]))
    if c["fn"] == "state":
        t.append(f"changed={c['changed']}")
        t.append(f"start={c['start']}")
        for k, v in c["cant"].items():
            if v: t.append(f"cant.{k}=1")
    return " ".join(t)


def parse_out(path):
    out = {}
    for l in pathlib.Path(path).read_text().split("\n"):
        if not l:
            continue
        kv = dict(x.split("=", 1) for x in l.split(" "))
        cid = int(kv["id"])
        if kv["fn"] == "state":
            out[cid] = {c: int(kv[c]) for c in CANT}
        elif kv["fn"] == "chain":
            out[cid] = {k: int(kv[k]) for k in CHAIN_FIELDS}
        elif kv["fn"] == "script":
            out[cid] = [] if kv["log"] == "-" else [[int(x) for x in rec.split(",")] for rec in kv["log"].split(";")]
        else:
            out[cid] = int(kv["out"])
    return out


def parse_dump(path):
    consts, skdb, sc, db, cfg, scriptk = {}, {}, {}, {}, {}, {}
    for l in pathlib.Path(path).read_text().split("\n"):
        p = l.split(" ")
        if l.startswith("DB "):
            if p[2] == "-":
                continue
            rec = {"name": p[1]}
            for kv in p[2:]:
                k, v = kv.split("=", 1)
                rec[k] = ([] if v == "-" else v.split(",")) if k in ("state", "calc", "flag", "fail", "end_on_start", "end_return", "end_on_end") else int(v)
            db[p[1]] = rec
        elif l.startswith("SKDB "):
            skdb[p[1]] = int(p[2])
        elif l.startswith("SC "):
            sc[p[1]] = int(p[2])
        elif l.startswith("CFG "):
            cfg[p[1]] = int(p[2])
        elif l.startswith("SCRIPTK "):
            scriptk[p[1]] = int(p[2]) if p[2] != "?" else None
        elif len(p) == 2 and p[0] != "MIN_ASPD_VALUE":
            consts[p[0]] = int(p[1])
    return consts, skdb, sc, db, cfg, scriptk


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--server", required=True, help="<build>/rA directory (map-server built by build.sh with GOLDEN_PRERE=1)")
    ap.add_argument("--out", default=str(HERE.parent / "golden" / "status_pre.jsonl.gz"))
    ap.add_argument("--seed", type=int, default=SEED)
    ap.add_argument("--scale", type=float, default=1.0)
    ap.add_argument("--status-cpp", default=X.DEFAULT_CPP)
    ap.add_argument("--dump-out", help="also copy the constants dump of the legacy build here (input of st_consts.py)")
    ap.add_argument("--records", default=str(HERE.parent.parent / "data" / "status_records.luau"), help="the status records (for the statuses that have a Script:)")
    a = ap.parse_args()
    server = pathlib.Path(a.server)
    tmp = pathlib.Path(tempfile.mkdtemp(prefix="goldenst-"))

    def run(env_extra):
        env = dict(os.environ); env.update(env_extra)
        r = subprocess.run([str(server / "map-server")], cwd=server, env=env, capture_output=True, text=True, timeout=1800)
        if r.returncode != 0:
            sys.stderr.write(r.stdout[-2000:] + r.stderr[-2000:]); sys.exit(f"map-server failed ({r.returncode})")
        return r

    scripted, idents = scripted_statuses(a.records)
    run({"RA_GOLDEN_ST_CONSTS": str(tmp / "consts.txt"), "RA_GOLDEN_ST_SCRIPTK": ",".join(idents)})
    if a.dump_out:
        shutil.copyfile(tmp / "consts.txt", a.dump_out)
    consts, skdb, scids, db, cfgdef, scriptk = parse_dump(tmp / "consts.txt")
    missing = [k for k, v in scriptk.items() if v is None]
    assert not missing, ("script constants unknown to the legacy build", missing)
    model = Model(a.status_cpp, consts)
    g = Gen(model, a.seed, a.scale)
    for fn in model.fn_statuses:
        if fn != "chain":
            g.gen_function(fn)
    gen_speed(g, 700); gen_aspd(g, 220); gen_flee(g, 160); gen_watk(g, 140); gen_maxhp(g, 120); gen_pc_bonus(g, 60); gen_matk(g, 90); gen_mode(g, 80)
    gen_chain(g, 900)
    gen_script(g, scripted, 5, 200)
    gen_worked(g)
    gen_state(g, {k: v["state"] for k, v in db.items()}, 6)
    for i, c in enumerate(g.cases, 1):
        c["id"] = i
    (tmp / "cases.txt").write_text("\n".join(line(c) for c in g.cases) + "\n")
    run({"RA_GOLDEN_ST_CASES": str(tmp / "cases.txt"), "RA_GOLDEN_ST_OUT": str(tmp / "out.txt")})
    out = parse_out(tmp / "out.txt")
    assert len(out) == len(g.cases), (len(out), len(g.cases))
    with open(a.out, "wb") as raw:
        with gzip.GzipFile(filename="", mode="wb", fileobj=raw, mtime=0, compresslevel=9) as z:
            def w(o):
                z.write((json.dumps(o, sort_keys=True, separators=(",", ":")) + "\n").encode())
            w({"t": "consts", "K": consts, "skdb": skdb, "sc": scids, "cfg": cfgdef, "scriptk": scriptk})
            for name in sorted(db):
                w(dict(db[name], t="db"))
            for c in g.cases:
                rec = {"t": "case", "id": c["id"], "cat": c["cat"], "fn": c["fn"], "ut": c["ut"], "base": c["base"], "sc": c["sc"], "unit": c["unit"],
                       "cfg": c["cfg"], "out": out[c["id"]]}
                if c["fn"] == "state":
                    rec.update(changed=c["changed"], start=c["start"], cant=c["cant"])
                if c["fn"] == "chain":
                    rec.update(b=c["b"], s=c["s"], flags=c["flags"])
                if "tag" in c:
                    rec["tag"] = c["tag"]
                w(rec)
    cnt = collections.Counter(c["fn"] for c in g.cases)
    print(f"{len(g.cases)} cases, {len(db)} status db records -> {a.out}")
    print(" ".join(f"{k}={v}" for k, v in sorted(cnt.items())))


if __name__ == "__main__":
    main()
