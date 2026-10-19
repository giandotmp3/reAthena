#!/usr/bin/env python3
"""Parses the PRE (non-#ifdef RENEWAL) branch of the status_calc_* functions of src/map/status.cpp.

usage:
  st_extract.py list  [--status-cpp PATH]                 # enumerate statuses per function (the covered set), print the report
  st_extract.py ir    [--status-cpp PATH] --out FILE.json # the parsed contribution entries (machine readable)

The legacy file is only READ. What this tool does:
  * masks `#ifdef RENEWAL` / `#ifndef RENEWAL` / `#else` / `#endif` so only the pre-renewal text remains (line numbers are kept),
  * tokenizes and parses the bodies of the stat functions (a small C expression / statement subset),
  * lowers every guarded assignment of a function to an *entry*: (function, attribute, source line, guard, operation, operand).
    The entries are the input of `st_gen_defs.py`, which writes the first draft of the Luau status definitions; the Luau files are
    then reviewed and corrected by hand against the golden data (they are the maintained source, this tool is not run in CI).
Anything the lowering does not understand is reported as UNSUPPORTED with its line (those are written by hand).
"""
import argparse, json, re, sys, pathlib

DEFAULT_CPP = "/home/user/reAthena/src/map/status.cpp"

# function name -> (attribute, accumulator variable, first line of the definition). Lines are checked against the file.
FUNCS = [
    ("status_calc_str", "str"), ("status_calc_agi", "agi"), ("status_calc_vit", "vit"), ("status_calc_int", "int_"),
    ("status_calc_dex", "dex"), ("status_calc_luk", "luk"),
    ("status_calc_batk", "batk"), ("status_calc_watk", "watk"), ("status_calc_critical", "critical"), ("status_calc_hit", "hit"),
    ("status_calc_flee", "flee"), ("status_calc_flee2", "flee2"), ("status_calc_def", "def"), ("status_calc_def2", "def2"),
    ("status_calc_mdef", "mdef"), ("status_calc_mdef2", "mdef2"),
    ("status_calc_aspd_rate", "aspd_rate"), ("status_calc_fix_aspd", "aspd"),
    ("status_calc_patk", "patk"), ("status_calc_smatk", "smatk"), ("status_calc_res", "res"), ("status_calc_mres", "mres"),
    ("status_calc_hplus", "hplus"), ("status_calc_crate", "crate"),
    ("status_calc_pow", "pow"), ("status_calc_sta", "sta"), ("status_calc_wis", "wis"), ("status_calc_spl", "spl"),
    ("status_calc_con", "con"), ("status_calc_crt", "crt"),
    ("status_calc_element", "element"), ("status_calc_element_lv", "lv"), ("status_calc_attack_element", "element"),
    ("status_calc_mode", "mode"), ("status_calc_speed", "speed"),
]
EXTRA_FUNCS = [  # status_get_*bonus are lowered like the others (acc = bonus); pseudobuff_matk is only scanned (hand written)
    ("status_get_hpbonus", "bonus"), ("status_get_spbonus", "bonus"), ("status_calc_pseudobuff_matk", None),
]
# read by status_calc_bl_main itself (SCB_MATK, status.cpp:6247-6268), not by a status_calc_* function
BL_MAIN_STATUSES = {"RECOGNIZEDSPELL": "matk_min = max(matk_min, matk_max)", "MAGICPOWER": "matk += matk * val3 / 100 while val4 is set"}
MACRO_STATUSES = {"PUSH_CART": "pc_iscarton(sd) under NEW_CARTS (mmo.hpp:1204) is sd->sc.getSCE(SC_PUSH_CART): read by status_calc_speed"}


# ---------------------------------------------------------------------------------------------------------------------
def load_pre(path):
    """Returns list of (lineno, text) with RENEWAL-only text blanked."""
    src = pathlib.Path(path).read_text().split("\n")
    out, stack = [], []
    active = lambda: all(s[0] for s in stack)
    for i, l in enumerate(src, 1):
        m = re.match(r"\s*#\s*(ifdef|ifndef|if|else|elif|endif)\b\s*(.*)", l)
        if m:
            d, arg = m.groups()
            arg = re.split(r"//|/\*", arg)[0].strip()
            if d in ("ifdef", "ifndef") and arg == "RENEWAL":
                stack.append([d == "ifndef", True]); out.append((i, "")); continue
            if d in ("ifdef", "ifndef", "if"):
                # RENEWAL_* sub flags (RENEWAL_ASPD ...) are NOT defined in a PRERE build either: treat like RENEWAL
                if re.match(r"RENEWAL_\w+$", arg):
                    stack.append([d == "ifndef", True]); out.append((i, "")); continue
                stack.append([True, False]); out.append((i, l)); continue
            if d == "else":
                if stack[-1][1]:
                    stack[-1][0] = not stack[-1][0]; out.append((i, "")); continue
                out.append((i, l)); continue
            if d == "endif":
                s = stack.pop(); out.append((i, "" if s[1] else l)); continue
            out.append((i, l)); continue
        out.append((i, l if active() else ""))
    return out


# ---------------------------------------------------------------------------------------------------------------------
TOK = re.compile(r"""
    (?P<ws>\s+)|(?P<lc>//[^\n]*)|(?P<bc>/\*.*?\*/)|
    (?P<num>0[xX][0-9a-fA-F]+|\d+)|
    (?P<id>[A-Za-z_]\w*)|
    (?P<str>"(?:\\.|[^"\\])*")|(?P<chr>'(?:\\.|[^'\\])*')|
    (?P<op><<=|>>=|->|\+\+|--|<<|>>|<=|>=|==|!=|&&|\|\||\+=|-=|\*=|/=|%=|&=|\|=|\^=|::|[-+*/%&|^~!<>=?:;,.(){}\[\]])
""", re.X | re.S)


def tokenize(lines):
    """lines: list of (lineno, text). Returns list of (kind, value, lineno)."""
    toks = []
    text = "\n".join(t for _, t in lines)
    # map offset -> line
    starts, off = [], 0
    for ln, t in lines:
        starts.append((off, ln)); off += len(t) + 1
    import bisect
    offs = [s for s, _ in starts]
    for m in TOK.finditer(text):
        k = m.lastgroup
        if k in ("ws", "lc", "bc"):
            continue
        ln = starts[bisect.bisect_right(offs, m.start()) - 1][1]
        v = m.group(k)
        toks.append((k, v, ln))
    return toks


class P:
    def __init__(self, toks, i=0):
        self.t, self.i = toks, i

    def peek(self, k=0):
        return self.t[self.i + k] if self.i + k < len(self.t) else ("eof", "", 0)

    def next(self):
        x = self.t[self.i]; self.i += 1; return x

    def accept(self, v):
        if self.peek()[1] == v and self.peek()[0] in ("op", "id"):
            self.i += 1; return True
        return False

    def expect(self, v):
        if not self.accept(v):
            raise SyntaxError(f"line {self.peek()[2]}: expected {v!r} got {self.peek()[1]!r}")


BINPREC = [("||",), ("&&",), ("|",), ("^",), ("&",), ("==", "!="), ("<", ">", "<=", ">="), ("<<", ">>"), ("+", "-"), ("*", "/", "%")]
ASSIGN = ("=", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<=", ">>=")
TYPES = {"int", "int32", "int16", "int8", "uint16", "uint32", "uint8", "int64", "uint64", "defType", "unsigned", "char", "short", "long", "bool", "float", "double", "size_t", "signed"}


def parse_expr(p):
    return parse_assign(p)


def parse_assign(p):
    lhs = parse_cond(p)
    t = p.peek()
    if t[0] == "op" and t[1] in ASSIGN:
        p.next()
        rhs = parse_assign(p)
        return ("assign", t[1], lhs, rhs, t[2])
    return lhs


def parse_cond(p):
    c = parse_bin(p, 0)
    if p.accept("?"):
        a = parse_assign(p); p.expect(":"); b = parse_cond(p)
        return ("cond", c, a, b)
    return c


def parse_bin(p, lvl):
    if lvl == len(BINPREC):
        return parse_unary(p)
    a = parse_bin(p, lvl + 1)
    while p.peek()[0] == "op" and p.peek()[1] in BINPREC[lvl]:
        op = p.next()[1]
        b = parse_bin(p, lvl + 1)
        a = ("bin", op, a, b)
    return a


def parse_unary(p):
    t = p.peek()
    if t[0] == "op" and t[1] in ("!", "-", "+", "~"):
        p.next(); return ("un", t[1], parse_unary(p))
    if t[0] == "op" and t[1] == "(":
        # cast?
        j = p.i + 1
        names = []
        while p.t[j][0] == "id" and (p.t[j][1] in TYPES):
            names.append(p.t[j][1]); j += 1
        if names and p.t[j][1] == ")":
            p.i = j + 1
            return ("cast", " ".join(names), parse_unary(p))
    return parse_postfix(p)


def parse_postfix(p):
    e = parse_primary(p)
    while True:
        t = p.peek()
        if t[0] == "op" and t[1] == "->" or (t[0] == "op" and t[1] == "."):
            p.next()
            name = p.next()[1]
            if p.peek()[1] == "(" and name in ("getSCE", "hasSCE") and e == ("id", "sc"):
                p.next()
                a = parse_assign(p)
                p.expect(")")
                if a[0] == "assign" and a[1] == "=":   # getSCE(i = SC_X): keep the status, drop the variable
                    a = a[3]
                if a[0] == "id" and a[1].startswith("SC_"):
                    e = ("sce", a[1][3:])
                else:   # getSCE(i) with a variable: only the hand-written potion code does that
                    e = ("scevar", json.dumps(a))
            elif p.peek()[1] == "(":
                p.next(); args = []
                while not p.accept(")"):
                    args.append(parse_assign(p)); p.accept(",")
                e = ("mcall", e, name, args)
            else:
                e = ("member", e, name)
        elif t[0] == "op" and t[1] == "[":
            p.next(); idx = parse_assign(p); p.expect("]"); e = ("index", e, idx)
        elif t[0] == "op" and t[1] == "(" and e[0] == "id":
            p.next(); args = []
            while not p.accept(")"):
                args.append(parse_assign(p)); p.accept(",")
            e = ("call", e[1], args, p.peek()[2])
        elif t[0] == "op" and t[1] in ("++", "--"):
            p.next(); e = ("post", t[1], e)
        else:
            return e


def parse_primary(p):
    t = p.next()
    if t[0] == "num":
        return ("num", int(t[1], 0))
    if t[0] == "id":
        if t[1] == "nullptr":
            return ("null",)
        return ("id", t[1])
    if t[1] == "(":
        e = parse_assign(p); p.expect(")"); return ("paren", e)
    raise SyntaxError(f"line {t[2]}: unexpected {t[1]!r}")


def parse_stmt(p):
    t = p.peek()
    line = t[2]
    if t[1] == "{":
        p.next(); body = []
        while not p.accept("}"):
            body.append(parse_stmt(p))
        return ("block", body, line)
    if t[1] == "if":
        p.next(); p.expect("("); c = parse_expr(p); p.expect(")")
        a = parse_stmt(p); b = None
        if p.peek()[1] == "else":
            p.next(); b = parse_stmt(p)
        return ("if", c, a, b, line)
    if t[1] == "return":
        p.next()
        e = None if p.peek()[1] == ";" else parse_expr(p)
        p.expect(";")
        return ("return", e, line)
    if t[1] in ("switch", "for", "while", "do"):
        # skip structurally: consume balanced block, report unsupported
        start = p.i; p.next()
        depth = 0
        while True:
            x = p.next()
            if x[1] == "{": depth += 1
            elif x[1] == "}":
                depth -= 1
                if depth == 0: break
            elif x[1] == ";" and depth == 0 and t[1] in ("for", "while"): break
        return ("unsupported", t[1], line)
    if t[0] == "id" and (t[1] in TYPES or (t[1] in ("struct", "enum", "const", "static") ) or (p.peek(1)[0] == "op" and p.peek(1)[1] == "*" and p.peek(2)[0] == "id" and p.peek(3)[1] in ("=", ";"))):
        # declaration: swallow to ';'
        depth = 0
        while True:
            x = p.next()
            if x[1] in "({[": depth += 1
            elif x[1] in ")}]": depth -= 1
            elif x[1] == ";" and depth == 0: break
        return ("decl", line)
    if t[1] == ";":
        p.next(); return ("empty", line)
    e = parse_expr(p)
    p.expect(";")
    return ("expr", e, line)


def find_function(toks, name):
    """Index of the '{' that starts the body of the definition `name(` ... `)` {."""
    for i in range(len(toks) - 1):
        if toks[i][1] == name and toks[i + 1][1] == "(" and (i == 0 or toks[i - 1][1] != "return"):
            # definition: ... ) {   (not a declaration ending with ';')
            depth, j = 0, i + 1
            while True:
                if toks[j][1] == "(": depth += 1
                elif toks[j][1] == ")":
                    depth -= 1
                    if depth == 0: break
                j += 1
            if toks[j + 1][1] == "{":
                return j + 1, toks[i][2]
    raise KeyError(name)


def parse_function(toks, name):
    i, line = find_function(toks, name)
    p = P(toks, i + 1)
    body = []
    while not p.accept("}"):
        body.append(parse_stmt(p))
    return body, line


# ---------------------------------------------------------------------------------------------------------------------
# expression helpers
def strip_paren(e):
    while e[0] == "paren":
        e = e[1]
    return e


def mentions(e, name):
    if isinstance(e, tuple):
        if e[0] == "id" and e[1] == name:
            return True
        return any(mentions(x, name) for x in e[1:] if isinstance(x, (tuple, list)))
    if isinstance(e, list):
        return any(mentions(x, name) for x in e)
    return False


def sces(e, out=None):
    out = [] if out is None else out
    if isinstance(e, tuple):
        if e[0] == "sce":
            out.append(e[1])
        for x in e[1:]:
            if isinstance(x, (tuple, list)):
                sces(x, out)
    elif isinstance(e, list):
        for x in e:
            sces(x, out)
    return out


def trigger(e):
    """Set of statuses of which at least one must be active for `e` to be true; None = can be true with nothing active."""
    e = strip_paren(e)
    if e[0] == "sce":
        return {e[1]}
    if e[0] == "bin" and e[1] == "&&":
        a, b = trigger(e[2]), trigger(e[3])
        if a is None: return b
        if b is None: return a
        return a if len(a) <= len(b) else b
    if e[0] == "bin" and e[1] == "||":
        a, b = trigger(e[2]), trigger(e[3])
        if a is None or b is None: return None
        return a | b
    if e[0] == "bin" and e[1] in ("!=", "==", "<", ">", "<=", ">=") and sces(e):
        # comparison that reads a status value: false when absent, but reading ->valN of an absent sce is a crash in C, so the
        # source always guards it with &&; treat as 'always' here
        return None
    if e[0] == "assign":   # (sce = sc->getSCE(X))
        return trigger(e[3])
    if e[0] == "bin" and e[1] == "!=" and e[3] == ("null",):
        return trigger(e[2])
    return None


# ---------------------------------------------------------------------------------------------------------------------
class Entry(dict):
    pass


GROUP_VARS = {"status_calc_speed": ("val", "speed_rate"), "status_calc_aspd_rate": ("max",)}


def lower(stmts, guard, fn, accvar, out, unsup):
    for s in stmts:
        k = s[0]
        if k == "block":
            lower(s[1], guard, fn, accvar, out, unsup)
        elif k == "if":
            _, c, a, b, line = s
            lower([a], guard + [c], fn, accvar, out, unsup)
            if b is not None:
                lower([b], guard + [("un", "!", c)], fn, accvar, out, unsup)
        elif k == "return":
            out.append(Entry(fn=fn, line=s[2], guard=guard, kind="return", rhs=s[1]))
        elif k == "expr":
            e = strip_paren(s[1])
            if e[0] == "assign" and e[2][0] == "id" and e[2][1] in GROUP_VARS.get(fn, ()):
                out.append(Entry(fn=fn, line=s[2], guard=guard, kind="gassign", var=e[2][1], op=e[1], rhs=e[3]))
            elif e[0] == "assign" and mentions(e[2], accvar) and e[2][0] == "id":
                out.append(Entry(fn=fn, line=s[2], guard=guard, kind="assign", op=e[1], rhs=e[3]))
            elif e[0] == "assign":
                unsup.append((fn, s[2], "assignment to other variable " + json.dumps(e[2])))
                out.append(Entry(fn=fn, line=s[2], guard=guard, kind="other", expr=e))
            else:
                unsup.append((fn, s[2], "expression statement " + e[0]))
                out.append(Entry(fn=fn, line=s[2], guard=guard, kind="other", expr=e))
        elif k in ("decl", "empty"):
            if k == "decl":
                out.append(Entry(fn=fn, line=s[1], guard=guard, kind="decl"))
        elif k == "unsupported":
            unsup.append((fn, s[2], s[1]))
            out.append(Entry(fn=fn, line=s[2], guard=guard, kind="unsupported", what=s[1]))


def to_json(e):
    if isinstance(e, tuple):
        return [to_json(x) for x in e]
    if isinstance(e, list):
        return [to_json(x) for x in e]
    return e


def build(cpp):
    lines = load_pre(cpp)
    toks = tokenize(lines)
    res, unsup = {}, []
    for name, acc in FUNCS + EXTRA_FUNCS:
        try:
            body, line = parse_function(toks, name)
        except SyntaxError as ex:   # hand-written functions with C++17 constructs: scan the tokens for SC_ names instead
            assert acc is None, (name, ex)
            i, line = find_function(toks, name)
            depth, j, names = 0, i, []
            while True:
                if toks[j][1] == "{": depth += 1
                elif toks[j][1] == "}":
                    depth -= 1
                    if depth == 0: break
                elif toks[j][0] == "id" and toks[j][1].startswith("SC_"): names.append(("sce", toks[j][1][3:]))
                j += 1
            res[name] = {"line": line, "acc": acc, "entries": [], "body": names}
            continue
        ent = []
        lower(body, [], name, acc or "bonus", ent, unsup)
        res[name] = {"line": line, "acc": acc, "entries": ent, "body": body}
    return res, unsup, toks


def statuses_of(entries_or_body):
    return sorted(set(sces(entries_or_body)))


def cmd_list(a):
    res, unsup, _ = build(a.status_cpp)
    allsc = {}
    print("function                          line  #statuses  statuses")
    for name, r in res.items():
        sc = statuses_of(r["body"])
        for s in sc:
            allsc.setdefault(s, []).append(name.replace("status_", ""))
        print(f"{name:32s} {r['line']:5d} {len(sc):9d}")
    print()
    print(f"distinct statuses referenced by the covered functions: {len(allsc)}")
    if a.verbose:
        for s in sorted(allsc):
            print(f"  SC_{s:32s} {','.join(allsc[s])}")
    for m, why in MACRO_STATUSES.items():
        allsc.setdefault(m, []).append("calc_speed (macro)")
    for m, why in BL_MAIN_STATUSES.items():
        allsc.setdefault(m, []).append("calc_bl_main")
    print(f"statuses incl. macro-hidden and bl_main ones: {len(allsc)}")
    print("unsupported/hand-written statements:", len(unsup))
    for u in unsup:
        print("  ", u)


# constants the Luau side needs the legacy values of (dumped by the hook; checked against scripts/status/consts.luau)
CONSTS = ["BL_PC", "BL_MOB", "BL_HOM", "BL_MER", "BL_ELEM", "BREAK_ANKLE", "BREAK_KNEE", "BREAK_WRIST", "BREAK_SHOULDER", "BREAK_WAIST",
          "BCT_ENEMY", "BCT_SELF", "ELE_NEUTRAL", "ELE_WATER", "ELE_EARTH", "ELE_FIRE", "ELE_WIND", "ELE_POISON", "ELE_HOLY",
          "ELE_DARK", "ELE_GHOST", "ELE_UNDEAD", "SL_HIGH", "SL_BARDDANCER", "MD_MASK", "MD_NONE", "DEFTYPE_MIN", "DEFTYPE_MAX",
          "MIN_ASPD", "MIN_WALK_SPEED", "MAX_WALK_SPEED", "AMOTION_INTERVAL", "AMOTION_ZERO_ASPD", "MAPID_ASSASSIN", "MAPID_SECONDMASK",
          "MAPID_SWORDMAN", "OPTION_DRAGON", "OPTION_RIDING", "OPTION_WUGRIDER", "OPTION_MADOGEAR", "OPTION_CART", "IT_WEAPON", "INT_MAX", "UCHAR_MAX", "USHRT_MAX",
          "SHRT_MAX", "SHRT_MIN", "W_MUSICAL", "W_WHIP", "W_KATAR", "W_BOW", "W_REVOLVER", "W_RIFLE", "W_GATLING", "W_SHOTGUN", "W_GRENADE", "CG_MOONLIT", "CG_HERMODE",
          "MANNER_NOCHAT", "MANNER_NOITEM", "LR_FLAG_NONE", "LR_FLAG_WEAPON", "MF_BATTLEGROUND", "MF_GVG",
          "SA_FREECAST", "LG_EXEEDBREAK", "NC_MADOLICENCE", "RA_WUGRIDER", "RG_TUNNELDRIVE", "TF_MISS", "MC_PUSHCART", "BA_MUSICALLESSON",
          "DC_DANCINGLESSON", "NV_TRANSCENDENCE", "CR_TRUST", "STATUS_BONUS_FIX", "STATUS_BONUS_RATE"]


def sc_enum(status_hpp):
    """Names of the SC_ enumerators of `enum sc_type` (without the SC_ prefix), in declaration order."""
    txt = pathlib.Path(status_hpp).read_text().split("\n")
    out, inside = [], False
    for l in txt:
        if l.startswith("enum sc_type"):
            inside = True; continue
        if inside and l.startswith("};"):
            break
        if inside:
            m = re.match(r"\s*SC_([A-Z0-9_]+)\b", re.sub(r"//.*", "", l))
            if m and m.group(1) not in ("NONE", "MAX", "COMMON_MIN", "COMMON_MAX", "BEGIN", "END"):
                out.append(m.group(1))
    return out


def enum_names(hpp, enum, prefix, skip=("NONE", "MAX")):
    txt = pathlib.Path(hpp).read_text().split("\n")
    out, inside = [], False
    for l in txt:
        if l.startswith("enum " + enum):
            inside = True; continue
        if inside and l.startswith("};"):
            break
        if inside:
            m = re.match(r"\s*" + prefix + r"([A-Z0-9_]+)\b", re.sub(r"//.*", "", l))
            if m and m.group(1) not in skip:
                out.append(m.group(1))
    return out


def cmd_incs(a):
    out = pathlib.Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    for enum, prefix, fname in (("e_scs_flag", "SCS_", "golden_status_scs.inc"), ("e_scb_flag", "SCB_", "golden_status_scb.inc"),
                                ("e_status_change_flag", "SCF_", "golden_status_scf.inc")):
        (out / fname).write_text("".join(f"GS( {n} )\n" for n in enum_names(a.status_hpp, enum, prefix)))
    names = sc_enum(a.status_hpp)
    (out / "golden_status_sc.inc").write_text("".join(f'\t{{ "{n}", SC_{n} }},\n' for n in names))
    (out / "golden_status_consts.inc").write_text("".join(f"GK( {n} )\n" for n in CONSTS))
    print(f"golden_status_sc.inc: {len(names)} statuses; golden_status_consts.inc: {len(CONSTS)} constants")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd", choices=["list", "ir", "incs"])
    ap.add_argument("--status-hpp", default=DEFAULT_CPP.replace(".cpp", ".hpp"))
    ap.add_argument("--status-cpp", default=DEFAULT_CPP)
    ap.add_argument("--out")
    ap.add_argument("-v", "--verbose", action="store_true")
    a = ap.parse_args()
    if a.cmd == "list":
        cmd_list(a)
    elif a.cmd == "incs":
        cmd_incs(a)
    else:
        res, unsup, _ = build(a.status_cpp)
        out = {n: {"line": r["line"], "acc": r["acc"], "entries": [to_json(dict(e)) for e in r["entries"]]} for n, r in res.items()}
        pathlib.Path(a.out).write_text(json.dumps({"funcs": out, "unsupported": unsup}, indent=1, sort_keys=True, default=str))


if __name__ == "__main__":
    main()
