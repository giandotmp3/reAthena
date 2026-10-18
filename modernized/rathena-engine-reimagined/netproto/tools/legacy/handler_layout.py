#!/usr/bin/env python3
"""Mechanical field layouts from legacy `clif_parse_*` bodies (RFIFO reads at literal offsets).

A layout is derived ONLY when the handler is a closed, straight-line reader of the packet:

  * every packet read is `RFIFOB/W/L/Q/F(fd, OFFSET)` or `RFIFOPOS/RFIFOPOS2(fd, OFFSET, ...)` or (once, in a
    variable-length packet, past every other field) `RFIFOP/RFIFOCP(fd, OFFSET)` for a tail;
  * OFFSET is an integer literal / literal arithmetic, or `info->pos[k]` / `packet_db[..].pos[k]`, resolved
    with the pos list of the opcode being derived;
  * no read sits inside a loop, none uses another fd expression, and the handler calls no function of the
    same file that reads the packet itself (those helpers would hold the rest of the layout);
  * one width per offset, reads never overlap, all reads lie inside the packet's fixed length.

Anything else is NOT derived (`fields: unknown`): no width is guessed, no loop is unrolled. Types are the
widths the macros read (unsigned, uint8/16/32/64, float): the macros carry no signedness, so none is
claimed. Names are the variable a read is assigned to when that is a plain `name = RFIFOx(..)`, else
`f<offset>`.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from cpp_subset import strip_comments  # noqa: E402

WIDTH = {"B": ("u8", 1), "W": ("u16", 2), "L": ("u32", 4), "Q": ("u64", 8), "F": ("f32", 4)}
IGNORED_RFIFO = {"RFIFOSKIP", "RFIFOREST", "RFIFOFLUSH", "RFIFOSPACE", "RFIFOHEAD", "RFIFOPOS", "RFIFOPOS2",
                 "RFIFOB", "RFIFOW", "RFIFOL", "RFIFOQ", "RFIFOF", "RFIFOP", "RFIFOCP"}
KEYWORDS = {"if", "for", "while", "switch", "return", "sizeof", "catch", "defined"}


def _skip_literal(t, i):
    """t[i] is a quote: return index after the literal."""
    q = t[i]
    j = i + 1
    while j < len(t) and t[j] != q:
        j += 2 if t[j] == "\\" else 1
    return j + 1


def match_brace(t, i, open_="{", close="}"):
    depth = 0
    n = len(t)
    while i < n:
        c = t[i]
        if c in "\"'":
            i = _skip_literal(t, i)
            continue
        if c == open_:
            depth += 1
        elif c == close:
            depth -= 1
            if depth == 0:
                return i
        i += 1
    return -1


def split_args(t):
    out, depth, cur, i = [], 0, [], 0
    while i < len(t):
        c = t[i]
        if c in "\"'":
            j = _skip_literal(t, i)
            cur.append(t[i:j])
            i = j
            continue
        if c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
        if c == "," and depth == 0:
            out.append("".join(cur).strip())
            cur = []
        else:
            cur.append(c)
        i += 1
    out.append("".join(cur).strip())
    return out


_FUNC = re.compile(r"(?m)^(?:static\s+|inline\s+)*[A-Za-z_][\w\s\*&:<>,]*?[\s\*&](\w+)\s*\(([^;{}]*)\)\s*(?:const\s*)?\{")


class Source:
    """A comment-stripped C++ file with its top-level function bodies."""

    def __init__(self, path: Path):
        self.path = path
        self.text = strip_comments(path.read_text(encoding="utf-8", errors="replace"))
        self.funcs = {}   # name -> (body_text, line_of_definition, body_start_offset)
        for m in _FUNC.finditer(self.text):
            name = m.group(1)
            if name in KEYWORDS:
                continue
            ob = m.end() - 1
            cb = match_brace(self.text, ob)
            if cb < 0:
                continue
            line = self.text.count("\n", 0, m.start(1)) + 1
            self.funcs.setdefault(name, (self.text[ob:cb + 1], line, ob))
        self.readers = {n for n, (b, _, _) in self.funcs.items() if not re.match(r"[RW](?:FIFO|BUF)", n) and
                        re.search(r"\bRFIFO(?:[BWLQFP]|CP|POS2?)\s*\(", b)}


def _statements(body):
    """Yield (statement_text, in_loop) for every statement; statements are split at ; { } outside parens."""
    stack = []    # True for loop blocks
    cur = []
    depth = 0
    i, n = 0, len(body)
    loop_hdr = re.compile(r"^\s*(?:for|while)\b|^\s*do\b|^\s*else\s+(?:for|while)\b")
    while i < n:
        c = body[i]
        if c in "\"'":
            j = _skip_literal(body, i)
            cur.append(body[i:j])
            i = j
            continue
        if c in "([":
            depth += 1
        elif c in ")]":
            depth -= 1
        if depth == 0 and c in ";{}":
            stmt = "".join(cur)
            in_loop = any(stack)
            if stmt.strip():
                yield stmt, in_loop or bool(loop_hdr.match(stmt))
            if c == "{":
                stack.append(bool(loop_hdr.match(stmt)))
            elif c == "}":
                if stack:
                    stack.pop()
            cur = []
        else:
            cur.append(c)
        i += 1
    stmt = "".join(cur)
    if stmt.strip():
        yield stmt, any(stack)


_POS = re.compile(r"(?:info\s*->\s*|packet_db\s*\[[^\]]*\]\s*\.\s*)pos\s*\[\s*(\d+)\s*\]")


def eval_offset(expr, pos):
    """-> int, or None when the expression is not literal / pos[k] arithmetic."""
    ok = [True]

    def sub(m):
        k = int(m.group(1))
        if k >= len(pos):
            ok[0] = False
            return "0"
        return str(pos[k])

    e = _POS.sub(sub, expr)
    if not ok[0] or not re.fullmatch(r"[0-9a-fA-FxX+\-*() ]+", e) or not re.search(r"\d", e):
        return None
    try:
        v = eval(e, {"__builtins__": {}}, {})  # only digits/hex/+-*() reach here
    except Exception:
        return None
    return v if isinstance(v, int) and v >= 0 else None


_READ = re.compile(r"\bRFIFO(B|W|L|Q|F|POS2|POS|CP|P)\s*\(")
_ASSIGN = re.compile(r"(?:^|[\s;{(,])(?:[A-Za-z_][\w:<>\*&]*\s+)*\*?\s*(?:[A-Za-z_]\w*\s*(?:->|\.)\s*)*([A-Za-z_]\w*)\s*=\s*(?:\([\w\s\*:<>]*\)\s*|[A-Za-z_]\w*\s*\(\s*)?$")


_READ_TOKEN = re.compile(r"\bRFIFO(?:[BWLQF]|POS2?|CP|P)\s*\(")
_DUMP = re.compile(r"RFIFOP\s*\(\s*fd\s*,\s*0\s*\)")


def _strip_conditionals(body):
    """Remove preprocessor lines. A packet read under #if/#ifdef/#ifndef is not mechanical: the live
    branch depends on the build, which this reader does not evaluate."""
    depth, out = 0, []
    for line in body.split("\n"):
        t = line.strip()
        if t.startswith("#"):
            d = re.match(r"#\s*(\w+)", t)
            kw = d.group(1) if d else ""
            if kw in ("if", "ifdef", "ifndef"):
                depth += 1
            elif kw == "endif":
                depth -= 1
            out.append("")
            continue
        if depth > 0 and _READ_TOKEN.search(_DUMP.sub("", line)):
            return body, "packet read under a preprocessor conditional"
        out.append(line)
    return "\n".join(out), None


def _collect(src: Source, handler: str, pos, prefix_offset, depth, stack):
    """-> (reads, tail_offsets, error). reads = [(offset, type, hint)]; includes the reads of called
    helper functions that take `fd` and are themselves mechanical (depth <= 2)."""
    if handler not in src.funcs:
        return None, None, "handler body not found"
    if handler in stack or depth > 2:
        return None, None, "recursive / deeply nested helpers"
    body, cond_err = _strip_conditionals(src.funcs[handler][0])
    if cond_err:
        return None, None, cond_err
    for t in re.findall(r"\bRFIFO[A-Z0-9]*", body):
        if t not in IGNORED_RFIFO:
            return None, None, f"unrecognised {t}"
    called = {m.group(1) for m in re.finditer(r"\b([A-Za-z_]\w*)\s*\(", body)} - KEYWORDS
    reads, tails = [], set()
    for h in sorted((called & src.readers) - {handler}):
        r, t, err = _collect(src, h, pos, prefix_offset, depth + 1, stack | {handler})
        if err:
            return None, None, f"calls packet-reading helper {h}: {err}"
        reads += r
        tails |= t
    for stmt, in_loop in _statements(body):
        for m in _READ.finditer(stmt):
            kind = m.group(1)
            open_i = m.end() - 1
            close_i = match_brace(stmt, open_i, "(", ")")
            if close_i < 0:
                return None, None, "unbalanced read"
            args = split_args(stmt[open_i + 1:close_i])
            if args[0] != "fd":
                return None, None, "read through another fd expression"
            if in_loop:
                return None, None, "read inside a loop"
            if len(args) < 2:
                return None, None, "malformed read"
            off = eval_offset(args[1], pos)
            if off is None:
                return None, None, f"non-literal offset '{args[1][:30]}'"
            if kind in ("P", "CP"):
                if off != 0:   # RFIFOP(fd, 0) is the start of the frame (ShowDump / logging), not a field
                    tails.add(off)
                continue
            if kind in ("POS", "POS2"):
                reads.append((off, "pos3" if kind == "POS" else "pos6", None))
                continue
            hint = None
            before = stmt[:m.start()].rstrip()
            am = _ASSIGN.search(before)
            if am and (before.endswith("=") or before.endswith(")")):
                hint = am.group(1)
            reads.append((off, WIDTH[kind][0], hint))
    return reads, tails, None


def derive(src: Source, handler: str, pos, db_len, prefix_offset=2):
    """Returns (layout | None, why). layout = {'fields': [...], 'tail': {...}|None, 'line': int}."""
    reads, tail_offsets, err = _collect(src, handler, pos, prefix_offset, 0, frozenset())
    if err:
        return None, err
    line = src.funcs[handler][1]
    fields, seen = [], {}
    for off, ty, hint in reads:
        size = {"pos3": 3, "pos6": 6}.get(ty) or WIDTH[{"u8": "B", "u16": "W", "u32": "L", "u64": "Q", "f32": "F"}[ty]][1]
        if off == 0 and ty == "u16":
            continue
        if db_len == -1 and off == prefix_offset and ty == "u16":
            continue
        key = (off, ty)
        if key in seen:
            if hint and not seen[key]["hint"]:
                seen[key]["hint"] = hint
            continue
        f = {"offset": off, "type": ty, "size": size, "hint": hint}
        seen[key] = f
        fields.append(f)
    fields.sort(key=lambda f: f["offset"])
    for a, b in zip(fields, fields[1:]):
        if a["offset"] + a["size"] > b["offset"]:
            return None, f"reads overlap at {b['offset']}"
    tail = None
    if tail_offsets:
        if len(tail_offsets) != 1 or db_len != -1:
            return None, "pointer read (string/array) of unknown width"
        t = next(iter(tail_offsets))
        end = max([f["offset"] + f["size"] for f in fields] + [prefix_offset + 2])
        if t < end:
            return None, "pointer read overlaps other reads"
        tail = {"offset": t}
    for f in fields:
        if db_len != -1 and f["offset"] + f["size"] > db_len:
            return None, "read beyond the packet's fixed length"
        if f["offset"] < 2:
            return None, "read inside the opcode"
    used = set()
    for f in fields:
        if f["type"] in ("pos3", "pos6"):
            nm = "pos" if "pos" not in used else None
        else:
            nm = f["hint"] if f["hint"] and f["hint"] not in used else None
        f["name"] = nm or f"f{f['offset']}"
        used.add(f["name"])
        del f["hint"]
    return {"fields": fields, "tail": tail, "line": line, "reads": len(reads)}, "ok"
