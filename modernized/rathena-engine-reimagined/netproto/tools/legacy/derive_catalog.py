#!/usr/bin/env python3
"""Derive the full netproto packet catalog for ONE client target from the legacy sources.

  python3 tools/legacy/derive_catalog.py --legacy-root <repo> --out proto/packets_re_20211103.yaml
  python3 tools/legacy/derive_catalog.py --legacy-root <repo> --check proto/packets_re_20211103.yaml

Inputs (read-only): everything extract_packets.py reads, plus src/common/packets.hpp (login / char /
zone-hand-off packets), src/char/char_clif.cpp (CH_ENTER, which has no struct) and the curated semantic
overlay proto/client_packets.yaml (names, categories and gates the legacy data does not carry).

Where a layout comes from (`source:` of every version entry):
  typed-struct     a struct of src/map/packets_struct.hpp / packets.hpp whose HEADER id is this opcode
  common-struct    a struct of src/common/packets.hpp (login / char / zone)
  handler-derived  straight RFIFO reads of the clif_parse_* body (tools/legacy/handler_layout.py rules)
  handler          the opcode has a handler but its layout is not mechanical: `fields: unknown`
  length-only      the packet DB lists only a length (no struct, no handler): `fields: unknown`

Output is deterministic YAML in the schema netproto/yaml loader reads (see proto/client_packets.yaml).
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent / "codegen"))

import extract_packets as ep  # noqa: E402
import handler_layout as hl  # noqa: E402

TARGET_APPLIES = "RE>=20211103 && RE<20211104"

# Session-phase gates the legacy data does not carry. The login / char / hand-off phases follow the
# gateway's wire table (gateway/src/wire.cpp); everything else is in_world, with the two map-connect
# packets taken from the curated overlay.
PRE_SESSION_PREFIX = ("CA_", "CT_", "AC_", "TC_")
PRE_SESSION_NAMES = {"SC_NOTIFY_BAN", "CH_ENTER", "HC_ACCEPT_ENTER", "HC_ACCEPT_ENTER2", "HC_REFUSE_ENTER",
                     "HC_ACK_CHARINFO_PER_PAGE", "HC_CHARLIST_NOTIFY", "HC_BLOCK_CHARACTER", "ZC_REFUSE_ENTER"}
PRE_MAP_LOAD_PREFIX = ("CH_", "HC_")
PRE_MAP_LOAD_NAMES = {"PING"}
C2S_PREFIX = ("CZ_", "CA_", "CH_", "CT_")
S2C_PREFIX = ("ZC_", "HC_", "AC_", "SC_", "TC_")
EXTRA_HANDLERS = {   # opcode -> (file, function, name, doc line) for packets with a handler outside clif.cpp
    0x0065: ("char/char_clif.cpp", "chclif_parse_reqtoconnect", "CH_ENTER"),
}
GENERIC_CATEGORY = {"CA_": "login", "AC_": "login", "CT_": "login", "TC_": "login", "CH_": "char", "HC_": "char"}


def direction_of(name):
    if name == "PING":
        return "cz"
    if name.startswith(C2S_PREFIX):
        return "cz"
    if name.startswith(S2C_PREFIX):
        return "zc"
    return "any"


def gate_of(name, direction):
    if name in PRE_SESSION_NAMES:
        return "pre_session"
    if name.startswith(PRE_SESSION_PREFIX):
        return "pre_session"
    if name in PRE_MAP_LOAD_NAMES or name.startswith(PRE_MAP_LOAD_PREFIX):
        return "pre_map_load"
    if name.startswith("ZC_ACCEPT_ENTER"):
        return "pre_session"
    return "in_world"


# ---------------------------------------------------------------------------
# overlay
# ---------------------------------------------------------------------------

def load_overlay(path: Path, flavour: str, date: int):
    import yaml
    from netproto_codegen import matches
    doc = yaml.safe_load(path.read_text())
    out = {}
    for p in doc["packets"]:
        for v in p["versions"]:
            if matches(v.get("applies", ""), flavour, date):
                out[int(v["opcode"])] = {"name": p["name"], "category": p.get("category"), "gate": p.get("gate", "in_world"),
                                          "direction": p["direction"], "handler": p.get("legacy_handler")}
    return out


# ---------------------------------------------------------------------------
# struct flattening
# ---------------------------------------------------------------------------

def storage_type(nbytes):
    return {1: "u8", 2: "u16", 4: "u32", 8: "u64"}[nbytes]


class Flattener:
    def __init__(self, model, pos_names):
        self.model = model
        self.pos3, self.pos6 = pos_names

    def fields(self, st, base=0, prefix="", row=False, skip=None):
        out = []
        for m in st.members:
            if m.flex or m is skip:
                continue
            off = base + m.offset
            if m.bits is not None:
                n = m.count if m.count is not None else 1
                for i in range(n):
                    shift = 0
                    for bn, bw in m.bits:
                        nm = f"{prefix}{m.name}" + (f"[{i}]" if m.count is not None else "") + f".{bn}"
                        out.append(self._mk(nm, off + i * m.elem, storage_type(m.elem), row, bit_shift=shift, bit_width=bw))
                        shift += bw
            elif m.struct is not None:
                sub = self.model.structs[m.struct]
                n = m.count if m.count is not None else 1
                for i in range(n):
                    nm = f"{prefix}{m.name}" + (f"[{i}]" if m.count is not None else "") + "."
                    out += self.fields(sub, off + i * m.elem, nm, row)
            else:
                t = m.type
                nm = prefix + m.name
                if m.count is not None and m.ctype == "char" and m.inner:     # char names[10][24]: ten strings
                    for i in range(m.count):
                        out.append(self._mk(f"{nm}[{i}]", off + i * m.inner, "string", row, width=m.inner))
                elif m.count is not None and m.ctype == "char":
                    out.append(self._mk(nm, off, "string", row, width=m.count))
                elif m.count == 3 and t == "u8" and m.name in self.pos3:
                    out.append(self._mk(nm, off, "pos3", row))
                elif m.count == 6 and t == "u8" and m.name in self.pos6:
                    out.append(self._mk(nm, off, "pos6", row))
                else:
                    f = self._mk(nm, off, t, row)
                    if m.count is not None:
                        f["array_len"] = m.count
                    if m.ctype == "bool":
                        f["_bool"] = True   # kept for the layout oracle, not rendered: the wire byte is 0 or 1
                    out.append(f)
        return out

    @staticmethod
    def _mk(name, off, t, row, width=None, bit_shift=None, bit_width=None):
        f = {"name": name, "offset": off, "type": t}
        if width:
            f["width"] = width
        if row:
            f["row"] = True
        if bit_width:
            f["bit_shift"] = bit_shift
            f["bit_width"] = bit_width
        return f


def position_names(clif_text):
    """Struct member names that clif.cpp packs with WBUFPOS (3 bytes) / WBUFPOS2 (6 bytes)."""
    p3, p6 = set(), set()
    for m in re.finditer(r"\b[WR]BUFPOS(2?)\s*\(\s*&?\s*\w+\s*(?:\.|->)\s*(\w+)", clif_text):
        (p6 if m.group(1) else p3).add(m.group(2))
    return p3, p6


# ---------------------------------------------------------------------------
# typed packets
# ---------------------------------------------------------------------------

def typed_version(model, flat, st, name, db_len, db_pos, source, struct_name, meta=None):
    """-> version dict for a packet backed by struct `st`."""
    members_hdr = [m for m in st.members if not m.flex]
    # A struct that carries its own length word (packetLength / packetSize, int16/uint16 at offset 2) is a
    # variable-length packet even when the packet DB has no entry for it (login / char / hand-off packets).
    has_len_member = any(m.offset == 2 and not m.flex and m.type in ("i16", "u16") and m.count is None and
                         re.fullmatch(r"(?i)(packet)?_?(length|size|len)", m.name) for m in st.members)
    variable = db_len == -1 or st.flex is not None or (db_len is None and has_len_member)
    # A variable-length packet whose struct ends in a fixed-size ARRAY OF STRUCTS (itemlist: `list[MAX_ITEMLIST]`)
    # is the writer's maximum buffer: the wire frame carries as many rows as the writer filled. The array is the
    # repeat group; the header is everything before it.
    rep_member = None
    if variable and st.flex is None and members_hdr:
        last = members_hdr[-1]
        if last.struct is not None and last.count is not None and last.count > 1:
            rep_member = last
    fields = flat.fields(st, skip=rep_member)
    # drop packetType (offset 0, 16-bit) and, for variable packets, the length word at offset 2
    def is_hdr(f):
        if f["offset"] == 0 and f["type"] in ("i16", "u16") and "." not in f["name"]:
            return True
        return variable and f["offset"] == 2 and f["type"] in ("i16", "u16") and "." not in f["name"]
    fields = [f for f in fields if not is_hdr(f)]
    v = {"applies": TARGET_APPLIES, "source": source}
    extra = {}
    if meta is not None:
        meta.update({"struct": struct_name, "type_member": st.members[0].name if st.members else None,
                     "length_member": next((m.name for m in st.members if m.offset == 2 and not m.flex and m.type in ("i16", "u16")), None)
                     if variable else None, "flex": st.flex.name if st.flex is not None else (rep_member.name if rep_member is not None else None),
                     "flex_elem": st.flex.elem if st.flex is not None else (rep_member.elem if rep_member is not None else None),
                     "flex_char": bool(st.flex is not None and st.flex.struct is None and st.flex.ctype == "char"),
                     "size": st.size})
    if variable:
        hdr = st.flex.offset if st.flex is not None else (rep_member.offset if rep_member is not None else st.size)
        hdr = max(hdr, 4)
        length = {"variable": {"prefix_offset": 2, "header": hdr}}
        if rep_member is not None:
            length["variable"]["repeat"] = {"offset": rep_member.offset, "stride": rep_member.elem}
            fields += flat.fields(model.structs[rep_member.struct], 0, "", True)
        if st.flex is not None:
            m = st.flex
            if m.struct is not None:
                sub = model.structs[m.struct]
                length["variable"]["repeat"] = {"offset": m.offset, "stride": m.elem}
                fields += flat.fields(sub, 0, "", True)
            elif m.elem == 1 and m.ctype == "char":
                fields.append({"name": m.name, "offset": m.offset, "type": "string"})
            elif m.elem == 1:
                fields.append({"name": m.name, "offset": m.offset, "type": "raw"})
            else:
                length["variable"]["repeat"] = {"offset": m.offset, "stride": m.elem}
                fields.append({"name": m.name, "offset": 0, "type": m.type, "row": True})
    else:
        if db_len is not None and db_len != st.size:
            extra["legacy_db_length"] = db_len
        length = {"fixed": st.size}
    v["length"] = length
    v.update(extra)
    v["fields"] = fields
    return v


# ---------------------------------------------------------------------------
# the catalog
# ---------------------------------------------------------------------------

def build(root: Path, packetver: int, overlay_path: Path):
    src = root / "src"
    spec = ep.build_spec(root, packetver, "auto")
    flavour = spec["flavour"]
    date = packetver
    overlay = load_overlay(overlay_path, flavour, date)
    mmodel = spec["_model"]
    clif = hl.Source(src / "map/clif.cpp")
    pos3, pos6 = position_names(clif.text)
    mflat = Flattener(mmodel, (pos3, pos6))

    # common/packets.hpp model (a fresh preprocessor: PACKETS_HPP is also the guard of map/packets.hpp)
    pp, _, _ = ep.build_environment(src, packetver, "auto")
    ch = src / "common/packets.hpp"
    pp.strict |= {str(ch.resolve())}
    cmodel = ep.Model(pp)
    toks = []
    pp.process(ch, lambda path, ln, text: toks.extend(cmodel.expand(ep.ctokens(text, ln))))
    cmodel.scan(toks)
    cflat = Flattener(cmodel, (pos3, pos6))

    entries = {e["opcode"]: e for e in spec["entries"]}
    docs = ep.doc_names(src / "map/clif.cpp")
    clif_doc_names = {}
    for ln in (src / "map/clif.cpp").read_text(errors="replace").split("\n"):
        mo = re.match(r"^\s*///\s*([0-9a-fA-F]{4})\b.*\(([A-Z][A-Z0-9_]+)\)", ln)
        if mo:
            clif_doc_names.setdefault(int(mo.group(1), 16), mo.group(2))

    packets = []          # dicts in catalog order
    names, claimed = {}, {}
    stats = {"typed_struct": 0, "common_struct": 0, "handler_derived": 0, "handler_unknown": 0, "length_only": 0,
             "aliases_of_handler": 0, "struct_alias_names": 0, "unresolved_struct": 0, "lowercase_without_opcode": 0}
    notes = {"derived": [], "unknown": [], "aliases": [], "struct_aliases": [], "unresolved": []}

    def add(name, direction, opcode, version, category=None, gate=None, handler=None, struct=None, cmt=None):
        key = (direction, opcode)
        if name in names:
            raise SystemExit(f"duplicate packet name {name}")
        if key in claimed:
            raise SystemExit(f"opcode {opcode:#06x} claimed twice for {direction}: {claimed[key]} and {name}")
        names[name] = True
        claimed[key] = name
        p = {"name": name, "direction": direction}
        cat = category or next((c for pre, c in GENERIC_CATEGORY.items() if name.startswith(pre)), None)
        if cat:
            p["category"] = cat
        p["gate"] = gate or gate_of(name, direction)
        if handler:
            p["legacy_handler"] = handler
        if struct:
            p["struct"] = struct
        p["versions"] = [version]
        if cmt:
            p["_comment"] = cmt
        packets.append(p)

    typed_ops = set()

    # --- 1. typed structs of the map packet DB -------------------------------------------------
    for e in sorted(spec["entries"], key=lambda e: e["opcode"]):
        if e["fields"] == "unresolved":
            stats["unresolved_struct"] += 1
            notes["unresolved"].append((e["opcode"], e["name"], e.get("fields_problem", "")))
    # entries with list fields, then struct_only (several struct names may share an opcode: first wins)
    typed_src = []
    for e in spec["entries"]:
        if isinstance(e["fields"], list):
            typed_src.append((e["opcode"], e["name"], e["struct"], e["len"], e.get("pos", []), e.get("handler"), e))
    seen_so = {}
    for e in spec["struct_only"]:
        if e["fields"] == "unresolved":
            stats["unresolved_struct"] += 1
            notes["unresolved"].append((e["opcode"], e["name"], e.get("fields_problem", "")))
            continue
        typed_src.append((e["opcode"], e["name"], e["struct"], None, [], None, e))
    # second tier of src/map/packets_struct.hpp: `struct packet_<n>` whose opcode is the enumerator `<n>Type`
    # (the #if ladder at the top of the file picks the value for this PACKETVER, the extractor already
    # evaluated it). They have no HEADER_ id, so the first tier does not see them.
    usage = {}   # struct -> {opcode values assigned to its PacketType in clif.cpp}
    ctext = clif.text
    for m in re.finditer(r"\bstruct\s+(packet_\w+)\s+(\w+)\s*(?:;|=|\{)", ctext):
        sname_u, var = m.group(1), m.group(2)
        window = ctext[m.end(): m.end() + 3000]
        end = re.search(r"\n}\s*\n", window)
        if end:
            window = window[:end.start()]
        for am in re.finditer(r"\b" + re.escape(var) + r"\s*(?:\.|->)\s*(?:P|p)acketType\s*=\s*([A-Za-z_]\w*|0[xX][0-9a-fA-F]+|\d+)\s*;", window):
            tok = am.group(1)
            val = int(tok, 0) if tok[0].isdigit() else mmodel.consts.get(tok)
            usage.setdefault(sname_u, set()).add(val)
    for sname, st in sorted(mmodel.structs.items()):
        if not sname.startswith("packet_") or st.synthetic:
            continue
        base = sname[len("packet_"):]
        cname = base + "Type"
        how = "enumerator " + cname
        if cname in mmodel.consts:
            opcode = mmodel.consts[cname]
        else:
            vals = usage.get(sname, set())
            if len(vals) == 1 and None not in vals:
                opcode = next(iter(vals))
                how = "PacketType assignment in clif.cpp"
                cname = None
            else:
                stats["lowercase_without_opcode"] += 1
                notes["unresolved"].append((0, sname, "no single opcode: " + (", ".join(str(v) for v in sorted(vals, key=str)) or "no enumerator, no assignment")))
                continue
        if st.size is None:
            stats["unresolved_struct"] += 1
            notes["unresolved"].append((opcode, sname, "; ".join(st.problems)[:100]))
            continue
        db = entries.get(opcode)
        if db is not None and db["len"] != -1 and st.flex is None and db["len"] != st.size:
            stats["lowercase_without_opcode"] += 1
            notes["unresolved"].append((opcode, sname, f"size {st.size} disagrees with the packet DB length {db['len']}"))
            continue
        name = clif_doc_names.get(opcode)
        if name is None or name in names or any(t[1] == name for t in typed_src):
            name = "LEGACY_" + base.upper()
        typed_src.append((opcode, name, sname, db["len"] if db else None, db.get("pos", []) if db else [],
                          db.get("handler") if db else None, {"lower": True, "cname": cname, "base": base}))
    typed_src.sort(key=lambda t: (t[0], 2 if isinstance(t[6], dict) and t[6].get("lower") else (0 if t[5] is not None or t[3] is not None else 1)))
    for opcode, name, sname, db_len, pos, handler, e in typed_src:
        if opcode in typed_ops:
            stats["struct_alias_names"] += 1
            first = next(p for p in packets if p["versions"][0]["opcode"] == opcode)
            first["versions"][0].setdefault("also_named_by_legacy", []).append(name if not (isinstance(e, dict) and e.get("lower")) else sname)
            continue
        typed_ops.add(opcode)
        st = mmodel.structs[sname]
        lower = isinstance(e, dict) and e.get("lower")
        pname = overlay.get(opcode, {}).get("name", name)
        meta = {"scope": "map", "header": name, "header_const": (e["cname"] or f"0x{opcode:x}") if lower else "HEADER_" + name}
        v = typed_version(mmodel, mflat, st, pname, db_len, pos, "typed-struct", sname, meta)
        v["opcode"] = opcode
        if db_len is not None:
            v["handler_offsets"] = list(pos)
        direction = overlay.get(opcode, {}).get("direction") or direction_of(pname)
        if direction == "any":
            if lower and not pname.startswith(("CZ_", "ZC_")):
                direction = "cz" if e["base"].endswith(("_req", "_request")) or handler else "zc"
            else:
                direction = "zc" if opcode not in entries or entries[opcode].get("handler") is None else "cz"
        add(pname, direction, opcode, v, category=overlay.get(opcode, {}).get("category"),
            gate=overlay.get(opcode, {}).get("gate"), handler=handler, struct=sname)
        packets[-1]["_meta"] = meta
        stats["typed_struct"] += 1
    # --- 2. common/packets.hpp ------------------------------------------------------------------
    for name, opcode in sorted(cmodel.headers.items(), key=lambda kv: kv[1]):
        st = cmodel.structs.get("PACKET_" + name)
        if st is None or st.size is None:
            continue
        if opcode in typed_ops:
            # the same packet is already typed by the map headers (e.g. SC_NOTIFY_BAN): the layouts must agree
            first = next(p for p in packets if p["versions"][0]["opcode"] == opcode)
            if first["name"] != name:
                first["versions"][0].setdefault("also_named_by_legacy", []).append(name)
            continue
        typed_ops.add(opcode)
        db = entries.get(opcode)
        db_len = db["len"] if db else None
        meta = {"scope": "common", "header": name, "header_const": "HEADER_" + name}
        v = typed_version(cmodel, cflat, st, name, db_len, db.get("pos", []) if db else [], "common-struct", "PACKET_" + name, meta)
        v["opcode"] = opcode
        direction = direction_of(name)
        add(name, direction, opcode, v, category=overlay.get(opcode, {}).get("category"), struct="PACKET_" + name,
            handler=(db or {}).get("handler"))
        packets[-1]["_meta"] = meta
        stats["common_struct"] += 1

    # --- 3. handler-parsed (untyped) -------------------------------------------------------------
    untyped = [e for e in spec["entries"] if e["opcode"] not in typed_ops and e["fields"] == "unknown"]
    with_handler = [e for e in untyped if "handler" in e]
    # order of definition: file, then line (clif_shuffle.hpp is included after clif_packetdb.hpp)
    def src_key(e):
        f, ln = e["src"].rsplit(":", 1)
        return (0 if "packetdb" in f else 1, int(ln))
    groups = {}
    for e in with_handler:
        groups.setdefault(e["handler"], []).append(e)
    chosen = {}
    for h, es in groups.items():
        ov = [e for e in es if e["opcode"] in overlay]
        canon = ov[0] if ov else sorted(es, key=src_key)[-1]
        chosen[h] = canon
    sources = {"clif.cpp": clif}
    char_src = hl.Source(src / "char/char_clif.cpp")
    for h, canon in sorted(chosen.items(), key=lambda kv: kv[1]["opcode"]):
        es = groups[h]
        opcode = canon["opcode"]
        ovl = overlay.get(opcode, {})
        d = docs.get(h, {"by_opcode": {}, "names": []})
        if ovl.get("name"):
            name = ovl["name"]
        elif opcode in d["by_opcode"]:
            name = d["by_opcode"][opcode]
        elif len(d["names"]) >= 1:
            name = d["names"][0]
        else:
            name = "LEGACY_" + re.sub(r"^clif_parse_", "", h).upper()
        base = name
        k = 2
        while name in names:
            name = f"{base}_{k}"
            k += 1
        lay, why = hl.derive(clif, h, canon["pos"], canon["len"])
        v = {"applies": TARGET_APPLIES, "opcode": opcode}
        if canon["len"] == -1:
            hdr = 4
            v["length"] = {"variable": {"prefix_offset": 2, "header": hdr}}
        else:
            v["length"] = {"fixed": canon["len"]}
        v["handler_offsets"] = list(canon["pos"])
        aliases = sorted(e["opcode"] for e in es if e["opcode"] != opcode)
        if aliases:
            v["also_accepted_by_legacy"] = aliases
            stats["aliases_of_handler"] += len(aliases)
            notes["aliases"].append((h, opcode, aliases))
        if lay is not None and not (canon["len"] != -1 and not lay["fields"] and canon["len"] > 2):
            fields = []
            for f in lay["fields"]:
                ff = {"name": f["name"], "offset": f["offset"], "type": f["type"]}
                fields.append(ff)
            hdr = 4
            if canon["len"] == -1:
                hdr = max([4] + [f["offset"] + f["size"] for f in lay["fields"]])
                if lay["tail"]:
                    hdr = max(hdr, lay["tail"]["offset"])
                    if hdr != lay["tail"]["offset"]:
                        lay = None
                if lay is not None:
                    v["length"]["variable"]["header"] = hdr
                    if lay["tail"]:
                        fields.append({"name": "tail", "offset": lay["tail"]["offset"], "type": "raw"})
            if lay is not None:
                v["source"] = "handler-derived"
                v["fields"] = fields
                stats["handler_derived"] += 1
                notes["derived"].append((opcode, name, h, lay["line"]))
        if "fields" not in v:
            v["source"] = "handler"
            v["fields"] = "unknown"
            stats["handler_unknown"] += 1
            notes["unknown"].append((opcode, name, h, why if lay is None else "no reads recorded for a longer fixed packet"))
        add(name, "cz", opcode, v, category=ovl.get("category"), gate=ovl.get("gate"), handler=h,
            cmt=(f"clif.cpp:{clif.funcs[h][1]} {h}" if h in clif.funcs else f"{h} (no body in clif.cpp)"))
        typed_ops.add(opcode)
    # --- 4. char-server handler packets (no struct): CH_ENTER ----------------------------------
    for opcode, (rel, func, name) in sorted(EXTRA_HANDLERS.items()):
        e = entries.get(opcode)
        if e is None or opcode in typed_ops:
            continue
        lay, why = hl.derive(char_src, func, e["pos"], e["len"])
        v = {"applies": TARGET_APPLIES, "opcode": opcode, "length": {"fixed": e["len"]} if e["len"] != -1 else {"variable": {"prefix_offset": 2, "header": 4}},
             "handler_offsets": list(e["pos"])}
        if lay is not None and e["len"] != -1:
            v["source"] = "handler-derived"
            v["fields"] = [{"name": f["name"], "offset": f["offset"], "type": f["type"]} for f in lay["fields"]]
            stats["handler_derived"] += 1
            notes["derived"].append((opcode, name, func, lay["line"]))
        else:
            v["source"] = "handler"
            v["fields"] = "unknown"
            stats["handler_unknown"] += 1
            notes["unknown"].append((opcode, name, func, why))
        add(name, "cz", opcode, v, handler=func, cmt=f"{rel}:{char_src.funcs[func][1]} {func}")
        typed_ops.add(opcode)
    # --- 5. length-only frames -------------------------------------------------------------------
    for e in sorted(spec["entries"], key=lambda e: e["opcode"]):
        opcode = e["opcode"]
        if opcode in typed_ops or e["fields"] != "unknown" or "handler" in e:
            continue
        name = clif_doc_names.get(opcode)
        if name is None or name in names:
            name = f"OPAQUE_{opcode:04X}"
        v = {"applies": TARGET_APPLIES, "opcode": opcode, "source": "length-only",
             "length": {"fixed": e["len"]} if e["len"] != -1 else {"variable": {"prefix_offset": 2, "header": 4}},
             "fields": "unknown"}
        add(name, "any", opcode, v, gate="in_world")
        stats["length_only"] += 1
    packets.sort(key=lambda p: (p["versions"][0]["opcode"], p["direction"]))
    counts = {"packets": len(packets), **stats,
              "typed_total": stats["typed_struct"] + stats["common_struct"],
              "opcodes_in_packet_db": len(spec["entries"]), "struct_only_in_legacy": len(spec["struct_only"])}
    return {"spec": spec, "packets": packets, "counts": counts, "notes": notes, "overlay": overlay}


# ---------------------------------------------------------------------------
# rendering
# ---------------------------------------------------------------------------

def render(cat) -> str:
    y = ep.y
    spec = cat["spec"]
    c = cat["counts"]
    L = ["# GENERATED by tools/legacy/derive_catalog.py from the rAthena sources and proto/client_packets.yaml. DO NOT EDIT.",
         "# Regenerate: python3 tools/legacy/derive_catalog.py --legacy-root <repo> --out proto/packets_re_20211103.yaml",
         "# Verified by `netproto_codegen.py --verify-catalog` (re-derives and compares) and by tests/legacy_ref (compiled",
         "# legacy structs: sizeof/offsetof and golden byte vectors).",
         "# Layout provenance is the `source:` of every version entry: typed-struct | common-struct | handler-derived |",
         "# handler (fields: unknown) | length-only (fields: unknown). Nothing here is guessed; see the README.",
         "schema: 1",
         "kind: catalog",
         f"packetver: {spec['packetver']}",
         f"flavour: {spec['flavour']}",
         "client_versions:",
         '  - { id: "re/20211103", flavour: re, date: 20211103, obfuscation: { keys_env: NETPROTO_KEYS_RE_20211103 } }',
         "counts:"]
    for k, v in c.items():
        L.append(f"  {k}: {v}")
    L.append("packets:")
    for p in cat["packets"]:
        if "_comment" in p:
            L.append(f"  # {p['_comment']}")
        L.append(f"  - name: {p['name']}")
        L.append(f"    direction: {p['direction']}")
        for k in ("category", "gate", "legacy_handler", "struct"):
            if k in p:
                L.append(f"    {k}: {p[k]}")
        L.append("    versions:")
        for v in p["versions"]:
            L.append(f"      - applies: {y(v['applies'])}")
            L.append(f"        opcode: 0x{v['opcode']:04x}")
            ln = v["length"]
            if "fixed" in ln:
                L.append(f"        length: {{ fixed: {ln['fixed']} }}")
            else:
                var = ln["variable"]
                rep = f", repeat: {{ offset: {var['repeat']['offset']}, stride: {var['repeat']['stride']} }}" if "repeat" in var else ""
                L.append(f"        length: {{ variable: {{ prefix_offset: {var['prefix_offset']}, header: {var['header']}{rep} }} }}")
            L.append(f"        source: {v['source']}")
            if "handler_offsets" in v:
                L.append(f"        handler_offsets: {y(v['handler_offsets'])}")
            for k in ("legacy_db_length",):
                if k in v:
                    L.append(f"        {k}: {v[k]}")
            if "also_accepted_by_legacy" in v:
                L.append(f"        also_accepted_by_legacy: {y(['0x%04x' % o for o in v['also_accepted_by_legacy']]).replace(chr(34), '')}")
            if "also_named_by_legacy" in v:
                L.append(f"        also_named_by_legacy: {y(v['also_named_by_legacy'])}")
            f = v["fields"]
            if f == "unknown":
                L.append("        fields: unknown")
            elif not f:
                L.append("        fields: []")
            else:
                L.append("        fields:")
                for x in f:
                    L.append("          - " + y({k: val for k, val in x.items() if not k.startswith("_")}))
    return "\n".join(L) + "\n"


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--legacy-root", required=True)
    ap.add_argument("--packetver", type=int, default=20211103)
    ap.add_argument("--overlay", default=str(HERE.parent.parent / "proto/client_packets.yaml"))
    ap.add_argument("--out")
    ap.add_argument("--check", metavar="FILE")
    ap.add_argument("--report", action="store_true")
    a = ap.parse_args(argv)
    cat = build(Path(a.legacy_root), a.packetver, Path(a.overlay))
    text = render(cat)
    if a.out:
        Path(a.out).write_text(text)
    rc = 0
    if a.check:
        if Path(a.check).read_text() != text:
            print(f"error: {a.check} differs from the freshly derived catalog", file=sys.stderr)
            rc = 1
    if a.report or not (a.out or a.check):
        import json
        print(json.dumps(cat["counts"], indent=1))
    return rc


if __name__ == "__main__":
    sys.exit(main())
