#!/usr/bin/env python3
"""Evaluate rAthena's packet tables for ONE client target without a compiler.

Inputs (read-only, in the legacy tree):
  src/config/packets.hpp        PACKETVER default, flavour macros (*_NUM)
  src/map/clif_packetdb.hpp     packet(id,len) / parseable_packet(id,len,fn,off...)
  src/map/clif_shuffle.hpp      per-build overrides, included after the db
  src/map/packets_struct.hpp    typed packet structs and HEADER_* ids
  src/map/packets.hpp           more typed structs (includes packets_struct.hpp)
  src/map/clif.cpp              `/// 0072 ... (CZ_ENTER)` doc comments (names only)

The legacy table is built exactly as clif.cpp does it (packetdb_readdb):
clif_packetdb.hpp, then clif_shuffle.hpp; a later live definition of an opcode
overwrites the earlier one; opcodes above MAX_PACKET_DB (0xCFF) are dropped.

Output: a deterministic YAML document (no timestamps, sorted by opcode).
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from cpp_subset import CppError, Preprocessor, strip_comments  # noqa: E402

SCHEMA = 1
MAX_PACKET_DB = 0xCFF  # src/map/clif.hpp e_PacketDBVersion
MIN_PACKET_DB = 0x064
MAX_PACKET_POS = 20

INPUT_FILES = [
    "config/packets.hpp",
    "map/clif_packetdb.hpp",
    "map/clif_shuffle.hpp",
    "map/packets_struct.hpp",
    "map/packets.hpp",
    "map/clif.cpp",
]

BASE_TYPES = {  # C type -> (size, spec type)
    "int8": (1, "i8"), "uint8": (1, "u8"), "int16": (2, "i16"), "uint16": (2, "u16"),
    "int32": (4, "i32"), "uint32": (4, "u32"), "int64": (8, "i64"), "uint64": (8, "u64"),
    "char": (1, "i8"), "bool": (1, "u8"), "float": (4, "f32"), "int": (4, "i32"),
}

_CTOK = re.compile(
    r"\s*(?:(?P<num>0[xX][0-9a-fA-F]+[uUlL]*|\d+[uUlL]*)|(?P<id>[A-Za-z_]\w*)|(?P<str>\"[^\"]*\")"
    r"|(?P<op>\|\||&&|==|!=|<=|>=|<<|>>|::|[-+*/%<>!~()?:&|^,;{}\[\]=.#]))")


class ExtractError(Exception):
    pass


def ctokens(text, lineno=0):
    pos, out = 0, []
    text = text.rstrip()
    while pos < len(text):
        m = _CTOK.match(text, pos)
        if not m or m.end() == pos:
            raise ExtractError(f"line {lineno}: cannot tokenize {text[pos:pos + 30]!r}")
        pos = m.end()
        if m.group("num") is not None:
            out.append(("num", int(m.group("num").rstrip("uUlL"), 0), lineno))
        elif m.group("id") is not None:
            out.append(("id", m.group("id"), lineno))
        elif m.group("str") is not None:
            out.append(("str", m.group("str"), lineno))
        else:
            out.append(("op", m.group("op"), lineno))
    return out


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


# ---------------------------------------------------------------------------
# environment
# ---------------------------------------------------------------------------

def build_environment(src: Path, packetver: int, flavour: str):
    """Run config/packets.hpp for PACKETVER and apply the flavour rules.
    Returns (pp, derived_flavour, hypothetical)."""
    pp = Preprocessor([src], strict_paths=[src / "config/packets.hpp"])
    pp.define("PACKETVER", str(packetver))
    pp.process(src / "config/packets.hpp")
    if "PACKETVER_ZERO" in pp.macros:
        derived = "zero"
    elif "PACKETVER_RE_NUM" in pp.macros:
        derived = "re"
    else:
        derived = "main"
    hypothetical = False
    if flavour not in ("auto", derived):
        # Not reachable through config/packets.hpp for this PACKETVER; evaluate the
        # flavour macros the way a build of that flavour would see them.
        hypothetical = True
        for n in ("PACKETVER_RE", "PACKETVER_RE_NUM", "PACKETVER_MAIN_NUM", "PACKETVER_ZERO", "PACKETVER_ZERO_NUM"):
            pp.undef(n)
        if flavour == "main":
            pp.define("PACKETVER_MAIN_NUM", str(packetver))
        elif flavour == "re":
            pp.define("PACKETVER_RE", str(packetver))
            pp.define("PACKETVER_RE_NUM", str(packetver))
        elif flavour == "zero":
            pp.define("PACKETVER_ZERO", str(packetver))
            pp.define("PACKETVER_ZERO_NUM", str(packetver))
        else:
            raise ExtractError(f"unknown flavour {flavour}")
        derived = flavour
    return pp, derived, hypothetical


# ---------------------------------------------------------------------------
# structs and header constants
# ---------------------------------------------------------------------------

class Member:
    def __init__(self, name, offset, width, line):
        self.name, self.offset, self.width, self.line = name, offset, width, line
        self.type = None      # spec scalar type, for scalars / arrays of scalars
        self.count = None     # array length, None for a scalar
        self.struct = None    # struct name for struct members
        self.bits = None      # [(name, width)] for bitfield groups
        self.flex = False     # flexible array member (T name[])
        self.elem = None      # element size
        self.ctype = None     # legacy C type name for scalars


class Struct:
    def __init__(self, name, line):
        self.name, self.line = name, line
        self.members: list[Member] = []
        self.size: int | None = 0
        self.flex: Member | None = None
        self.problems: list[str] = []


class Model:
    def __init__(self, pp):
        self.pp = pp
        self.consts: dict[str, int] = {}
        self.structs: dict[str, Struct] = {}
        self.headers: dict[str, int] = {}          # NAME -> id
        self.header_struct: dict[str, str] = {}    # NAME -> struct name
        self.by_id: dict[int, list[str]] = {}      # id -> [NAME]
        self.enum_problems: list[str] = []

    # macro expansion on C tokens (object-like macros only)
    def expand(self, toks, hide=frozenset()):
        out = []
        for t in toks:
            k, v, ln = t
            if k == "id" and v in self.pp.macros and v not in hide:
                params, body = self.pp.macros[v]
                if params is None:
                    try:
                        sub = ctokens(body, ln)
                    except ExtractError:
                        out.append(t)
                        continue
                    out.extend(self.expand(sub, hide | {v}))
                    continue
            out.append(t)
        return out

    def eval(self, toks):
        """Evaluate constant tokens ("num"/"id"/"op", value, line)."""
        def resolve(name):
            if name in self.consts:
                return self.consts[name]
            raise CppError(f"unresolved identifier {name}")
        return self.pp.eval_tokens([(k, v) for k, v, _ in toks], resolve)

    def sizeof(self, name):
        name = self.alias(name)
        if name in BASE_TYPES:
            return BASE_TYPES[name][0]
        s = self.structs.get(name)
        if s is None or s.size is None:
            raise CppError(f"sizeof({name}) unresolved")
        return s.size

    def alias(self, name):
        seen = set()
        while name in self.pp.macros and self.pp.macros[name][0] is None and name not in seen:
            seen.add(name)
            body = self.pp.macros[name][1].strip()
            if not re.fullmatch(r"[A-Za-z_]\w*", body):
                break
            name = body
        return name

    # -- top-level scan ------------------------------------------------------
    def scan(self, toks):
        i, n = 0, len(toks)

        def match(j, open_, close):
            depth = 0
            while j < n:
                if toks[j][:2] == ("op", open_):
                    depth += 1
                elif toks[j][:2] == ("op", close):
                    depth -= 1
                    if depth == 0:
                        return j
                j += 1
            raise ExtractError("unbalanced " + open_)

        last_struct = None
        while i < n:
            k, v, ln = toks[i]
            if k == "id" and v == "enum":
                j = i + 1
                while j < n and toks[j][:2] not in (("op", "{"), ("op", ";")):
                    j += 1
                if j < n and toks[j][:2] == ("op", "{"):
                    end = match(j, "{", "}")
                    self.parse_enum(toks[j + 1:end])
                    i = end + 1
                else:
                    i = j + 1
            elif k == "id" and v == "struct" and i + 2 < n and toks[i + 1][0] == "id" and toks[i + 2][:2] == ("op", "{"):
                end = match(i + 2, "{", "}")
                st = self.parse_struct(toks[i + 1][1], toks[i + 3:end], toks[i + 1][2])
                self.structs[st.name] = st
                last_struct = st.name
                i = end + 1
            elif k == "id" and v in ("DEFINE_PACKET_HEADER", "DEFINE_PACKET_ID") and i + 1 < n and toks[i + 1][:2] == ("op", "("):
                end = match(i + 1, "(", ")")
                args = toks[i + 2:end]
                if not args or args[0][0] != "id" or args[1][:2] != ("op", ","):
                    raise ExtractError(f"line {ln}: bad {v}")
                name = args[0][1]
                try:
                    value = self.eval(self.expand(args[2:]))
                except CppError as e:
                    raise ExtractError(f"line {ln}: {v}({name}): {e}") from None
                self.headers[name] = value
                self.consts["HEADER_" + name] = value
                self.by_id.setdefault(value, [])
                if name not in self.by_id[value]:
                    self.by_id[value].append(name)
                # the struct a header belongs to is PACKET_<name> (checked by name, not by position)
                self.header_struct[name] = "PACKET_" + name
                i = end + 1
            elif k == "id" and v == "const" and i + 4 < n and toks[i + 2][1].startswith("HEADER_") and toks[i + 3][:2] == ("op", "="):
                j = i + 4
                while toks[j][:2] != ("op", ";"):
                    j += 1
                name = toks[i + 2][1]
                self.consts[name] = self.eval(self.expand(toks[i + 4:j]))
                i = j + 1
            elif k == "id" and v == "static_assert":
                end = match(i + 1, "(", ")")
                i = end + 1
            elif (k, v) == ("op", "{"):
                i = match(i, "{", "}") + 1
            else:
                i += 1

    def parse_enum(self, toks):
        entries, cur, depth = [], [], 0
        for t in toks:
            if t[:2] == ("op", "(") : depth += 1
            if t[:2] == ("op", ")") : depth -= 1
            if t[:2] == ("op", ",") and depth == 0:
                entries.append(cur); cur = []
            else:
                cur.append(t)
        if cur:
            entries.append(cur)
        nxt = 0
        for e in entries:
            if not e or e[0][0] != "id":
                continue
            name = e[0][1]
            try:
                if len(e) > 1:
                    if e[1][:2] != ("op", "="):
                        raise CppError("unsupported enumerator")
                    nxt = self.eval(self.expand(e[2:]))
                self.consts[name] = nxt
                nxt += 1
            except CppError as err:
                self.enum_problems.append(f"{name}: {err}")
                nxt = 0

    def parse_struct(self, name, toks, line):
        st = Struct(name, line)
        stmts, cur, depth = [], [], 0
        for t in toks:
            if t[1] in ("{", "[", "(") and t[0] == "op": depth += 1
            if t[1] in ("}", "]", ")") and t[0] == "op": depth -= 1
            if t[:2] == ("op", ";") and depth == 0:
                stmts.append(cur); cur = []
            else:
                cur.append(t)
        if any(t for t in cur if t[:2] != ("op", ";")):
            # trailing tokens such as __attribute__((packed)) are outside the body; ignore
            pass
        offset = 0
        for stmt in stmts:
            stmt = self.expand(stmt)
            # UNAVAILABLE_STRUCT-style macros may expand to several tokens, still one statement
            try:
                m = self.parse_member(stmt, offset, st)
            except CppError as e:
                st.problems.append(f"line {stmt[0][2] if stmt else line}: {e}")
                st.size = None
                continue
            if m is None:
                continue
            if st.flex is not None:
                st.problems.append("member after flexible array")
            st.members.append(m)
            if m.flex:
                st.flex = m
            offset += m.width
        if st.problems:
            st.size = None
        else:
            st.size = offset
        return st

    def parse_member(self, stmt, offset, st):
        if not stmt:
            return None
        i = 0
        while stmt[i][1] in ("const", "volatile"):
            i += 1
        line = stmt[0][2]
        if stmt[i][:2] == ("id", "union"):
            raise CppError("union member")
        anon = None
        if stmt[i][:2] == ("id", "struct") and i + 1 < len(stmt) and stmt[i + 1][:2] == ("op", "{"):
            depth, j = 0, i + 1
            while True:
                if stmt[j][:2] == ("op", "{"): depth += 1
                if stmt[j][:2] == ("op", "}"):
                    depth -= 1
                    if depth == 0: break
                j += 1
            anon = self.parse_bitgroup(stmt[i + 2:j])
            i = j + 1
            type_name, is_struct = None, False
        else:
            is_struct = stmt[i][:2] == ("id", "struct")
            if is_struct:
                i += 1
            if stmt[i][0] != "id":
                raise CppError(f"cannot parse member starting at {stmt[i][1]!r}")
            type_name = self.alias(stmt[i][1])
            i += 1
        if i >= len(stmt) or stmt[i][0] != "id":
            raise CppError("member without a name")
        name = stmt[i][1]
        i += 1
        dims = []
        while i < len(stmt) and stmt[i][:2] == ("op", "["):
            j = i + 1
            while stmt[j][:2] != ("op", "]"):
                j += 1
            dims.append(stmt[i + 1:j])
            i = j + 1
        if i < len(stmt):
            raise CppError(f"unsupported member syntax near {stmt[i][1]!r} ({name})")
        counts = []
        flex = False
        for d in dims:
            if not d:
                flex = True
                counts.append(0)
            else:
                counts.append(self.eval(self.expand(d)))
        if len(counts) > 1:
            raise CppError(f"multi-dimensional array {name}")
        count = counts[0] if counts else None
        if anon is not None:
            bits, size = anon
            m = Member(name, offset, size * (count or 1), line)
            m.bits, m.count, m.elem = bits, count, size
            return m
        if is_struct or type_name in self.structs:
            elem = self.sizeof(type_name)
            m = Member(name, offset, elem * (count if count is not None else 1), line)
            m.struct = type_name
        else:
            if type_name not in BASE_TYPES:
                raise CppError(f"unknown type {type_name}")
            elem, m_type = BASE_TYPES[type_name]
            m = Member(name, offset, elem * (count if count is not None else 1), line)
            m.type = m_type
            m.ctype = type_name
        m.count, m.flex, m.elem = count, flex, elem
        return m

    def parse_bitgroup(self, toks):
        bits, total, cur = [], 0, []
        stmts = []
        for t in toks:
            if t[:2] == ("op", ";"):
                stmts.append(cur); cur = []
            else:
                cur.append(t)
        for s in stmts:
            s = self.expand(s)
            if not s:
                continue
            if len(s) != 4 or s[0][1] not in ("uint8",) or s[2][:2] != ("op", ":") or s[3][0] != "num":
                raise CppError("unsupported anonymous struct (only uint8 bit groups)")
            bits.append((s[1][1], s[3][1]))
            total += s[3][1]
        return bits, (total + 7) // 8


# ---------------------------------------------------------------------------
# the table
# ---------------------------------------------------------------------------

_ENTRY = re.compile(r"^(parseable_packet|packet)\s*\((.*)\)\s*;$")


def split_args(text):
    out, depth, cur = [], 0, []
    for c in text:
        if c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
        if c == "," and depth == 0:
            out.append("".join(cur).strip()); cur = []
        else:
            cur.append(c)
    out.append("".join(cur).strip())
    return out


def eval_arg(model: Model, text: str):
    """cmd / length / offset argument -> int. Handles sizeof(struct X) and sizeof(X)."""
    def sz(m):
        return str(model.sizeof(m.group(1)))
    text = re.sub(r"sizeof\s*\(\s*(?:struct\s+)?(\w+)\s*\)", sz, text)
    toks = ctokens(text)
    return model.eval(model.expand(toks))


def doc_names(clif_cpp: Path):
    """handler -> {"by_opcode": {opcode: NAME}, "names": [NAME...]} from `///` comments."""
    res, block = {}, []
    for raw in clif_cpp.read_text(encoding="utf-8", errors="replace").split("\n"):
        s = raw.strip()
        if s.startswith("///"):
            block.append(s[3:].strip())
            continue
        m = re.match(r"^(?:static\s+)?\w[\w\s\*&:<>]*?\s\*?&?(clif_parse_\w+)\s*\(", s)
        if m and block:
            ent = {"by_opcode": {}, "names": []}
            for b in block:
                mo = re.match(r"^([0-9a-fA-F]{4})\b.*\(([A-Z][A-Z0-9_]+)\)\s*$", b)
                if mo:
                    ent["by_opcode"][int(mo.group(1), 16)] = mo.group(2)
                    continue
                mn = re.search(r"\(([A-Z][A-Z0-9_]{3,})\)\.?\s*$", b)
                if mn:
                    ent["names"].append(mn.group(1))
            res[m.group(1)] = ent
        block = []
    return res


def build_spec(legacy_root: Path, packetver: int, flavour: str = "auto"):
    """Evaluate the legacy tables. Returns the spec as a plain dict (see render())."""
    src = Path(legacy_root) / "src"
    for f in INPUT_FILES:
        if not (src / f).is_file():
            raise ExtractError(f"missing legacy input {src / f}")
    pp, derived, hypothetical = build_environment(src, packetver, flavour)
    structs_h, packets_h = src / "map/packets_struct.hpp", src / "map/packets.hpp"
    db_h, shuffle_h = src / "map/clif_packetdb.hpp", src / "map/clif_shuffle.hpp"
    pp.strict |= {str(p.resolve()) for p in (structs_h, packets_h, db_h, shuffle_h)}

    # --- typed structs. Macros are expanded as each line is seen, because packets.hpp
    # #undef-s helpers (MAX_ITEMLIST, MAX_ITEM_OPTIONS ...) at its end.
    model = Model(pp)
    toks = []

    def on_struct_line(path, lineno, text):
        toks.extend(model.expand(ctokens(text, lineno)))

    pp.process(packets_h, on_struct_line, emit_includes=[structs_h])
    model.scan(toks)

    # --- the packet table, in the order packetdb_readdb() includes the files
    raw = []  # every live definition: dicts in source order
    all_textual = {}  # cmd text per file -> count of textual definitions (live or not), by opcode value where literal

    def on_db_line(path, lineno, text):
        m = _ENTRY.match(text)
        if not m:
            if text.startswith("#"):
                return
            raise ExtractError(f"{path.name}:{lineno}: unrecognised live line: {text!r}")
        kind, argtext = m.groups()
        args = split_args(argtext)
        try:
            cmd = eval_arg(model, args[0])
            length = eval_arg(model, args[1])
            if kind == "packet":
                handler, offs = None, []
                if len(args) != 2:
                    raise ExtractError("packet() takes 2 arguments")
            else:
                handler = args[2]
                offs = [eval_arg(model, a) for a in args[3:]]
        except (CppError, ExtractError, IndexError) as e:
            raise ExtractError(f"{path.name}:{lineno}: {e}: {text}") from None
        pos = []
        for o in offs + [0]:  # parseable_packet appends a terminating 0
            if o == 0:
                break
            pos.append(o)
        if handler == "nullptr":
            handler = None
        raw.append({"cmd": cmd, "len": length, "handler": handler, "pos": pos, "kind": kind,
                    "src": f"map/{path.name}:{lineno}", "cmd_text": args[0], "len_text": args[1],
                    "over_pos": len(offs) >= MAX_PACKET_POS})

    pp.process(db_h, on_db_line)
    n_db = len(raw)
    pp.process(shuffle_h, on_db_line)
    n_shuffle = len(raw) - n_db

    # textual (target-independent) definition counts, to report redefinition across #if blocks
    textual = {}
    for fname in ("clif_packetdb.hpp", "clif_shuffle.hpp"):
        text = strip_comments((src / "map" / fname).read_text(encoding="utf-8", errors="replace"))
        for mm in _ENTRY_ANY.finditer(text):
            a0 = split_args(mm.group(2))[0]
            try:
                c = eval_arg(model, a0)
            except (CppError, ExtractError):
                continue
            textual[c] = textual.get(c, 0) + 1

    # --- final table: later definition wins; out-of-range ids are dropped by packetdb_addpacket
    final, shadowed, dropped = {}, {}, []
    for r in raw:
        c = r["cmd"]
        if c <= 0 or c > MAX_PACKET_DB:
            dropped.append(r)
            continue
        if c in final:
            shadowed.setdefault(c, []).append(final[c])
        final[c] = r

    docs = doc_names(src / "map/clif.cpp")
    structs_used = {}
    entries = []
    mismatches = []
    for c in sorted(final):
        r = final[c]
        e = {"opcode": c, "len": r["len"], "src": r["src"]}
        if r["len"] == -1:
            e["length_prefix_offset"] = 2
        if r["handler"]:
            e["handler"] = r["handler"]
            d = docs.get(r["handler"])
            if d:
                if c in d["by_opcode"]:
                    e["doc_name"] = d["by_opcode"][c]
                elif d["names"]:
                    e["handler_doc_names"] = list(d["names"])
        e["pos"] = r["pos"]
        # typed struct: by HEADER constant of this opcode
        names = model.by_id.get(c, [])
        cands = [n for n in names if model.header_struct[n] in model.structs]
        if r["cmd_text"].startswith("HEADER_") and r["cmd_text"][7:] in model.headers:
            cands = [r["cmd_text"][7:]] if model.header_struct[r["cmd_text"][7:]] in model.structs else []
        if len(cands) == 1:
            sname = model.header_struct[cands[0]]
            st = model.structs[sname]
            e["struct"] = sname
            e["name"] = cands[0]
            if st.size is None:
                e["fields"] = "unresolved"
                e["fields_problem"] = "; ".join(st.problems)[:200]
            else:
                if r["len"] != -1 and r["len"] != st.size:
                    mismatches.append((c, r["len"], sname, st.size))
                    e["struct_size_mismatch"] = st.size
                e["fields"] = field_list(st, model, structs_used)
                if st.flex is not None:
                    e["repeat"] = repeat_group(st, model, structs_used)
        elif len(cands) > 1:
            e["fields"] = "unknown"
            e["fields_note"] = "ambiguous: " + ",".join(cands)
        else:
            e["fields"] = "unknown"  # handler-parsed: layout lives in the handler, not in a struct
        if c in shadowed:
            e["shadows"] = [{"len": s["len"], "handler": s["handler"], "pos": s["pos"], "src": s["src"]}
                            for s in shadowed[c]]
        entries.append(e)

    # typed structs whose id is not in packet_db (mostly server-to-client writers, which the
    # table does not need because their length is implied by the struct)
    struct_only = []
    for hid in sorted(model.by_id):
        if hid in final:
            continue
        for nme in model.by_id[hid]:
            sname = model.header_struct[nme]
            st = model.structs.get(sname)
            if st is None:
                continue
            e = {"opcode": hid, "name": nme, "struct": sname}
            if st.size is None:
                e["fields"] = "unresolved"
                e["fields_problem"] = "; ".join(st.problems)[:200]
            else:
                e["size"] = st.size
                e["fields"] = field_list(st, model, structs_used)
                if st.flex is not None:
                    e["repeat"] = repeat_group(st, model, structs_used)
            struct_only.append(e)

    # struct table for sub-structs referenced by packets
    struct_table = {}
    pending = sorted(structs_used)
    while pending:
        nme = pending.pop()
        if nme in struct_table:
            continue
        st = model.structs[nme]
        struct_table[nme] = {"size": st.size, "fields": field_list(st, model, structs_used)}
        pending.extend(n for n in structs_used if n not in struct_table)
    spec = {
        "schema": SCHEMA,
        "kind": "legacy_packet_db",
        "packetver": packetver,
        "flavour": derived,
        "flavour_request": flavour,
        "hypothetical": hypothetical,
        "entries": entries,
        "struct_only": struct_only,
        "structs": struct_table,
        "macros": {k: pp.value(k) for k in ("PACKETVER", "PACKETVER_MAIN_NUM", "PACKETVER_RE_NUM", "PACKETVER_ZERO_NUM")
                   if k in pp.macros},
        "defined": sorted(k for k in ("PACKETVER_RE", "PACKETVER_ZERO", "PACKET_OBFUSCATION") if k in pp.macros),
        "zero_ids": sorted(pp.unknown_ids),
        "inputs": {f: sha256(src / f) for f in INPUT_FILES},
        "counts": None,
        "dropped": [{"cmd": r["cmd"], "src": r["src"]} for r in dropped],
        "mismatches": mismatches,
        "_raw": raw,        # in-memory only: every live definition in source order (cross-check input)
        "_model": model,
    }
    live_defs = len(raw)
    multi_live = sum(1 for c in final if c in shadowed)
    multi_text = sum(1 for c, k in textual.items() if k > 1)
    spec["counts"] = {
        "live_definitions": live_defs,
        "live_definitions_packetdb": n_db,
        "live_definitions_shuffle": n_shuffle,
        "live_opcodes": len(final),
        "parseable": sum(1 for e in entries if "handler" in e),
        "variable_length": sum(1 for e in entries if e["len"] == -1),
        "with_struct_fields": sum(1 for e in entries if isinstance(e["fields"], list)),
        "handler_parsed_fields_unknown": sum(1 for e in entries if e["fields"] == "unknown"),
        "fields_unresolved": sum(1 for e in entries if e["fields"] == "unresolved"),
        "struct_only_packets": len(struct_only),
        "opcodes_redefined_live": multi_live,
        "opcodes_defined_in_more_than_one_block_any_target": multi_text,
        "dropped_out_of_range": len(dropped),
    }
    return spec


_ENTRY_ANY = re.compile(r"^\s*(?://)?\s*(parseable_packet|packet)\s*\((.*)\)\s*;", re.M)


def _scalar_type(m: Member):
    if m.count is not None and m.ctype == "char":
        return "string"
    return m.type


def field_list(st: Struct, model: Model, structs_used: dict):
    out = []
    for m in st.members:
        if m.flex:
            continue
        f = {"name": m.name, "offset": m.offset}
        if m.bits is not None:
            f.update(type="bitfield", width=m.elem, bits=[{"name": n, "width": w} for n, w in m.bits])
            if m.count is not None:
                f["array_len"] = m.count
        elif m.struct is not None:
            f.update(struct=m.struct, width=m.elem)
            if m.count is not None:
                f["array_len"] = m.count
            structs_used[m.struct] = True
        else:
            t = _scalar_type(m)
            f["type"] = t
            if t == "string":
                f["width"] = m.count
            elif m.count is not None:
                f["array_len"] = m.count
        out.append(f)
    return out


def repeat_group(st: Struct, model: Model, structs_used: dict):
    m = st.flex
    g = {"offset": m.offset, "stride": m.elem, "name": m.name}
    if m.struct is not None:
        g["struct"] = m.struct
        structs_used[m.struct] = True
    else:
        g["type"] = m.type
    return g


# ---------------------------------------------------------------------------
# rendering (hand-written so the bytes are stable across PyYAML versions)
# ---------------------------------------------------------------------------

def y(v):
    if v is None:
        return "null"
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, int):
        return str(v)
    if isinstance(v, str):
        return json.dumps(v)
    if isinstance(v, list):
        return "[" + ", ".join(y(x) for x in v) + "]"
    if isinstance(v, dict):
        return "{ " + ", ".join(f"{k}: {y(x)}" for k, x in v.items()) + " }" if v else "{}"
    raise TypeError(type(v))


def render(spec) -> str:
    L = []
    t = spec
    L.append("# GENERATED by tools/legacy/extract_packets.py from the rAthena sources. DO NOT EDIT.")
    L.append("# Regenerate: python3 tools/legacy/extract_packets.py --legacy-root <repo> --packetver %d" % t["packetver"]
             + ("" if t["flavour_request"] == "auto" else " --flavour " + t["flavour_request"]))
    L.append("# Verified by `netproto_codegen.py --verify-legacy` (re-evaluates the legacy headers, fails on any diff)")
    L.append("# and by tools/legacy/crosscheck_gxx.py (g++ -E / compiled oracle).")
    L.append(f"schema: {t['schema']}")
    L.append(f"kind: {t['kind']}")
    L.append(f"packetver: {t['packetver']}")
    L.append(f"flavour: {t['flavour']}")
    L.append(f"hypothetical: {y(t['hypothetical'])}   # true: flavour forced; no legacy build of this PACKETVER selects it")
    L.append(f"flavour_macros: {y(t['macros'])}")
    L.append(f"defined_macros: {y(t['defined'])}")
    L.append(f"identifiers_evaluated_as_zero_in_conditions: {y(t['zero_ids'])}")
    L.append("framing:   # src/map/clif.cpp clif_parse + rules::anticheat::check_frame")
    L.append("  variable_length_marker: -1")
    L.append("  length_prefix_offset: 2")
    L.append(f"  min_opcode: {MIN_PACKET_DB}")
    L.append(f"  max_opcode: {MAX_PACKET_DB}")
    L.append("inputs:  # sha256 of every legacy file read")
    for k, v in t["inputs"].items():
        L.append(f"  {k}: {v}")
    L.append("counts:")
    for k, v in t["counts"].items():
        L.append(f"  {k}: {v}")
    L.append(f"dropped_out_of_range: {y(t['dropped'])}")
    L.append(f"struct_size_mismatches: {y([list(m) for m in t['mismatches']])}")
    L.append("packets:")
    for e in t["entries"]:
        L.append(f"  - opcode: 0x{e['opcode']:04x}")
        for k in ("name", "len", "length_prefix_offset", "handler", "doc_name", "handler_doc_names", "pos", "struct",
                  "struct_size_mismatch", "src"):
            if k in e:
                L.append(f"    {k}: {y(e[k])}")
        f = e["fields"]
        if isinstance(f, list):
            if not f:
                L.append("    fields: []")
            else:
                L.append("    fields:")
                for x in f:
                    L.append(f"      - {y(x)}")
        else:
            why = {"unknown": "handler-parsed: offsets in pos[], layout not in a struct" if "handler" in e
                   else "no typed struct and no handler in this table",
                   "unresolved": "struct could not be laid out"}[f]
            L.append(f"    fields: {f}   # {why}")
            for k in ("fields_note", "fields_problem"):
                if k in e:
                    L.append(f"    {k}: {y(e[k])}")
        if "repeat" in e:
            L.append(f"    repeat: {y(e['repeat'])}")
        if "shadows" in e:
            L.append("    shadows:   # earlier live definitions overwritten by this one")
            for s in e["shadows"]:
                L.append(f"      - {y(s)}")
    L.append("struct_only_packets:   # typed structs with a HEADER id that packet_db does not list (server-to-client writers)")
    if not t["struct_only"]:
        L[-1] += " []"
    for e in t["struct_only"]:
        L.append(f"  - opcode: 0x{e['opcode']:04x}")
        for k in ("name", "struct", "size"):
            if k in e:
                L.append(f"    {k}: {y(e[k])}")
        f = e["fields"]
        if isinstance(f, list):
            L.append("    fields:" if f else "    fields: []")
            for x in f:
                L.append(f"      - {y(x)}")
        else:
            L.append(f"    fields: {f}")
        if "repeat" in e:
            L.append(f"    repeat: {y(e['repeat'])}")
    L.append("structs:   # layouts of structs referenced by packet fields (little-endian, packed)")
    if not t["structs"]:
        L[-1] += " {}"
    for name in sorted(t["structs"]):
        s = t["structs"][name]
        L.append(f"  {name}:")
        L.append(f"    size: {y(s['size'])}")
        L.append("    fields:")
        for x in s["fields"]:
            L.append(f"      - {y(x)}")
    return "\n".join(L) + "\n"


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--legacy-root", required=True, help="repository root that contains src/")
    ap.add_argument("--packetver", type=int, default=20211103)
    ap.add_argument("--flavour", default="auto", choices=["auto", "main", "re", "zero"])
    ap.add_argument("--out", help="write here (default: print the counts only)")
    ap.add_argument("--check", metavar="FILE", help="exit 1 unless FILE equals the freshly generated text")
    a = ap.parse_args(argv)
    try:
        spec = build_spec(Path(a.legacy_root), a.packetver, a.flavour)
    except (ExtractError, CppError) as e:
        print(f"error: {e}", file=sys.stderr)
        return 2
    text = render(spec)
    if a.out:
        Path(a.out).write_text(text, encoding="utf-8")
    if a.check:
        if Path(a.check).read_text(encoding="utf-8") != text:
            print(f"error: {a.check} differs from the legacy evaluation", file=sys.stderr)
            return 1
    print(json.dumps({"flavour": spec["flavour"], **spec["counts"]}, indent=1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
