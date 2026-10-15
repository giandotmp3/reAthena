#!/usr/bin/env python3
"""Generate the full typed battle-config knob table for the reimagined core from legacy sources.

The legacy map server declares every battle setting once, in `battle_data[]` in src/map/battle.cpp:

    { "conf_key", &battle_config.field, default, min, max },

This script reads that table and the shipped conf/battle/*.conf files and writes

    modernized/rathena-reimagined/core/include/rathena/core/battle_knobs.gen.hpp

which `knobs.def.hpp` includes to complete `RATHENA_BATTLE_KNOBS`. Run it after any change to
battle_data[], conf/battle/*.conf or the hand-typed knobs; CI fails when the output is stale:

    python3 tools/codegen/battle_knobs.py           # regenerate
    python3 tools/codegen/battle_knobs.py --check   # exit 1 if out of date
    python3 tools/codegen/battle_knobs.py --report  # also print the code-vs-conf differences

Rules (spec §5.6, "the value shipped in conf/ is the code default"):
  * shipped default = the value in conf/battle/*.conf (what an operator gets); for knobs the conf
    files do not set (several are commented out on purpose because the official value depends on
    the game mode), it is the legacy code default for that mode.
  * `#ifdef RENEWAL` / `#else` inside battle_data[] gives separate renewal and pre-renewal code
    defaults. Both are emitted; the typed default is the renewal one (the legacy build's default
    mode, src/config/renewal.hpp) and RATHENA_BATTLE_PRE_RENEWAL_DEFAULTS lists the knobs whose
    pre-renewal shipped default differs.
  * `#ifdef VIP_ENABLE` is handled the same way: the shipped build has VIP_ENABLE off
    (src/config/core.hpp); the VIP variant only widens/narrows bounds and is folded into the
    typed bound envelope. Any other preprocessor condition inside battle_data[] is an error.
  * symbolic values (BL_* bitmasks, INT_MAX, MAX_LEVEL, enum constants, ...) are evaluated from
    the legacy headers by a small preprocessor that follows #ifdef/#ifndef/#else for RENEWAL and
    VIP_ENABLE. A symbol that cannot be resolved unambiguously stops the generator.
  * typed bounds are the legacy [min, max] envelope over all modes; a knob whose bounds are
    exactly [0, 1] becomes `bool`.
  * knobs already typed by hand in knobs.def.hpp (RATHENA_BATTLE_KNOBS_TYPED, used by the rules)
    keep their name, type and bounds; they are checked against battle_data[] and left out of the
    generated list.
"""
from __future__ import annotations

import argparse
import ast
import itertools
import keyword
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BATTLE_CPP = ROOT / "src/map/battle.cpp"
CUSTOM_DIR = ROOT / "src"  # `#include <custom/...>` is relative to src/
CONF_ROOT = ROOT / "conf"
BATTLE_ATHENA_CONF = CONF_ROOT / "battle_athena.conf"
CORE_INCLUDE = ROOT / "modernized/rathena-reimagined/core/include/rathena/core"
KNOBS_DEF = CORE_INCLUDE / "knobs.def.hpp"
OUTPUT = CORE_INCLUDE / "battle_knobs.gen.hpp"
GENERATOR = "tools/codegen/battle_knobs.py"

# Headers scanned (in this order) for #define and enum constants used by battle_data[].
HEADERS = [
    "src/config/core.hpp",
    "src/common/mmo.hpp",
    "src/map/path.hpp",
    "src/map/map.hpp",
    "src/map/mob.hpp",
    "src/map/skill.hpp",
    "src/map/status.hpp",
]

# Build switches that may appear in battle_data[] / the headers above. Each mode is one
# combination; RENEWAL on + VIP_ENABLE off is the legacy shipped build.
FEATURES = ("RENEWAL", "VIP_ENABLE")
SHIPPED_VIP = False

# <climits>/<cstdint> limits for the platforms rAthena supports (int is 32-bit everywhere).
STD_LIMITS = {
    "CHAR_MAX": 127, "CHAR_MIN": -128, "SCHAR_MAX": 127, "SCHAR_MIN": -128, "UCHAR_MAX": 255,
    "SHRT_MAX": 32767, "SHRT_MIN": -32768, "USHRT_MAX": 65535,
    "INT_MAX": 2147483647, "INT_MIN": -2147483648, "UINT_MAX": 4294967295,
    "INT8_MAX": 127, "INT8_MIN": -128, "UINT8_MAX": 255,
    "INT16_MAX": 32767, "INT16_MIN": -32768, "UINT16_MAX": 65535,
    "INT32_MAX": 2147483647, "INT32_MIN": -2147483648, "UINT32_MAX": 4294967295,
}
I32_MIN, I32_MAX = -(2**31), 2**31 - 1

# Keys the legacy battle_config_read() handles itself (not battle_data[] entries).
CONF_SPECIAL_KEYS = {"import", "atcommand_symbol", "charcommand_symbol"}
# Members of the generated class; a knob may not shadow them.
RESERVED_NAMES = {"apply", "validate", "schema", "from_config", "overlaid_with", "shipped"}


class GenError(Exception):
    pass


def rel(p: Path) -> str:
    return p.relative_to(ROOT).as_posix()


# --------------------------------------------------------------------------- C text helpers


def strip_comments(text: str) -> str:
    """Removes // and /* */ comments, keeping string literals and line structure intact."""
    out: list[str] = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == '"' or c == "'":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            out.append(text[i : j + 1])
            i = j + 1
        elif text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j < 0 else j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            if j < 0:
                raise GenError("unterminated block comment")
            out.append("\n" * text.count("\n", i, j))  # keep line numbers
            i = j + 2
        else:
            out.append(c)
            i += 1
    return "".join(out)


DIRECTIVE = re.compile(r"^\s*#\s*(\w+)\s*(.*?)\s*$")


@dataclass
class CondStack:
    """#if nesting. Each frame: (state, taken) where state is True/False/None (unknown)."""

    frames: list[list] = field(default_factory=list)

    def active(self) -> bool | None:
        state: bool | None = True
        for s, _ in self.frames:
            if s is False:
                return False
            if s is None:
                state = None
        return state

    def push(self, state: bool | None) -> None:
        self.frames.append([state, state])

    def flip(self) -> None:
        frame = self.frames[-1]
        frame[0] = None if frame[0] is None else not frame[1]

    def pop(self) -> None:
        self.frames.pop()


# --------------------------------------------------------------------------- symbol resolution


SUFFIX = re.compile(r"\b((?:0[xX][0-9a-fA-F]+)|\d+)[uUlL]+\b")


class Resolver:
    """Evaluates integer constant expressions against the legacy headers for one build mode."""

    def __init__(self, mode: dict[str, bool]):
        self.mode = mode
        self.defines: dict[str, list[tuple[str, str]]] = {}  # name -> [(body, where)]
        self.uncertain: dict[str, str] = {}  # names defined only under conditions we cannot evaluate
        self.enumerators: dict[str, list[tuple[tuple, str]]] = {}
        for h in HEADERS:
            self._scan(ROOT / h)
        self._cache: dict[str, int] = {}
        self._resolving: set[str] = set()

    def _is_defined(self, name: str) -> bool | None:
        if name in self.mode:
            return self.mode[name]
        if name in self.defines:
            return True
        if name in self.uncertain:
            return None
        return False

    def _scan(self, path: Path) -> None:
        text = strip_comments(path.read_text(encoding="utf-8", errors="replace"))
        text = text.replace("\\\n", " ")
        cond = CondStack()
        active_lines: list[str] = []
        for lineno, line in enumerate(text.split("\n"), 1):
            where = f"{rel(path)}:{lineno}"
            m = DIRECTIVE.match(line)
            if not m:
                if cond.active() is True:
                    active_lines.append(line)
                continue
            d, rest = m.group(1), m.group(2)
            if d in ("ifdef", "ifndef"):
                state = self._is_defined(rest.split()[0])
                if state is not None and d == "ifndef":
                    state = not state
                cond.push(state)
            elif d == "if":
                cond.push(self._eval_if(rest))
            elif d == "elif":
                cond.frames[-1] = [None, None]  # any #elif chain is treated as unknown
            elif d == "else":
                cond.flip()
            elif d == "endif":
                cond.pop()
            elif d == "define":
                dm = re.match(r"(\w+)(\()?\s*(.*)$", rest)
                if not dm or dm.group(2):  # function-like macros are never needed here
                    continue
                name, body = dm.group(1), dm.group(3).strip()
                state = cond.active()
                if state is True:
                    self.defines.setdefault(name, []).append((body, where))
                elif state is None:
                    self.uncertain[name] = where
            elif d == "undef":
                if cond.active() is True:
                    self.defines.pop(rest.split()[0], None)
        self._scan_enums("\n".join(active_lines), rel(path))

    def _eval_if(self, expr: str) -> bool | None:
        m = re.fullmatch(r"(!)?\s*defined\s*\(?\s*(\w+)\s*\)?", expr)
        if m:
            state = self._is_defined(m.group(2))
            return state if state is None or not m.group(1) else not state
        return None

    def _scan_enums(self, text: str, where: str) -> None:
        for m in re.finditer(r"\benum\s+(class\s+|struct\s+)?(\w+)?\s*(?::\s*[\w:\s]+?)?\s*\{([^{}]*)\}", text):
            if m.group(1):
                continue  # scoped enums are never referenced unqualified
            prev: str | None = None
            for item in m.group(3).split(","):
                item = item.strip()
                if not item:
                    continue
                em = re.fullmatch(r"(\w+)\s*(?:=\s*(.+))?", item, re.S)
                if not em:
                    prev = None  # unparsable enumerator: later implicit values are unknown
                    continue
                name, expr = em.group(1), em.group(2)
                if expr is not None:
                    spec: tuple = ("expr", expr.strip())
                elif prev is None:
                    spec = ("expr", "0") if item == m.group(3).split(",")[0].strip() else ("unknown",)
                else:
                    spec = ("next", prev)
                self.enumerators.setdefault(name, []).append((spec, f"{where} enum {m.group(2) or '<anon>'}"))
                prev = name

    def value(self, name: str) -> int:
        if name in self._cache:
            return self._cache[name]
        if name in STD_LIMITS:
            return STD_LIMITS[name]
        if name in self._resolving:
            raise GenError(f"recursive definition of {name}")
        self._resolving.add(name)
        try:
            candidates: list[tuple[int, str]] = []
            for body, where in self.defines.get(name, []):
                candidates.append((self.eval(body, f"#define {name} ({where})"), where))
            for spec, where in self.enumerators.get(name, []):
                if spec[0] == "expr":
                    candidates.append((self.eval(spec[1], f"{name} ({where})"), where))
                elif spec[0] == "next":
                    candidates.append((self.value(spec[1]) + 1, where))
                else:
                    raise GenError(f"cannot determine the value of enumerator {name} ({where})")
            if not candidates:
                extra = f" (only defined under an unevaluated condition at {self.uncertain[name]})" if name in self.uncertain else ""
                raise GenError(f"unresolved symbol {name} in mode {self.mode}{extra}")
            values = {v for v, _ in candidates}
            if len(values) != 1:
                raise GenError(f"ambiguous symbol {name} in mode {self.mode}: {candidates}")
            self._cache[name] = candidates[0][0]
            return candidates[0][0]
        finally:
            self._resolving.discard(name)

    def eval(self, expr: str, context: str) -> int:
        src = SUFFIX.sub(r"\1", expr.strip())
        try:
            tree = ast.parse(src, mode="eval")
        except SyntaxError as e:
            raise GenError(f"cannot parse '{expr}' in {context}") from e
        return self._eval_node(tree.body, expr, context)

    def _eval_node(self, node: ast.AST, expr: str, context: str) -> int:
        if isinstance(node, ast.Constant) and type(node.value) is int:
            return node.value
        if isinstance(node, ast.Name):
            return self.value(node.id)
        if isinstance(node, ast.UnaryOp):
            v = self._eval_node(node.operand, expr, context)
            ops = {ast.Invert: lambda a: ~a, ast.USub: lambda a: -a, ast.UAdd: lambda a: a}
            if type(node.op) in ops:
                return ops[type(node.op)](v)
        if isinstance(node, ast.BinOp):
            a = self._eval_node(node.left, expr, context)
            b = self._eval_node(node.right, expr, context)
            ops = {
                ast.BitOr: lambda: a | b, ast.BitAnd: lambda: a & b, ast.BitXor: lambda: a ^ b,
                ast.Add: lambda: a + b, ast.Sub: lambda: a - b, ast.Mult: lambda: a * b,
                ast.LShift: lambda: a << b, ast.RShift: lambda: a >> b,
                # C integer division and remainder truncate toward zero
                ast.Div: lambda: c_div(a, b),
                ast.Mod: lambda: a - b * c_div(a, b),
            }
            if type(node.op) in ops:
                return ops[type(node.op)]()
        raise GenError(f"unsupported construct in '{expr}' ({context}): {ast.dump(node)}")


def c_div(a: int, b: int) -> int:
    if b == 0:
        raise GenError("division by zero")
    q = abs(a) // abs(b)
    return q if (a < 0) == (b < 0) else -q


# --------------------------------------------------------------------------- battle_data[]


ENTRY = re.compile(
    r'^\{\s*"(?P<key>[^"]+)"\s*,\s*&\s*battle_config\s*\.\s*(?P<field>\w+)\s*,'
    r"(?P<defv>[^,]+),(?P<min>[^,]+),(?P<max>[^,}]+),?\s*\}\s*,?$"
)


@dataclass
class Entry:
    key: str
    field: str
    exprs: tuple[str, str, str]  # default, min, max (legacy source text)
    cond: tuple[tuple[str, bool], ...]
    where: str


def read_battle_data() -> list[Entry]:
    text = BATTLE_CPP.read_text(encoding="utf-8")
    start = re.search(r"^\}\s*battle_data\[\]\s*=\s*\{\s*$", text, re.M)
    if not start:
        raise GenError(f"battle_data[] not found in {rel(BATTLE_CPP)}")
    first_line = text.count("\n", 0, start.end()) + 2
    body_end = text.find("\n};", start.end())
    lines = strip_comments(text[start.end() : body_end]).split("\n")[1:]
    entries: list[Entry] = []
    _read_entries(lines, rel(BATTLE_CPP), first_line, [], entries)
    return entries


def _read_entries(lines: list[str], path: str, first_line: int, cond: list[tuple[str, bool]], out: list[Entry]) -> None:
    depth_at_start = len(cond)
    for i, raw in enumerate(lines):
        where = f"{path}:{first_line + i}"
        line = raw.strip()
        if not line:
            continue
        m = DIRECTIVE.match(line)
        if m:
            d, rest = m.group(1), m.group(2)
            if d in ("ifdef", "ifndef"):
                sym = rest.split()[0]
                if sym not in FEATURES:
                    raise GenError(f"{where}: unhandled condition #{d} {rest} inside battle_data[]")
                cond.append((sym, d == "ifdef"))
            elif d == "else":
                if len(cond) <= depth_at_start:
                    raise GenError(f"{where}: #else without #ifdef")
                sym, pol = cond.pop()
                cond.append((sym, not pol))
            elif d == "endif":
                if len(cond) <= depth_at_start:
                    raise GenError(f"{where}: #endif without #ifdef")
                cond.pop()
            elif d == "include":
                inc = re.fullmatch(r"[<\"]([^>\"]+)[>\"]", rest)
                if not inc:
                    raise GenError(f"{where}: cannot parse #include {rest}")
                inc_path = CUSTOM_DIR / inc.group(1)
                inc_text = strip_comments(inc_path.read_text(encoding="utf-8"))
                _read_entries(inc_text.split("\n"), rel(inc_path), 1, cond, out)
            else:
                raise GenError(f"{where}: unhandled directive #{d} {rest} inside battle_data[]")
            continue
        m = ENTRY.match(line)
        if not m:
            raise GenError(f"{where}: cannot parse battle_data[] line: {line}")
        exprs = (m.group("defv").strip(), m.group("min").strip(), m.group("max").strip())
        out.append(Entry(m.group("key"), m.group("field"), exprs, tuple(cond), where))
    if len(cond) != depth_at_start:
        raise GenError(f"{path}: unbalanced #ifdef inside battle_data[]")


# --------------------------------------------------------------------------- conf/battle


def read_shipped_conf() -> tuple[dict[str, tuple[int, str, str]], list[str]]:
    """Values the legacy loader ends up with from conf/battle_athena.conf, without operator imports.

    Returns ({key: (value, raw, file)}, notes). Mirrors battle_config_read(): lines starting with
    "//" are skipped, `key: value` is split at the first ':', the value is the first word, later
    definitions win, `import:` is followed in order.
    """
    values: dict[str, tuple[int, str, str]] = {}
    notes: list[str] = []

    def read(path: Path) -> None:
        for lineno, line in enumerate(path.read_text(encoding="utf-8").split("\n"), 1):
            if line.startswith("//"):
                continue
            m = re.match(r"([^:]+):\s*(\S+)", line)
            if not m:
                continue
            key, raw = m.group(1), m.group(2)
            where = f"{rel(path)}:{lineno}"
            if key.lower() == "import":
                target = ROOT / raw
                if raw.startswith("conf/import/"):
                    continue  # the operator's overlay, not part of the shipped defaults
                if not target.is_file():
                    raise GenError(f"{where}: import of missing file {raw}")
                read(target)
                continue
            if key in CONF_SPECIAL_KEYS:
                continue
            value = conf_switch(raw, where)
            if key in values and values[key][0] != value:
                notes.append(f"{key}: set twice ({values[key][2]} = {values[key][1]}, {where} = {raw}); the later wins")
            values[key] = (value, raw, rel(path))

    read(BATTLE_ATHENA_CONF)
    return values, notes


def conf_switch(raw: str, where: str) -> int:
    low = raw.lower()
    if low in ("on", "yes", "oui", "ja", "si", "sim"):
        return 1
    if low in ("off", "no", "non", "nein", "nao"):
        return 0
    m = re.fullmatch(r"[+-]?(0[xX][0-9a-fA-F]+|0[0-7]*|[1-9]\d*)", raw)
    if not m:
        raise GenError(f"{where}: value '{raw}' is not a number or switch")
    sign = -1 if raw.startswith("-") else 1
    digits = raw.lstrip("+-")
    base = 16 if digits[:2].lower() == "0x" else 8 if digits.startswith("0") and len(digits) > 1 else 10
    return sign * int(digits, base)


# --------------------------------------------------------------------------- hand-typed knobs


@dataclass
class TypedKnob:
    type: str
    name: str
    default: str
    lo: str
    hi: str


def read_typed_knobs() -> list[TypedKnob]:
    text = KNOBS_DEF.read_text(encoding="utf-8")
    m = re.search(r"#define RATHENA_BATTLE_KNOBS_TYPED\(X\)(.*?)(?:\n\s*\n|\n//)", text, re.S)
    if not m:
        raise GenError(f"RATHENA_BATTLE_KNOBS_TYPED(X) not found in {rel(KNOBS_DEF)}")
    body = strip_comments(m.group(1))
    knobs = [TypedKnob(*(p.strip() for p in x)) for x in re.findall(r"X\(([^,]+),([^,]+),([^,]+),([^,]+),([^)]+)\)", body)]
    if not knobs:
        raise GenError("no hand-typed battle knobs found")
    return knobs


# --------------------------------------------------------------------------- model


@dataclass
class Knob:
    key: str
    name: str
    field: str
    exprs: dict[str, tuple[str, str, str]]  # per mode label -> legacy expressions
    code: dict[tuple[bool, bool], tuple[int, int, int]]  # (renewal, vip) -> (default, min, max)
    conf: tuple[int, str, str] | None
    where: str
    typed: TypedKnob | None = None

    def shipped(self, renewal: bool) -> int:
        return self.conf[0] if self.conf else self.code[(renewal, SHIPPED_VIP)][0]

    @property
    def lo(self) -> int:
        return min(v[1] for v in self.code.values())

    @property
    def hi(self) -> int:
        return max(v[2] for v in self.code.values())

    @property
    def is_bool(self) -> bool:
        return self.lo == 0 and self.hi == 1

    def legacy(self, renewal: bool) -> tuple[int, int, int]:
        return self.code[(renewal, SHIPPED_VIP)]


def identifier(key: str) -> str:
    name = key.replace(".", "_")
    if not re.fullmatch(r"[A-Za-z_]\w*", name) or keyword.iskeyword(name):
        raise GenError(f"conf key '{key}' does not map to a C++ identifier")
    if name in RESERVED_NAMES or name in CPP_KEYWORDS:
        raise GenError(f"conf key '{key}' maps to the reserved name '{name}'")
    return name


CPP_KEYWORDS = set(
    "alignas alignof and and_eq asm auto bitand bitor bool break case catch char char8_t char16_t char32_t "
    "class compl concept const consteval constexpr constinit const_cast continue co_await co_return co_yield "
    "decltype default delete do double dynamic_cast else enum explicit export extern false float for friend "
    "goto if inline int long mutable namespace new noexcept not not_eq nullptr operator or or_eq private "
    "protected public register reinterpret_cast requires return short signed sizeof static static_assert "
    "static_cast struct switch template this thread_local throw true try typedef typeid typename union "
    "unsigned using virtual void volatile wchar_t while xor xor_eq".split()
)


def build_model() -> tuple[list[Knob], list[Entry], list[str]]:
    entries = read_battle_data()
    conf, notes = read_shipped_conf()
    modes = list(itertools.product([True, False], [False, True]))  # (renewal, vip)
    resolvers = {m: Resolver({"RENEWAL": m[0], "VIP_ENABLE": m[1]}) for m in modes}

    knobs: dict[str, Knob] = {}
    for renewal, vip in modes:
        seen: dict[str, Entry] = {}
        for e in entries:
            flags = {"RENEWAL": renewal, "VIP_ENABLE": vip}
            if not all(flags[sym] == pol for sym, pol in e.cond):
                continue
            if e.key.lower() in seen:
                first = seen[e.key.lower()]
                if (first.key, first.field, first.exprs) != (e.key, e.field, e.exprs):
                    raise GenError(f"{e.where}: duplicate battle_data[] key '{e.key}' differs from {first.where}")
                note = f"{e.key}: listed twice in battle_data[], identical; the first is used"
                if note not in notes:
                    notes.append(note)
                continue
            seen[e.key.lower()] = e
            ctx = f"{e.key} ({e.where})"
            values = tuple(resolvers[(renewal, vip)].eval(x, ctx) for x in e.exprs)
            for v, x in zip(values, e.exprs):
                if not I32_MIN <= v <= I32_MAX:
                    raise GenError(f"{ctx}: '{x}' = {v} does not fit int32")
            if values[1] > values[2]:
                raise GenError(f"{ctx}: min {values[1]} > max {values[2]}")
            k = knobs.get(e.key)
            if k is None:
                k = knobs[e.key] = Knob(e.key, identifier(e.key), e.field, {}, {}, conf.get(e.key), e.where)
            elif k.field != e.field:
                raise GenError(f"{ctx}: field {e.field} differs from {k.field} in another mode")
            label = ("re" if renewal else "pre") + ("+vip" if vip else "")
            k.exprs[label] = e.exprs
            k.code[(renewal, vip)] = values
    for k in knobs.values():
        if len(k.code) != len(modes):
            missing = [m for m in modes if m not in k.code]
            raise GenError(f"{k.key}: missing in modes {missing} (an entry that exists in one mode only is not handled)")

    names: dict[str, str] = {}
    for k in knobs.values():
        if k.name in names:
            raise GenError(f"conf keys '{names[k.name]}' and '{k.key}' map to the same identifier {k.name}")
        names[k.name] = k.key

    unknown = sorted(set(conf) - set(knobs))
    if unknown:
        raise GenError(f"conf/battle sets keys that battle_data[] does not know: {unknown}")

    for t in read_typed_knobs():
        k = knobs.get(t.name)
        if k is None:
            raise GenError(f"hand-typed knob '{t.name}' is not a battle_data[] conf key")
        k.typed = t
    ordered = [knobs[e.key] for e in entries if e.key in knobs and knobs[e.key].where == e.where]
    return ordered, entries, notes


# --------------------------------------------------------------------------- output


def lit(v: int) -> str:
    if v == I32_MAX:
        return "RATHENA_I32_MAX"
    if v == I32_MIN:
        return "RATHENA_I32_MIN"
    return str(v)


def symbolic(exprs: tuple[str, str, str]) -> bool:
    return any(not re.fullmatch(r"-?\d+", x) for x in exprs)


def conf_file_of(k: Knob) -> str:
    return k.conf[2] if k.conf else ""


def render(knobs: list[Knob], entries: list[Entry], notes: list[str]) -> str:
    gen = [k for k in knobs if k.typed is None]
    re_diff = [k for k in knobs if k.legacy(True) != k.legacy(False)]
    vip_diff = [k for k in knobs if k.code[(True, False)] != k.code[(True, True)] or k.code[(False, False)] != k.code[(False, True)]]
    out: list[str] = []
    w = out.append
    w("// GENERATED FILE - DO NOT EDIT.")
    w(f"// Regenerate with `python3 {GENERATOR}`; CI runs it with --check.")
    w("//")
    w(f"// Sources: {rel(BATTLE_CPP)} battle_data[] ({len(entries)} live entries, {len(knobs)} knobs), the shipped")
    w(f"// conf/battle/*.conf (read through {rel(BATTLE_ATHENA_CONF)}) and, for symbolic values:")
    for h in HEADERS:
        w(f"//   {h}")
    w("//")
    w("// RATHENA_BATTLE_KNOBS_GENERATED(X)   X(Type, name, shipped_default, min, max)")
    w("//   Every battle_data[] knob not typed by hand in knobs.def.hpp, grouped by the conf file that")
    w("//   sets it. shipped_default is the conf/battle value; for knobs no conf file sets it is the")
    w("//   legacy RENEWAL code default. [min, max] is the legacy range over every build mode. The")
    w("//   trailing comment keeps the legacy expression where it was symbolic.")
    w("// RATHENA_BATTLE_KNOB_CONF_KEYS(K)     K(name, \"conf.key\") where the conf key is not an identifier.")
    w("// RATHENA_BATTLE_PRE_RENEWAL_DEFAULTS(P)  P(name, value): pre-renewal shipped default, where it")
    w("//   differs from the renewal one (only knobs that no conf file sets).")
    w("// RATHENA_BATTLE_KNOB_LEGACY(L)       L(name, \"battle_config field\", renewal_default,")
    w("//   pre_renewal_default, renewal_min, renewal_max, pre_renewal_min, pre_renewal_max, \"conf file\")")
    w("//   Legacy code values for every knob (hand-typed ones included), evaluated with VIP_ENABLE off")
    w("//   as shipped. Used by the drift test (core/tests/test_shipped_conf_parity.cpp).")
    w("//")
    w("// Knobs whose legacy [default, min, max] differs between RENEWAL and pre-renewal builds:")
    for k in re_diff:
        how = "#ifdef RENEWAL in battle_data[]" if k.exprs["re"] != k.exprs["pre"] else f"header macros in [{', '.join(k.exprs['re'])}]"
        w(f"//   {k.key}: {list(k.legacy(True))} renewal, {list(k.legacy(False))} pre-renewal ({how})")
    w("// Knobs whose legacy entry differs with VIP_ENABLE (off in the shipped src/config/core.hpp):")
    for k in vip_diff:
        vip, off = k.code[(True, True)], k.code[(True, False)]
        w(f"//   {k.key}: [{', '.join(k.exprs['re+vip'])}] = {list(vip)} with VIP_ENABLE, "
          f"[{', '.join(k.exprs['re'])}] = {list(off)} without")
    for n in notes:
        w(f"// Note: {n}")
    w("#pragma once")
    w("")
    w("// NOLINTBEGIN")
    w("#define RATHENA_BATTLE_KNOBS_GENERATED(X) \\")
    groups: dict[str, list[Knob]] = {}
    for k in gen:
        groups.setdefault(conf_file_of(k) or "(not set by conf/battle: legacy code default)", []).append(k)
    order = sorted(groups, key=lambda g: (g.startswith("("), g))
    for g in order:
        w(f"\t/* {g} */ \\")
        for k in groups[g]:
            t = "bool" if k.is_bool else "std::int32_t"
            d = k.shipped(True)
            dv = ("true" if d else "false") if k.is_bool else lit(d)
            note = ""
            if symbolic(k.exprs["re"]) or k.legacy(True)[0] != d or k.legacy(False)[0] != k.shipped(False):
                note = f" /* legacy: {', '.join(k.exprs['re'])} */"
            w(f"\tX({t}, {k.name}, {dv}, {lit(k.lo)}, {lit(k.hi)}){note} \\")
    w("\t/* end */")
    w("")
    w("#define RATHENA_BATTLE_KNOB_CONF_KEYS(K) \\")
    for k in knobs:
        if k.name != k.key:
            w(f"\tK({k.name}, \"{k.key}\") \\")
    w("\t/* end */")
    w("")
    w("#define RATHENA_BATTLE_PRE_RENEWAL_DEFAULTS(P) \\")
    for k in knobs:
        if k.shipped(False) != k.shipped(True):
            v = k.shipped(False)
            w(f"\tP({k.name}, {('true' if v else 'false') if (k.typed.type == 'bool' if k.typed else k.is_bool) else lit(v)}) \\")
    w("\t/* end */")
    w("")
    w("#define RATHENA_BATTLE_KNOB_LEGACY(L) \\")
    for k in knobs:
        r, p = k.legacy(True), k.legacy(False)
        w(f"\tL({k.name}, \"{k.field}\", {lit(r[0])}, {lit(p[0])}, {lit(r[1])}, {lit(r[2])}, {lit(p[1])}, {lit(p[2])}, \"{conf_file_of(k)}\") \\")
    w("\t/* end */")
    w("// NOLINTEND")
    w("")
    return "\n".join(out)


def check_typed(knobs: list[Knob]) -> None:
    """Hand-typed knobs keep their bounds, but their default must be the shipped one."""
    errors = []
    for k in knobs:
        t = k.typed
        if t is None:
            continue
        default = {"true": 1, "false": 0}.get(t.default)
        if default is None:
            try:
                default = int(t.default)
            except ValueError:
                continue  # symbolic hand-typed default; the C++ drift test checks it
        if default != k.shipped(True):
            errors.append(f"{t.name}: hand-typed default {t.default} != shipped {k.shipped(True)}")
    if errors:
        raise GenError("hand-typed knobs in knobs.def.hpp disagree with the shipped conf:\n  " + "\n  ".join(errors))


def report(knobs: list[Knob]) -> str:
    lines = []
    for k in knobs:
        if not k.conf:
            continue
        r, p = k.legacy(True)[0], k.legacy(False)[0]
        if r != k.conf[0] or p != k.conf[0]:
            lines.append(f"{k.key}: code renewal={r} pre-renewal={p}, shipped {k.conf[2]} = {k.conf[1]} ({k.conf[0]})")
    absent = [k.key for k in knobs if not k.conf]
    lines.append(f"{len(absent)} knobs not set by conf/battle: {' '.join(absent)}")
    return "\n".join(lines)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--check", action="store_true", help="exit 1 if the generated header is out of date")
    ap.add_argument("--report", action="store_true", help="print the code-default vs shipped-conf differences")
    args = ap.parse_args()
    try:
        knobs, entries, notes = build_model()
        check_typed(knobs)
        text = render(knobs, entries, notes)
    except GenError as e:
        print(f"{GENERATOR}: error: {e}", file=sys.stderr)
        return 2
    if args.report:
        print(report(knobs))
    current = OUTPUT.read_text(encoding="utf-8") if OUTPUT.exists() else None
    if args.check:
        if current != text:
            print(f"{rel(OUTPUT)} is out of date; run python3 {GENERATOR}", file=sys.stderr)
            return 1
        print(f"{rel(OUTPUT)} is up to date ({len(knobs)} knobs)")
        return 0
    if current != text:
        OUTPUT.write_text(text, encoding="utf-8", newline="\n")
        print(f"wrote {rel(OUTPUT)} ({len(knobs)} knobs, {sum(1 for k in knobs if k.typed is None)} generated)")
    else:
        print(f"{rel(OUTPUT)} unchanged ({len(knobs)} knobs)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
