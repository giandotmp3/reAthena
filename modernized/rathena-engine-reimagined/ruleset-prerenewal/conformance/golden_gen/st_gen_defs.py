#!/usr/bin/env python3
"""FIRST-DRAFT generator of the Luau status definitions (scripts/status/defs/*.luau) from the PRE branch of status.cpp.

usage: st_gen_defs.py --out DIR [--status-cpp PATH] [--report FILE]

Run once to produce the draft; the Luau files in the repository are the MAINTAINED source (reviewed and corrected by hand against the
golden data, plus the hand-written files scripts/status/defs_hand.luau, speed.luau, aspd.luau ...). This tool is not run by CI.

What it does per `if (sc->getSCE(SC_X)) attr += expr;` style statement of a stat function (parsed by st_extract.py):
  * decides the owner status (the status whose presence triggers the statement), the guard (`when`) and the operation:
        add v | mul_pct v | mul_frac v d | scale v d | div d | set v | final v | final_div d | pct_over base pct | gmax(group) v
  * names the val1..val4 fields of every status from how the covered functions use them (role names) plus a curated override table,
    and collects the producer line (`valN = ...;` in status_change_start) with its comment for docs/STATUS_FIELDS.md,
  * writes one definition per status, every contribution tagged `at = <status.cpp line>` (the citation AND the legacy order key).
Statements it cannot express (declarations, switches, pc_checkskill, ...) are written to the report as HAND and are the content of
scripts/status/defs_hand.luau.
"""
import argparse, json, re, sys, pathlib, collections
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import st_extract as X

ATTR = {  # function -> Luau attribute
    "status_calc_str": "str", "status_calc_agi": "agi", "status_calc_vit": "vit", "status_calc_int": "int", "status_calc_dex": "dex",
    "status_calc_luk": "luk", "status_calc_batk": "batk", "status_calc_watk": "watk", "status_calc_critical": "cri",
    "status_calc_hit": "hit", "status_calc_flee": "flee", "status_calc_flee2": "flee2", "status_calc_def": "def", "status_calc_def2": "def2",
    "status_calc_mdef": "mdef", "status_calc_mdef2": "mdef2", "status_calc_aspd_rate": "aspd_rate", "status_calc_fix_aspd": "fix_aspd",
    "status_calc_patk": "patk", "status_calc_smatk": "smatk", "status_calc_res": "res", "status_calc_mres": "mres",
    "status_calc_hplus": "hplus", "status_calc_crate": "crate", "status_calc_pow": "pow", "status_calc_sta": "sta",
    "status_calc_wis": "wis", "status_calc_spl": "spl", "status_calc_con": "con", "status_calc_crt": "crt",
    "status_calc_element": "element", "status_calc_element_lv": "element_lv", "status_calc_attack_element": "atk_element",
    "status_calc_mode": "mode", "status_calc_speed": "speed",
}
STATS = {"str", "agi", "vit", "int", "dex", "luk"}
# clamp text the final `return` of every function must have (verified against the parse)
CLAMP = {
    "str": ("0", "USHRT_MAX"), "agi": ("0", "USHRT_MAX"), "vit": ("0", "USHRT_MAX"), "int": ("0", "USHRT_MAX"), "dex": ("0", "USHRT_MAX"),
    "luk": ("0", "USHRT_MAX"), "watk": ("0", "USHRT_MAX"), "cri": ("10", "SHRT_MAX"), "hit": ("1", "SHRT_MAX"), "flee": ("1", "SHRT_MAX"),
    "flee2": ("10", "SHRT_MAX"), "def": ("DEFTYPE_MIN", "DEFTYPE_MAX"), "def2": ("1", "SHRT_MAX"), "mdef": ("DEFTYPE_MIN", "DEFTYPE_MAX"),
    "mdef2": ("1", "SHRT_MAX"), "aspd_rate": ("0", "SHRT_MAX"), "fix_aspd": ("1", "MIN_ASPD"), "patk": ("0", "SHRT_MAX"),
    "smatk": ("0", "SHRT_MAX"), "res": ("0", "SHRT_MAX"), "mres": ("0", "SHRT_MAX"), "hplus": ("0", "SHRT_MAX"), "crate": ("0", "SHRT_MAX"),
    "pow": ("0", "USHRT_MAX"), "sta": ("0", "USHRT_MAX"), "wis": ("0", "USHRT_MAX"), "spl": ("0", "USHRT_MAX"), "con": ("0", "USHRT_MAX"),
    "crt": ("0", "USHRT_MAX"), "element": ("0", "UCHAR_MAX"), "element_lv": ("1", "4"), "atk_element": ("0", "UCHAR_MAX"),
    "mode": ("MD_NONE", "INT_MAX"), "speed": ("MIN_WALK_SPEED", "MAX_WALK_SPEED"),
}
# val = max(val, E) groups of status_calc_speed / status_calc_aspd_rate by source line range: (first, last, attr, group)
GMAX = [(8070, 8079, "speed", "mount"), (8083, 8087, "speed", "slow_force"), (8088, 8159, "speed", "slow"),
        (8168, 8221, "speed", "haste"), (8436, 8489, "aspd_rate", "quicken")]
SD_MEMBERS = {("bonus", "speed_rate"): "e.unit.bonus_speed_rate", ("bonus", "speed_add_rate"): "e.unit.bonus_speed_add_rate",
              ("status", "sex"): "e.unit.sex", ("sc", "option"): "e.unit.option"}
SD_MACROS = {"pc_isriding": "O.band(e.unit.option, K.OPTION_RIDING) ~= 0", "pc_isridingwug": "O.band(e.unit.option, K.OPTION_WUGRIDER) ~= 0",
             "pc_ismadogear": "O.band(e.unit.option, K.OPTION_MADOGEAR) ~= 0"}
# curated field names: (STATUS, N) -> name. Everything else is named from its role.
OVERRIDE = {
    ("BLESSING", 1): "lv", ("BLESSING", 2): "stat_inc",          # 0 = half stat (undead / demon target, status.cpp:11588-11592)
    ("INCREASEAGI", 1): "lv", ("INCREASEAGI", 2): "agi_inc",
    ("DECREASEAGI", 1): "lv", ("DECREASEAGI", 2): "agi_dec",
    ("CONCENTRATE", 1): "lv", ("CONCENTRATE", 2): "stat_pct", ("CONCENTRATE", 3): "agi_card_bonus", ("CONCENTRATE", 4): "dex_card_bonus",
    ("CONCENTRATION", 1): "lv", ("CONCENTRATION", 2): "atk_pct", ("CONCENTRATION", 3): "hit_inc", ("CONCENTRATION", 4): "def_pct_dec",
    ("PROVOKE", 1): "lv", ("PROVOKE", 2): "atk_pct", ("PROVOKE", 3): "def_pct_dec", ("PROVOKE", 4): "auto",
    ("QUAGMIRE", 1): "lv", ("QUAGMIRE", 2): "agi_dex_dec",
    ("ANGELUS", 1): "lv", ("ANGELUS", 2): "def_pct_inc",
    ("HARMONIZE", 2): "stat_dec",
    ("SPIRIT", 1): "lv", ("SPIRIT", 2): "link_type",
    ("MARIONETTE", 3): "str_agi_vit_packed", ("MARIONETTE", 4): "int_dex_luk_packed",
    ("MARIONETTE2", 3): "str_agi_vit_packed", ("MARIONETTE2", 4): "int_dex_luk_packed",
    ("ENDURE", 1): "lv", ("ENDURE", 3): "permanent_flag", ("ENDURE", 4): "card_flag",
    ("CHASEWALK", 3): "speed_value",
    ("DANCING", 4): "ensemble_flag",
    ("DEFSET", 1): "def_set", ("MDEFSET", 1): "mdef_set",
    ("WALKSPEED", 1): "speed_pct",
    ("MODECHANGE", 2): "mode_set", ("MODECHANGE", 3): "mode_add", ("MODECHANGE", 4): "mode_del",
    ("ELEMENTALCHANGE", 1): "element_lv", ("ELEMENTALCHANGE", 2): "element",
    ("SHAPESHIFT", 2): "element",
    ("ENCHANTARMS", 1): "element",
    ("JOINTBEAT", 2): "break_flags",
    ("GOSPEL", 4): "target_type",
    ("GRAVITATION", 2): "aspd_rate_inc", ("GRAVITATION", 3): "target_type",
    ("CLOAKING", 1): "lv", ("CLOAKING", 4): "mode_flags",
    ("PARALYSE", 3): "kind",
    ("NIBELUNGEN", 2): "atk_inc",
    ("FIRE_INSIGNIA", 1): "level", ("WATER_INSIGNIA", 1): "level", ("WIND_INSIGNIA", 1): "level", ("EARTH_INSIGNIA", 1): "level",
    # statuses whose stat code is hand written (scripts/status/defs_hand.luau)
    ("ASSNCROS", 2): "aspd_rate_quicken",
    ("ASPDPOTION0", 1): "aspd_inc", ("ASPDPOTION0", 2): "aspd_rate_dec", ("ASPDPOTION1", 1): "aspd_inc", ("ASPDPOTION1", 2): "aspd_rate_dec",
    ("ASPDPOTION2", 1): "aspd_inc", ("ASPDPOTION2", 2): "aspd_rate_dec", ("ASPDPOTION3", 1): "aspd_inc", ("ASPDPOTION3", 2): "aspd_rate_dec",
    ("DORAM_MATK", 1): "matk_inc", ("AQUAPLAY_OPTION", 2): "matk_inc", ("CHILLY_AIR_OPTION", 2): "matk_inc", ("COOLER_OPTION", 2): "matk_inc",
    ("SOULFAIRY", 2): "matk_inc", ("INSPIRATION", 2): "atk_inc", ("SHIELDSPELL_ATK", 2): "atk_inc", ("ZANGETSU", 2): "batk_inc",
    ("ZANGETSU", 3): "matk_inc", ("MOONLITSERENADE", 3): "matk_inc", ("_AUTOSHADOWSPELL", 4): "matk_inc",
    ("ODINS_POWER", 1): "lv", ("IZAYOI", 1): "lv",
    ("MAGICPOWER", 2): "uses", ("MAGICPOWER", 3): "matk_pct", ("MAGICPOWER", 4): "active",
    ("ANGRIFFS_MODUS", 2): "atk_val", ("ANGRIFFS_MODUS", 4): "drain_timer", ("ARCLOUSEDASH", 4): "ranged_atk_bonus",
    ("ARMOR", 2): "damage_divisor", ("ARMOR", 3): "range", ("ARMOR", 4): "type", ("BERSERK", 2): "no_hp_loss", ("BERSERK", 3): "skill_duration",
    ("BLAST_OPTION", 3): "element_change", ("COOLER_OPTION", 3): "element_change", ("CURSED_SOIL_OPTION", 3): "element_change",
    ("HEATER_OPTION", 3): "element_change", ("CHANGE", 2): "vit_inc", ("CHANGE", 3): "int_inc",
    ("CHASEWALK", 2): "sp_interval", ("CLOAKING", 2): "sp_interval", ("CLOAKING", 3): "unused", ("CLOAKINGEXCEED", 2): "hit_count",
    ("DEFENDER", 2): "damage_reduction", ("DEFENDER", 3): "unused", ("ELEMENTALCHANGE", 3): "unused", ("ENCPOISON", 2): "poison_chance",
    ("ENDURE", 2): "hit_count", ("FLEET", 3): "atk_pct_change", ("FORCEOFVANGUARD", 2): "chance", ("FORCEOFVANGUARD", 3): "max_rage",
    ("GOLDENE_FERSE", 4): "holy_chance", ("GT_REVITALIZE", 3): "hp_regen_inc", ("GT_REVITALIZE", 4): "def2_inc",
    ("HALLUCINATIONWALK", 3): "mdef_evasion", ("NPC_HALLUCINATIONWALK", 3): "mdef_evasion", ("HIDING", 3): "unused", ("HIDING", 4): "sp_interval",
    ("INVINCIBLE", 2): "atk_pct_inc", ("LUXANIMA", 2): "storm_blast_chance", ("PARALYSIS", 3): "varcast_inc", ("PRESTIGE", 2): "magic_evade",
    ("SINCERE_FAITH", 3): "perfect_hit_inc", ("SOULFAIRY", 3): "varcast_dec", ("SPIRIT", 3): "str_agi_vit_packed", ("SPIRIT", 4): "int_dex_luk_packed",
    ("STRIKING", 3): "sp_cost", ("TEARGAS", 2): "hp_drain", ("VENOMBLEED", 2): "reflect_dec", ("VENOMBLEED", 3): "hp_penalty_flag",
    ("_LAZINESS", 2): "cast_inc", ("FRIGG_SONG", 3): "healing",
}


class Unsup(Exception):
    pass


# ---------------------------------------------------------------------------------------------------------------------
# expression translation (C subset -> Luau text)
def is_boolish(e):
    e = X.strip_paren(e)
    return e[0] in ("sce",) or (e[0] == "bin" and e[1] in ("&&", "||", "==", "!=", "<", ">", "<=", ">=")) or (e[0] == "un" and e[1] == "!")


class Tr:
    def __init__(self, names, owner, accvar):
        self.names, self.owner, self.acc = names, owner, accvar
        self.uses_s = False

    def field(self, status, n):
        nm = self.names.get((status, n))
        if nm is None:
            raise Unsup(f"unnamed field {status}.val{n}")
        if self.owner is not None and status == self.owner:
            self.uses_s = True
            return f"s.{nm}"
        return f"e.sc.{status}.{nm}"

    def num(self, e):
        e = X.strip_paren(e) if e[0] == "paren" and False else e
        k = e[0]
        if k == "num":
            return str(e[1])
        if k == "paren":
            return "(" + self.num(e[1]) + ")"
        if k == "cast":
            return self.num(e[2])
        if k == "member":
            o = e[1]
            if o[0] == "sce" and re.fullmatch(r"val[1-4]", e[2]):
                return self.field(o[1], int(e[2][3]))
            if o[0] == "member" and o[1] == ("id", "sd") and (o[2], e[2]) in SD_MEMBERS:
                return SD_MEMBERS[(o[2], e[2])]
            if o == ("id", "sd") and e[2] == "class_":
                return "e.unit.class_"
            if o == ("id", "battle_config"):
                return f"e.cfg.{e[2]}"
            raise Unsup("member " + json.dumps(e))
        if k == "id":
            if e[1] == self.acc:
                raise Unsup("accumulator in value")
            if e[1] in X.CONSTS or e[1] in ("DEFTYPE_MIN", "DEFTYPE_MAX"):
                return f"K.{e[1]}"
            raise Unsup("identifier " + e[1])
        if k == "un":
            if e[1] == "-": return "-" + self.atom(e[2])
            if e[1] == "+": return self.num(e[2])
            if e[1] == "~": return f"O.bnot({self.num(e[2])})"
            if e[1] == "!": return f"({self.boolean(e)} and 1 or 0)"
        if k == "bin":
            op, a, b = e[1], e[2], e[3]
            if op in ("&&", "||", "==", "!=", "<", ">", "<=", ">="):
                return f"({self.boolean(e)} and 1 or 0)"
            if op in ("+", "-", "*"):
                return f"{self.atom(a)} {op} {self.atom(b)}"
            if op == "/":
                return f"e.idiv({self.num(a)}, {self.num(b)})"
            if op in ("&", "|", "^", "<<", ">>"):
                fn = {"&": "band", "|": "bor", "^": "bxor", "<<": "shl", ">>": "sar"}[op]
                return f"O.{fn}({self.num(a)}, {self.num(b)})"
            raise Unsup("operator " + op)
        if k == "cond":
            return f"({self.boolean(e[1])} and {self.num(e[2])} or {self.num(e[3])})"
        if k == "call" and e[1] in ("max", "min") and len(e[2]) == 2:
            return f"math.{e[1]}({self.num(e[2][0])}, {self.num(e[2][1])})"
        if k == "call" and e[1] == "pc_checkskill" and len(e[2]) == 2 and e[2][0] == ("id", "sd") and e[2][1][0] == "id":
            return f'e.skill("{e[2][1][1]}")'
        if k == "call" and e[1] == "pc_checkskill" and len(e[2]) == 2 and e[2][0] == ("id", "sd") and e[2][1][0] == "paren":
            # pc_checkskill(sd, (sd->status.sex ? BA_MUSICALLESSON : DC_DANCINGLESSON))
            c = e[2][1][1]
            if c[0] == "cond":
                return f'e.skill({self.boolean(c[1])} and "{c[2][1]}" or "{c[3][1]}")'
        if k == "call" and e[1] in SD_MACROS:
            return f"({SD_MACROS[e[1]]} and 1 or 0)"
        if k == "sce":
            return f"({self.boolean(e)} and 1 or 0)"
        raise Unsup("expr " + k)

    def atom(self, e):
        s = self.num(e)
        e0 = e
        if e0[0] in ("num", "paren", "member", "id") or (e0[0] == "call"):
            return s
        return "(" + s + ")"

    def boolean(self, e):
        k = e[0]
        if k == "paren":
            return "(" + self.boolean(e[1]) + ")"
        if e == ("id", "sd"):
            return 'e.unit.type == "pc"'
        if k == "call" and e[1] in SD_MACROS:
            return SD_MACROS[e[1]]
        if k == "sce":
            return f"e.sc.{e[1]} ~= nil"
        if k == "un" and e[1] == "!":
            inner = X.strip_paren(e[2])
            if inner[0] == "sce":
                return f"e.sc.{inner[1]} == nil"
            return f"not ({self.boolean(e[2])})"
        if k == "bin" and e[1] == "&&":
            return f"{self.boolean_op(e[2])} and {self.boolean_op(e[3])}"
        if k == "bin" and e[1] == "||":
            return f"{self.boolean_op(e[2])} or {self.boolean_op(e[3])}"
        if k == "bin" and e[1] in ("==", "!=", "<", ">", "<=", ">="):
            a, b = e[2], e[3]
            # bl->type == BL_X
            for x, y in ((a, b), (b, a)):
                if x[0] == "member" and x[1] == ("id", "bl") and x[2] == "type" and y[0] == "id" and y[1] in ("BL_PC", "BL_MOB", "BL_HOM", "BL_MER", "BL_ELEM"):
                    t = y[1][3:].lower()
                    return f'e.unit.type {"==" if e[1] == "==" else "~="} "{t}"'
            if e[1] in ("==", "!=") and b[0] == "null":
                raise Unsup("null compare")
            op = {"==": "==", "!=": "~="}.get(e[1], e[1])
            return f"{self.num(a)} {op} {self.num(b)}"
        if k == "bin" and e[1] in ("&", "|", "^") or k in ("member", "num", "id", "cast", "bin", "cond", "un"):
            return f"{self.num(e)} ~= 0"
        raise Unsup("bool " + k)

    def boolean_op(self, e):   # operand of and / or: parenthesise or / and mixes
        s = self.boolean(e)
        e1 = X.strip_paren(e)
        if e1[0] == "bin" and e1[1] in ("||", "&&") or e[0] == "cond":
            return "(" + s + ")"
        return s


# ---------------------------------------------------------------------------------------------------------------------
def norm(e):
    """`getSCE(X) != nullptr` -> presence; `== nullptr` -> absence; recursive."""
    if isinstance(e, tuple):
        if e[0] == "bin" and e[1] in ("!=", "==") and e[3] == ("null",) and X.strip_paren(e[2])[0] == "sce":
            return X.strip_paren(e[2]) if e[1] == "!=" else ("un", "!", X.strip_paren(e[2]))
        return tuple(norm(x) if isinstance(x, (tuple, list)) else x for x in e)
    if isinstance(e, list):
        return [norm(x) for x in e]
    return e


def flatten_and(e):
    e = X.strip_paren(norm(e))
    if e[0] == "bin" and e[1] == "&&":
        return flatten_and(e[2]) + flatten_and(e[3])
    if e == ("id", "sc"):
        return []          # `sc && ...`: sc is never null in these functions
    return [e]


def is_preamble(guard):
    return len(guard) == 1 and guard[0][0] == "bin" and guard[0][1] == "||" and X.strip_paren(guard[0][2])[0] == "bin" and \
        X.strip_paren(guard[0][2])[2:] and "nullptr" in json.dumps(guard[0]) or False


def analyze_scale(rhs, acc):
    """rhs == acc * N1 * N2 ... / D  ->  (list of numerator exprs, denominator expr or None). Raises Unsup."""
    rhs = X.strip_paren(rhs) if rhs[0] == "paren" else rhs
    if rhs[0] == "id" and rhs[1] == acc:
        return [], None
    if rhs[0] == "bin" and rhs[1] == "/" and X.mentions(rhs[2], acc) and not X.mentions(rhs[3], acc):
        nums, den = analyze_scale(rhs[2], acc)
        if den is not None:
            raise Unsup("double division")
        return nums, rhs[3]
    if rhs[0] == "bin" and rhs[1] == "*":
        a, b = rhs[2], rhs[3]
        if X.mentions(a, acc) and not X.mentions(b, acc):
            nums, den = analyze_scale(a, acc)
            if den is not None:
                raise Unsup("multiply after division")
            return nums + [b], None
        if X.mentions(b, acc) and not X.mentions(a, acc):
            nums, den = analyze_scale(b, acc)
            if den is not None:
                raise Unsup("multiply after division")
            return [a] + nums, None
    raise Unsup("not a scale expression")


def analyzable(e, acc):
    try:
        analyze_scale(e, acc)
        return True
    except Unsup:
        return False


def lower_entry(en, acc, names, attr):
    """Returns dict(op, v, v2, owners, when_exprs) or raises Unsup. Does not translate yet (needs owner)."""
    kind = en["kind"]
    guard = []
    for g in en["guard"]:
        guard += flatten_and(g)
    return guard


def build_all(cpp):
    res, unsup, toks = X.build(cpp)
    return res, unsup


# ---------------------------------------------------------------------------------------------------------------------
# producer lines (status_change_start): `valN = expr; // comment` per status label, for docs/STATUS_FIELDS.md
def producers(cpp):
    lines = X.load_pre(cpp)
    start = None
    for ln, t in lines:
        if re.match(r"bool status_change_start\(", t):
            start = ln
            break
    out = collections.defaultdict(dict)   # status -> {N: (line, text)}
    labels, fresh = [], True
    depth0 = None
    for ln, t in lines:
        if start is None or ln < start or ln > start + 3200:
            continue
        m = re.match(r"\s*case\s+SC_([A-Z0-9_]+)\s*:", t)
        if m:
            if not fresh:
                labels, fresh = [], True
            labels.append(m.group(1))
            continue
        if re.match(r"\s*default\s*:", t):
            labels, fresh = [], True
            continue
        s = t.strip()
        if not s or s.startswith("//") or s.startswith("{") and len(s) == 1:
            continue
        fresh = False
        for mm in re.finditer(r"\bval([1-4])\s*=(?!=)\s*([^;]*);\s*(//\s*(.*))?", t):
            n, rhs, _, com = mm.groups()
            for lab in labels:
                if int(n) not in out[lab]:
                    out[lab][int(n)] = (ln, rhs.strip(), (com or "").strip())
    return out


def slug(s):
    words = [w for w in re.sub(r"[^A-Za-z0-9]+", " ", s.lower()).split() if w not in ("the", "of", "a", "an", "to", "is", "at", "in", "for")]
    return "_".join(words[:3])[:24].strip("_")


def build_names(res, prod):
    """(status, N) -> field name. Role based, with overrides."""
    uses = collections.defaultdict(list)   # (status, n) -> [(attr, role, line)]
    def role_of(en, acc_attr, valref_in_rhs):
        return None
    for fn, r in res.items():
        attr = ATTR.get(fn)
        if attr is None:
            attr = {"status_get_hpbonus": "maxhp_bonus", "status_get_spbonus": "maxsp_bonus", "status_calc_pseudobuff_matk": "matk"}.get(fn)
        if r["acc"] is None:
            for x in r["body"]:
                pass
        for en in r["entries"]:
            roles = []
            rhs = en.get("rhs")
            sign = ""
            if en["kind"] == "assign":
                op = en["op"]
                if op in ("+=", "-="):
                    pct = rhs is not None and X.mentions(rhs, r["acc"])
                    if pct:
                        role = "pct_inc" if op == "+=" else "pct_dec"
                    else:
                        role = "inc" if op == "+=" else "dec"
                else:
                    role = "set"
            elif en["kind"] == "return":
                role = "set"
            elif en["kind"] == "gassign":
                role = group_of(attr, en["line"]) or "grp"
            else:
                role = "cond"
            for x in (rhs,) if rhs is not None else ():
                rr = X.strip_paren(x)
                while rr[0] == "cast":
                    rr = X.strip_paren(rr[2])
                if rr[0] == "call" and rr[1] == "max" and len(rr[2]) == 2:      # val = max(val, E)
                    rr = X.strip_paren(rr[2][1])
                bare = rr[0] == "member" and rr[1][0] == "sce"
                if X.mentions(x, r["acc"]) and role in ("pct_inc", "pct_dec"):
                    try:                      # acc * E / 100: E is the percentage; bare if it is exactly one valN
                        nums, den = analyze_scale(x, r["acc"])
                        e1 = X.strip_paren(nums[0]) if len(nums) == 1 else None
                        bare = e1 is not None and e1[0] == "member" and e1[1][0] == "sce"
                    except Unsup:
                        bare = False
                for st, n in refs(x):
                    uses[(st, n)].append((attr, role if bare else role + "~", en["line"]))
            for g in en["guard"]:
                for st, n in refs(g):
                    uses[(st, n)].append((attr, "cond", en["line"]))
    names = {}
    allst = set(st for st, _ in uses)
    for st in set(x for r in res.values() for x in X.sces(r["body"])):
        allst.add(st)
    allst.update(X.MACRO_STATUSES)          # PUSH_CART (pc_iscarton)
    allst.update(X.BL_MAIN_STATUSES)        # RECOGNIZEDSPELL, MAGICPOWER (status_calc_bl_main)
    for st in allst:
        for n in range(1, 5):
            if (st, n) in OVERRIDE:
                names[(st, n)] = OVERRIDE[(st, n)]
                continue
            u = uses.get((st, n), [])
            if u:
                attrs = []
                for a, ro, ln in u:
                    a2 = "stat" if a in STATS else a
                    if a2 not in attrs:
                        attrs.append(a2)
                nonc = [x for x in u if x[1] != "cond"]
                bare = [x for x in nonc if not x[1].endswith("~")]
                role = (bare or nonc)[0][1].rstrip("~") if nonc else "cond"
                if len(set(x[1].rstrip("~") for x in nonc)) > 1:
                    role = "val"
                base = "_".join(attrs[:3])
                if nonc and not bare:                 # only used inside arithmetic on the value: a level / parameter
                    names[(st, n)] = "lv" if n == 1 else f"{base}_{role}_arg"
                else:
                    names[(st, n)] = f"{base}_{role}" if role != "val" else f"{base}_val"
            else:
                p = prod.get(st, {}).get(n)
                if n == 1 and any(k in prod.get(st, {}) and "val1" in prod[st][k][1] for k in prod.get(st, {})):
                    names[(st, n)] = "lv"
                elif p is not None and p[2]:
                    names[(st, n)] = slug(p[2])[:28]
                else:
                    names[(st, n)] = f"val{n}_unused"
    # make names unique inside a status
    for st in allst:
        seen = {}
        for n in range(1, 5):
            nm = names[(st, n)]
            if nm in seen:
                nm = f"{nm}{n}"
            seen[nm] = n
            names[(st, n)] = nm
    return names, uses


def refs(e, out=None):
    out = [] if out is None else out
    if isinstance(e, tuple):
        if e[0] == "member" and e[1][0] == "sce" and re.fullmatch(r"val[1-4]", e[2]):
            out.append((e[1][1], int(e[2][3])))
        for x in e[1:]:
            if isinstance(x, (tuple, list)):
                refs(x, out)
    elif isinstance(e, list):
        for x in e:
            refs(x, out)
    return out


# ---------------------------------------------------------------------------------------------------------------------
def group_of(attr, line):
    for (a, b, at, grp) in GMAX:
        if a <= line <= b and at == attr:
            return grp
    return None


def convert(res, names):
    """Returns (by_owner, rules, hand, clamps)."""
    by_owner = collections.defaultdict(lambda: collections.defaultdict(list))   # status -> attr -> [entry dict]
    rules = collections.defaultdict(list)    # attr -> [entry dict] (no owner)
    hand = []
    clamps = {}
    for fn, r in res.items():
        attr = ATTR.get(fn)
        bonus_fn = fn in ("status_get_hpbonus", "status_get_spbonus")
        if (attr is None and not bonus_fn) or r["acc"] is None:
            continue
        acc = r["acc"]
        for en in r["entries"]:
            line = en["line"]
            try:
                guard = []
                for g in en["guard"]:
                    guard += flatten_and(g)
                if bonus_fn:   # status_get_hpbonus(bl, type): the `type == STATUS_BONUS_FIX / RATE` branch selects the attribute
                    kinds = [("fix" if "STATUS_BONUS_FIX" in json.dumps(g) else "rate") for g in guard
                             if "STATUS_BONUS" in json.dumps(g) and g[0] == "bin" and g[1] == "=="]
                    if not kinds:
                        if en["kind"] == "return" or en["kind"] == "decl":
                            continue
                        raise Unsup("bonus statement outside a type branch")
                    attr = ("hp_" if fn == "status_get_hpbonus" else "sp_") + kinds[0]
                    guard = [g for g in guard if "STATUS_BONUS" not in json.dumps(g)]
                guard = [g for g in guard if "skilltimer" not in json.dumps(g)]   # speed: the Free Cast / Exceed Break frame is the evaluator's
                k = en["kind"]
                if k == "decl":
                    continue
                if k in ("other", "unsupported"):
                    raise Unsup(k + (" " + en.get("what", "")))
                if bonus_fn and k == "assign" and en["op"] == "=" and "cap_value" in json.dumps(en["rhs"]):
                    continue                       # bonus = cap_value(bonus, -100, INT_MAX): the clamp of hp_rate / sp_rate
                gtxt = json.dumps(guard)
                if k == "return" and '"null"' in gtxt and "empty" in gtxt:
                    continue                                   # `if (sc == nullptr || sc->empty()) return clamp(...)`
                gexpr = None
                if k == "return" and guard:
                    c = X.strip_paren(en["rhs"])
                    while c[0] == "cast":
                        c = X.strip_paren(c[2])
                    if c[0] == "call" and c[1] == "cap_value" and c[2][0] == ("id", acc):
                        assert (ctext(c[2][1]), ctext(c[2][2])) == CLAMP[attr], (fn, line)
                        k = "stop"                             # Harmonize: `str -= val2; return clamp(str)`
                if k == "return" and not guard:
                    c = X.strip_paren(en["rhs"])
                    while c[0] == "cast":
                        c = X.strip_paren(c[2])
                    if c[0] == "call" and c[1] == "cap_value":
                        clamps[attr] = (ctext(c[2][1]), ctext(c[2][2]))
                        continue
                    if c[0] == "id" and c[1] == acc:           # `return batk;`
                        clamps[attr] = None
                        continue
                if k == "gassign":
                    grp = group_of(attr, line)
                    if grp is None or en["var"] == "speed_rate" or en["op"] != "=":
                        raise Unsup(f"group variable {en['var']} outside a max-group")
                    rr = X.strip_paren(en["rhs"])
                    if line in (8088,):                          # `val = 0;` start of the slow group: the evaluator's
                        continue
                    if grp in ("mount", "slow_force"):             # first-active-wins chains (the guards are mutually exclusive)
                        gexpr, gop = rr, "gset"
                    elif rr[0] == "call" and rr[1] == "max" and len(rr[2]) == 2 and rr[2][0] == ("id", en["var"]):
                        gexpr, gop = rr[2][1], "gmax"
                    else:                                       # `if (X && max < E) max = E;`
                        cand = [g for g in guard if g[0] == "bin" and g[1] == "<" and g[2] == ("id", en["var"]) and g[3] == rr]
                        if cand:
                            guard.remove(cand[0]); gexpr, gop = rr, "gmax"
                        elif line == 8436:                      # first assignment: max = star_comfort.val2
                            gexpr, gop = rr, "gset"
                        else:
                            raise Unsup("group assignment form")
                trig = None
                for g in guard:
                    t = X.trigger(g)
                    if t is not None and (trig is None or len(t) < len(trig)):
                        trig = t
                owners = sorted(trig, key=lambda s: first_pos(en, s)) if trig else []
                owner = owners[0] if owners else None
                tr = Tr(names, owner if len(owners) == 1 else None, acc)
                gtext = []
                for g in guard:
                    if len(owners) == 1 and g[0] == "sce" and g[1] == owner:
                        continue                                # implied by the owner
                    gtext.append(tr.boolean_op(g))
                if k == "stop":
                    if attr == "speed":
                        continue                                # the Mado Gear block is the speed evaluator's (scripts/status/speed.luau)
                    op = dict(op="stop")
                elif k == "gassign":
                    op = dict(op=gop, group=grp, v=tr.num(gexpr))
                else:
                    op = lower_op(en, acc, tr, attr, line)
                ent = dict(at=line, fn=fn, **op)
                if gtext:
                    ent["when"] = " and ".join(gtext)
                if len(owners) > 1:
                    ent["also"] = owners[1:]
                if owner is None:
                    rules[attr].append(ent)
                else:
                    by_owner[owner][attr].append(ent)
            except Unsup as ex:
                hand.append((fn, line, str(ex)))
    # adds that precede a `stop` of the same status (Harmonize: `str -= val2; return clamp(str)`) belong to the override stage
    for owner, d in by_owner.items():
        for attr, lst in d.items():
            stops = [e["at"] for e in lst if e["op"] == "stop"]
            for e in lst:
                if stops and e["op"] != "stop" and any(0 < st - e["at"] <= 2 for st in stops):
                    e["stage"] = "override"
    return by_owner, rules, hand, clamps


def first_pos(en, s):
    return 0


def ctext(e):
    e = X.strip_paren(e)
    if e[0] == "num":
        return str(e[1])
    if e[0] == "id":
        return e[1]
    return json.dumps(e)


def lower_op(en, acc, tr, attr, line):
    k = en["kind"]
    if k == "return":
        rhs = en["rhs"]
        r = X.strip_paren(rhs)
        # return def /= 2;   return mdef / 2;
        if r[0] == "assign" and r[1] == "/=" and r[2] == ("id", acc):
            return dict(op="final_div", v=tr.num(r[3]))
        if r[0] == "bin" and r[1] == "/" and r[2] == ("id", acc):
            return dict(op="final_div", v=tr.num(r[3]))
        rc = r
        while rc[0] == "cast":
            rc = X.strip_paren(rc[2])
        if rc[0] == "call" and rc[1] == "cap_value" and not X.mentions(rc[2][0], acc):
            assert (ctext(rc[2][1]), ctext(rc[2][2])) == CLAMP[attr], (attr, line)
            return dict(op="final_clamp", v=tr.num(rc[2][0]))
        if X.mentions(r, acc):
            raise Unsup("return depends on accumulator")
        return dict(op="final", v=tr.num(r))
    op, rhs = en["op"], en["rhs"]
    # val = max(val, E)  group assignment of speed / aspd_rate
    for (a, b, at, grp) in GMAX:
        if a <= line <= b and at == attr and op == "=" :
            r = X.strip_paren(rhs)
            if r[0] == "call" and r[1] == "max" and len(r[2]) == 2 and r[2][0][0] == "id":
                return dict(op="gmax", group=grp, v=tr.num(r[2][1]))
    if op == "+=":   # agi += (agi - base) * pct / 100   (Attention-Concentrate)
        r = X.strip_paren(rhs)
        if r[0] == "bin" and r[1] == "/" and X.strip_paren(r[3]) == ("num", 100) and X.strip_paren(r[2])[0] == "bin" and X.strip_paren(r[2])[1] == "*":
            m = X.strip_paren(r[2])
            sub = X.strip_paren(m[2])
            if sub[0] == "bin" and sub[1] == "-" and sub[2] == ("id", acc) and not X.mentions(m[3], acc) and not X.mentions(sub[3], acc):
                return dict(op="pct_over", v=tr.num(sub[3]), v2=tr.num(m[3]))
    if op == "-=":   # def2 -= def2 * (A ? 50 : 0) / 100 + def2 * (B ? 25 : 0) / 100   (Joint Beat)
        r = X.strip_paren(rhs)
        if r[0] == "bin" and r[1] == "+" and all(analyzable(x, acc) for x in (r[2], r[3])):
            (n1, d1), (n2, d2) = analyze_scale(r[2], acc), analyze_scale(r[3], acc)
            if d1 == ("num", 100) and d2 == ("num", 100) and len(n1) == 1 and len(n2) == 1:
                return dict(op="sub_pct2", v=tr.num(n1[0]), v2=tr.num(n2[0]))
    if op == "+=" or op == "-=":
        sign = "" if op == "+=" else "-"
        if not X.mentions(rhs, acc):
            v = tr.num(rhs)
            if op == "-=":
                v = "-" + (v if re.fullmatch(r"[A-Za-z0-9_.]+", v) else f"({v})")
            return dict(op="add", v=v)
        nums, den = analyze_scale(rhs, acc)
        num = " * ".join(tr.atom(n) for n in nums) if nums else "1"
        if op == "-=":
            num = "-" + (num if re.fullmatch(r"[A-Za-z0-9_.]+", num) else f"({num})")
        dtxt = tr.num(den) if den is not None else "1"
        if dtxt == "100":
            return dict(op="mul_pct", v=num)
        return dict(op="mul_frac", v=num, v2=dtxt)
    if op == "/=":
        return dict(op="div", v=tr.num(rhs))
    if op == "=":
        if X.mentions(rhs, acc):
            nums, den = analyze_scale(rhs, acc)
            num = " * ".join(tr.atom(n) for n in nums) if nums else "1"
            return dict(op="scale", v=num, v2=tr.num(den) if den is not None else "1")
        return dict(op="set", v=tr.num(rhs))
    raise Unsup("assignment operator " + op)


# ---------------------------------------------------------------------------------------------------------------------
def lua_fn(expr, uses_s, params="s, e"):
    return f"function({params}) return {expr} end"


def emit_entry(ent, names, owner):
    parts = [f"at = {ent['at']}", f'op = "{ent["op"]}"']
    if "group" in ent:
        parts.append(f'group = "{ent["group"]}"')
    if "stage" in ent:
        parts.append(f'stage = "{ent["stage"]}"')
    if "when" in ent:
        parts.append("when = " + lua_fn(ent["when"], True))
    for key in ("v", "v2"):
        if key in ent:
            val = ent[key]
            if re.fullmatch(r"-?\d+", val):
                parts.append(f"{key} = {val}")
            else:
                parts.append(f"{key} = " + lua_fn(val, True))
    if "also" in ent:
        parts.append("also = {" + ", ".join(f'"{a}"' for a in ent["also"]) + "}")
    return "{ " + ", ".join(parts) + " }"


ATTR_ORDER = ["hp_fix", "hp_rate", "sp_fix", "sp_rate", "str", "agi", "vit", "int", "dex", "luk", "pow", "sta", "wis", "spl", "con", "crt", "batk", "watk", "hit", "cri", "flee",
              "flee2", "def", "def2", "mdef", "mdef2", "speed", "aspd_rate", "fix_aspd", "patk", "smatk", "res", "mres", "hplus", "crate",
              "element", "element_lv", "atk_element", "mode"]


def write_defs(outdir, by_owner, rules, names, prod, res, db_names):
    outdir.mkdir(parents=True, exist_ok=True)
    allst = sorted(set(by_owner) | {st for st, _ in names})
    groups = collections.OrderedDict()
    for st in allst:
        c = st.lstrip("_")[:1].upper() or "_"
        key = "a_f" if c <= "F" else "g_l" if c <= "L" else "m_r" if c <= "R" else "s_z"
        groups.setdefault(key, []).append(st)
    files = []
    for key in sorted(groups):
        L = ["--!nonstrict",
             "-- Status definitions, draft generated from the PRE branch of src/map/status.cpp by conformance/golden_gen/st_gen_defs.py,",
             "-- then reviewed and corrected by hand against the golden data (this file is the maintained source).",
             "-- Every contribution is tagged `at = <status.cpp line>`: the citation and the legacy evaluation order inside its attribute.",
             "-- Field names answer Q-F (docs/STATUS_FIELDS.md). `s` = this status' named fields, `e` = environment (e.sc[NAME] other statuses,",
             "-- e.unit, e.idiv ...), `K` = legacy constants (scripts/status/consts.luau), `O` = bit helpers.",
             'local status = import("scripts/status/define")',
             'local K = import("scripts/status/consts")',
             'local O = import("scripts/status/ops")',
             "local D = status.define", ""]
        for st in groups[key]:
            fl = []
            for n in range(1, 5):
                fl.append(f'val{n} = "{names[(st, n)]}"')
            L.append(f'D("{st}", {{')
            if st not in db_names:
                L.append("\tin_pre_db = false, -- not in db/pre-re/status.yml: the stat code knows it, a PRE server never starts it")
            L.append("\tfields = { " + ", ".join(fl) + " },")
            ca = by_owner.get(st, {})
            if ca:
                L.append("\tcontributes = {")
                for attr in ATTR_ORDER:
                    if attr not in ca:
                        continue
                    L.append(f"\t\t{attr} = {{")
                    for ent in sorted(ca[attr], key=lambda x: x["at"]):
                        L.append("\t\t\t" + emit_entry(ent, names, st) + ",")
                    L.append("\t\t},")
                L.append("\t},")
            L.append("})")
            L.append("")
        (outdir / f"defs_{key}.luau").write_text("\n".join(L) + "\n")
        files.append(f"defs_{key}.luau")
    # attribute rules without an owning status
    R = ["--!nonstrict", "-- Unit rules of the stat functions that no status triggers (generated draft, hand-reviewed).",
         'local status = import("scripts/status/define")', 'local K = import("scripts/status/consts")', 'local O = import("scripts/status/ops")', ""]
    for attr in ATTR_ORDER:
        for ent in rules.get(attr, []):
            R.append(f'status.rule("{attr}", ' + emit_entry(ent, names, None) + ")")
    (outdir / "rules_gen.luau").write_text("\n".join(R) + "\n")
    return files


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--status-cpp", default=X.DEFAULT_CPP)
    ap.add_argument("--out", required=True)
    ap.add_argument("--report")
    ap.add_argument("--names-json")
    ap.add_argument("--db-names", help="file with the PRE status db names, one per line")
    a = ap.parse_args()
    res, unsup = build_all(a.status_cpp)
    prod = producers(a.status_cpp)
    names, uses = build_names(res, prod)
    by_owner, rules, hand, clamps = convert(res, names)
    db_names = set(pathlib.Path(a.db_names).read_text().split()) if a.db_names else set(n for n, _ in names)
    for attr, cl in clamps.items():
        if cl is not None and CLAMP.get(attr) not in (None, cl):
            print("CLAMP MISMATCH", attr, cl, CLAMP[attr])
    files = write_defs(pathlib.Path(a.out), by_owner, rules, names, prod, res, db_names)
    n_ent = sum(len(v) for d in by_owner.values() for v in d.values())
    print(f"statuses with entries: {len(by_owner)}; entries: {n_ent}; unit rules: {sum(len(v) for v in rules.values())}; HAND: {len(hand)}")
    ops = collections.Counter(e["op"] for d in by_owner.values() for v in d.values() for e in v)
    print("ops:", dict(ops))
    if a.report:
        with open(a.report, "w") as f:
            for h in hand:
                f.write(f"HAND {h[0]}:{h[1]}: {h[2]}\n")
    if a.names_json:
        out = {}
        for (st, n), nm in sorted(names.items()):
            out.setdefault(st, {})[n] = {"name": nm, "producer": prod.get(st, {}).get(n)}
        pathlib.Path(a.names_json).write_text(json.dumps(out, indent=1, sort_keys=True))


if __name__ == "__main__":
    main()
