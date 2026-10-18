"""A deterministic subset of the C preprocessor, enough to evaluate rAthena's
packet headers for one (PACKETVER, flavour) target without a compiler.

Supported: #if / #elif / #else / #endif / #ifdef / #ifndef, defined(X) and
defined X, object-like and function-like #define (function-like bodies are
stored but never expanded, see `Preprocessor.expand`), #undef, #include
(resolved inside the repository only), #pragma/#warning (ignored), #error
(raises when live in a strict file).

Expressions: integer literals (dec/hex, U/L suffixes), identifiers (macro
expanded, an unknown identifier is 0 as in C), ! ~ - + * / % << >> < > <= >=
== != & ^ | && || ?: and parentheses. Anything else is a CppError; a silent
wrong answer is worse than a refusal.

No network, no clock, no environment: the same inputs give the same output.
"""
from __future__ import annotations

import os
import re
from pathlib import Path


class CppError(Exception):
    pass


_NUM = re.compile(r"0[xX][0-9a-fA-F]+|\d+")
_TOK = re.compile(
    r"\s*(?:(?P<num>(?:0[xX][0-9a-fA-F]+|\d+)[uUlL]*)|(?P<id>[A-Za-z_]\w*)"
    r"|(?P<op>\|\||&&|==|!=|<=|>=|<<|>>|[-+*/%<>!~()?:&|^,]))"
)
_DIRECTIVE = re.compile(r"^\s*#\s*(\w+)\s*(.*)$")


def strip_comments(text: str) -> str:
    """Remove // and /* */ comments, keeping newlines so line numbers survive."""
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == '"' or c == "'":
            j = i + 1
            while j < n and text[j] != c and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            out.append(text[i:j + 1])
            i = j + 1
        elif text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j < 0 else j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append(" " + "\n" * text.count("\n", i, j))
            i = j
        else:
            out.append(c)
            i += 1
    return "".join(out)


def logical_lines(text: str):
    """Yield (first_physical_line_number, text) joining backslash continuations."""
    lines = strip_comments(text).split("\n")
    i = 0
    while i < len(lines):
        start = i + 1
        cur = lines[i]
        while cur.endswith("\\") and i + 1 < len(lines):
            i += 1
            cur = cur[:-1] + " " + lines[i]
        yield start, cur
        i += 1


def tokenize(expr: str):
    pos, toks = 0, []
    expr = expr.rstrip()
    while pos < len(expr):
        m = _TOK.match(expr, pos)
        if not m or m.end() == pos:
            raise CppError(f"cannot tokenize {expr[pos:]!r} in {expr!r}")
        pos = m.end()
        if m.group("num") is not None:
            toks.append(("num", int(m.group("num").rstrip("uUlL"), 0)))
        elif m.group("id") is not None:
            toks.append(("id", m.group("id")))
        else:
            toks.append(("op", m.group("op")))
    return toks


class _Eval:
    """Precedence-climbing evaluator over a macro-expanded token list."""

    BIN = [("||",), ("&&",), ("|",), ("^",), ("&",), ("==", "!="), ("<", ">", "<=", ">="),
           ("<<", ">>"), ("+", "-"), ("*", "/", "%")]

    def __init__(self, toks, resolve):
        self.t = toks
        self.i = 0
        self.resolve = resolve  # name -> int; may raise CppError

    def peek(self):
        return self.t[self.i] if self.i < len(self.t) else (None, None)

    def take(self):
        tok = self.peek()
        self.i += 1
        return tok

    def expect(self, op):
        k, v = self.take()
        if (k, v) != ("op", op):
            raise CppError(f"expected {op!r}, got {v!r}")

    def parse(self):
        v = self.ternary()
        if self.i != len(self.t):
            raise CppError(f"trailing tokens {self.t[self.i:]!r}")
        return v

    def ternary(self):
        c = self.binary(0)
        if self.peek() == ("op", "?"):
            self.take()
            a = self.ternary()
            self.expect(":")
            b = self.ternary()
            return a if c else b
        return c

    def binary(self, level):
        if level == len(self.BIN):
            return self.unary()
        v = self.binary(level + 1)
        while self.peek()[0] == "op" and self.peek()[1] in self.BIN[level]:
            op = self.take()[1]
            r = self.binary(level + 1)
            v = self.apply(op, v, r)
        return v

    @staticmethod
    def apply(op, a, b):
        if op == "||": return int(bool(a) or bool(b))
        if op == "&&": return int(bool(a) and bool(b))
        if op == "|": return a | b
        if op == "^": return a ^ b
        if op == "&": return a & b
        if op == "==": return int(a == b)
        if op == "!=": return int(a != b)
        if op == "<": return int(a < b)
        if op == ">": return int(a > b)
        if op == "<=": return int(a <= b)
        if op == ">=": return int(a >= b)
        if op == "<<": return a << b
        if op == ">>": return a >> b
        if op == "+": return a + b
        if op == "-": return a - b
        if op in ("*", "/", "%"):
            if op == "*": return a * b
            if b == 0: raise CppError("division by zero in #if")
            q = abs(a) // abs(b) * (1 if (a < 0) == (b < 0) else -1)  # C truncation
            return q if op == "/" else a - q * b
        raise CppError(op)

    def unary(self):
        k, v = self.take()
        if k == "num": return v
        if k == "id":
            return self.resolve(v)
        if (k, v) == ("op", "("):
            r = self.ternary()
            self.expect(")")
            return r
        if (k, v) == ("op", "!"): return int(not self.unary())
        if (k, v) == ("op", "~"): return ~self.unary()
        if (k, v) == ("op", "-"): return -self.unary()
        if (k, v) == ("op", "+"): return self.unary()
        raise CppError(f"unexpected token {v!r}")


class Preprocessor:
    def __init__(self, include_dirs, strict_paths=()):
        self.include_dirs = [Path(d) for d in include_dirs]
        self.macros: dict[str, tuple[list[str] | None, str]] = {}
        self.strict = {str(Path(p).resolve()) for p in strict_paths}
        self.unknown_ids: set[str] = set()   # evaluated as 0 inside strict files
        self.skipped_exprs: list[str] = []   # #if expressions we could not evaluate (non-strict)
        self._seen: set[str] = set()
        self._depth = 0

    # -- macro table --------------------------------------------------------
    def define(self, name, body="1"):
        self.macros[name] = (None, body)

    def undef(self, name):
        self.macros.pop(name, None)

    def value(self, name):
        """Integer value of an object-like macro, or None."""
        if name not in self.macros or self.macros[name][0] is not None:
            return None
        try:
            return self.eval_expr(self.macros[name][1])
        except CppError:
            return None

    def expand(self, toks, hide=frozenset()):
        out = []
        for k, v in toks:
            if k == "id" and v in self.macros and v not in hide:
                params, body = self.macros[v]
                if params is None:
                    out.extend(self.expand(tokenize(body), hide | {v}) if body.strip() else [])
                    continue
            out.append((k, v))
        return out

    def eval_expr(self, expr, strict=False):
        toks = tokenize(expr)
        # defined X / defined(X) first, before macro expansion
        j, pre = 0, []
        while j < len(toks):
            if toks[j] == ("id", "defined"):
                if j + 1 < len(toks) and toks[j + 1] == ("op", "("):
                    name = toks[j + 2][1]
                    j += 4
                else:
                    name = toks[j + 1][1]
                    j += 2
                pre.append(("num", int(name in self.macros)))
            else:
                pre.append(toks[j])
                j += 1

        def unknown(name):  # C semantics: an unknown identifier in #if is 0
            if strict:
                self.unknown_ids.add(name)
            return 0

        return _Eval(self.expand(pre), unknown).parse()

    def eval_tokens(self, toks, resolve):
        """Evaluate already macro-expanded ("num"|"id"|"op", value) tokens."""
        return _Eval(toks, resolve).parse()

    # -- file processing ----------------------------------------------------
    def resolve_include(self, name, cur_dir):
        cands = [Path(cur_dir) / name] + [d / name for d in self.include_dirs]
        for c in cands:
            if c.is_file():
                return c.resolve()
        return None

    def process(self, path, on_line=None, emit_includes=(), follow_includes=True):
        """Run `path`. `on_line(path, lineno, text)` is called for every live,
        non-directive, non-blank line of `path` and of the includes listed in
        `emit_includes`. Other in-repo includes are processed for their macros."""
        path = Path(path).resolve()
        key = str(path)
        strict = key in self.strict
        emit = on_line is not None
        emit_set = {str(Path(p).resolve()) for p in emit_includes}
        text = path.read_text(encoding="utf-8", errors="replace")
        stack = []  # (parent_live, taken, live)
        live = True
        for lineno, line in logical_lines(text):
            m = _DIRECTIVE.match(line)
            if not m:
                if live and emit and line.strip():
                    on_line(path, lineno, line.strip())
                continue
            d, rest = m.group(1), m.group(2).strip()
            if d in ("if", "ifdef", "ifndef"):
                if live:
                    cond = self._cond(d, rest, strict, path, lineno)
                else:
                    cond = False
                stack.append((live, cond, live and cond))
                live = live and cond
            elif d == "elif":
                if not stack:
                    raise CppError(f"{path}:{lineno}: #elif without #if")
                parent, taken, _ = stack.pop()
                if parent and not taken:
                    cond = self._cond("if", rest, strict, path, lineno)
                else:
                    cond = False
                stack.append((parent, taken or cond, parent and not taken and cond))
                live = stack[-1][2]
            elif d == "else":
                parent, taken, _ = stack.pop()
                stack.append((parent, True, parent and not taken))
                live = stack[-1][2]
            elif d == "endif":
                if not stack:
                    raise CppError(f"{path}:{lineno}: #endif without #if")
                stack.pop()
                live = stack[-1][2] if stack else True
            elif not live:
                continue
            elif d == "define":
                self._define(rest)
            elif d == "undef":
                self.undef(rest.split()[0])
            elif d == "error":
                if strict:
                    raise CppError(f"{path}:{lineno}: #error {rest}")
            elif d == "include" and follow_includes:
                im = re.match(r'[<"]([^>"]+)[>"]', rest)
                inc = self.resolve_include(im.group(1), path.parent) if im else None
                if inc is not None and (str(inc) in emit_set or str(inc) not in self._seen):
                    if str(inc) not in emit_set:
                        self._seen.add(str(inc))
                    self._depth += 1
                    if self._depth > 40:
                        raise CppError("include depth > 40")
                    self.process(inc, on_line if str(inc) in emit_set else None, emit_includes)
                    self._depth -= 1
        if stack:
            raise CppError(f"{path}: unterminated #if")

    def _define(self, rest):
        m = re.match(r"(\w+)(\(([^)]*)\))?\s*(.*)$", rest)
        if not m:
            raise CppError(f"bad #define {rest!r}")
        name, _, params, body = m.groups()
        self.macros[name] = ([p.strip() for p in params.split(",")] if params is not None else None, body.strip())

    def _cond(self, d, rest, strict, path, lineno):
        if d == "ifdef":
            return rest.split()[0] in self.macros
        if d == "ifndef":
            return rest.split()[0] not in self.macros
        try:
            return bool(self.eval_expr(rest, strict))
        except CppError as e:
            if strict:
                raise CppError(f"{path}:{lineno}: {e}") from None
            self.skipped_exprs.append(rest)
            return False
