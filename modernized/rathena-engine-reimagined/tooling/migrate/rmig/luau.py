"""Deterministic Luau serializer: Python values -> Luau table literals (no timestamps, fixed key order)."""
import re

_IDENT = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
_KEYWORDS = set("and break do else elseif end false for function if in local nil not or repeat return then true until while continue".split())
MAX_SAFE = 1 << 53


class Unsafe(Exception):
    pass


def q(s):
    out = ['"']
    for ch in s:
        o = ord(ch)
        if ch == '"':
            out.append('\\"')
        elif ch == "\\":
            out.append("\\\\")
        elif ch == "\n":
            out.append("\\n")
        elif ch == "\r":
            out.append("\\r")
        elif ch == "\t":
            out.append("\\t")
        elif o < 0x20 or o == 0x7F:
            out.append("\\%03d" % o)
        elif 0xD800 <= o <= 0xDFFF:      # lone surrogate from surrogateescape: keep it printable
            out.append("\\u{%X}" % o)
        else:
            out.append(ch)
    out.append('"')
    return "".join(out)


def key(k):
    if isinstance(k, int):
        return "[%d]" % k
    if _IDENT.match(k) and k not in _KEYWORDS:
        return k
    return "[" + q(k) + "]"


def val(v, depth=0, notes=None):
    if v is None:
        return "nil"
    if v is True:
        return "true"
    if v is False:
        return "false"
    if isinstance(v, int):
        if abs(v) >= MAX_SAFE:
            if notes is not None:
                notes.append("integer %d exceeds 2^53 and is emitted as a decimal string" % v)
            return q(str(v))
        return str(v)
    if isinstance(v, float):
        return repr(v)
    if isinstance(v, str):
        return q(v)
    if isinstance(v, (list, tuple)):
        if not v:
            return "{}"
        if all(isinstance(x, (int, bool)) and not isinstance(x, bool) for x in v):
            return "{" + ",".join(val(x, depth + 1, notes) for x in v) + "}"
        if all(isinstance(x, str) for x in v) and sum(len(x) for x in v) < 100:
            return "{" + ",".join(q(x) for x in v) + "}"
        pad = "\t" * (depth + 1)
        return "{\n" + "".join(pad + val(x, depth + 1, notes) + ",\n" for x in v) + "\t" * depth + "}"
    if isinstance(v, dict):
        if not v:
            return "{}"
        pad = "\t" * (depth + 1)
        return "{\n" + "".join(pad + key(k) + " = " + val(x, depth + 1, notes) + ",\n" for k, x in v.items()) + "\t" * depth + "}"
    raise TypeError("cannot serialize %r" % (v,))


def module(header_lines, entries, notes=None):
    """entries: iterable of (string id, dict). Emits `return { [id] = {...}, ... }` with one entry per block."""
    out = []
    for h in header_lines:
        out.append("-- " + h + "\n")
    out.append("return {\n")
    for k, v in entries:
        out.append("\t[" + q(k) + "] = " + val(v, 1, notes) + ",\n")
    out.append("}\n")
    return "".join(out)
