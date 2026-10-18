#!/usr/bin/env python3
"""netproto codegen: proto/*.yaml -> validated model -> generated C++ tables.

Implemented: validation, the frame-table emitter, the catalog emitter (the whole
spec as compact C++ row tables, see include/netproto/catalog_tables.hpp), `--verify-legacy` and
`--verify-catalog`. The field codecs themselves are table-driven C++ (src/codec.cpp) over the emitted
catalog, so there is no per-packet generated decode/encode code to review.
Generated files go under generated/ and are never edited by hand.

Usage:
  netproto_codegen.py --proto proto/client_packets.yaml --client re/20211103 --out generated/
  netproto_codegen.py --proto proto/client_packets.yaml --emit-catalog --out generated/
  netproto_codegen.py --proto proto/client_packets.yaml --verify-legacy [--legacy-root REPO]
  netproto_codegen.py --proto proto/packets_re_20211103.yaml --verify-catalog [--legacy-root REPO]

--verify-legacy re-evaluates the legacy packet headers (tools/legacy/extract_packets.py, a
subset C preprocessor over src/map/clif_packetdb.hpp and friends) and fails (exit 1) when
  1. the checked-in proto/legacy_<date>.yaml differs from the fresh evaluation, or
  2. a version entry of client_packets.yaml disagrees with the legacy table for any verified
     (flavour, date) it applies to (opcode, length, variable-length prefix, handler, handler
     offsets, struct fields, aliases), or applies to none of them.
"""
import argparse
import difflib
import os
import re
import sys
from pathlib import Path

import yaml

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "legacy"))

GATES = {"pre_session", "pre_map_load", "in_world"}
DIRS = {"cz": "ClientToServer", "zc": "ServerToClient", "inter": "Inter", "any": "Any"}
# u64/f32 appear in legacy structs but have no FieldType in the C++ model yet
TYPES = {"u8", "u16", "u32", "u64", "i8", "i16", "i32", "i64", "f32", "string", "raw", "pos3", "pos6"}
# FieldType enumerators in include/netproto/spec.hpp, in declaration order
CPP_TYPES = {"u8": 0, "u16": 1, "u32": 2, "i8": 3, "i16": 4, "i32": 5, "i64": 6, "string": 7, "raw": 8,
             "pos3": 9, "pos6": 10, "u64": 11, "f32": 12}
TYPE_SIZE = {"u8": 1, "i8": 1, "u16": 2, "i16": 2, "u32": 4, "i32": 4, "f32": 4, "u64": 8, "i64": 8, "pos3": 3, "pos6": 6}
TERM = re.compile(r"^\s*(MAIN|RE|ZERO)\s*>=\s*(\d{8})\s*(?:&&\s*(MAIN|RE|ZERO)\s*<\s*(\d{8}))?\s*$")


class SpecError(Exception):
    pass


def matches(expr, flavour, date):
    if not expr or not expr.strip():
        return True
    for term in expr.split("||"):
        m = TERM.match(term)
        if not m:
            raise SpecError(f"bad version predicate: {expr!r}")
        fl, lo, fl2, hi = m.groups()
        if fl2 and fl2 != fl:
            raise SpecError(f"mixed flavours in term: {term!r}")
        if fl.lower() == flavour and date >= int(lo) and (not hi or date < int(hi)):
            return True
    return False


def validate(doc):
    if doc.get("schema") != 1:
        raise SpecError("unsupported schema version")
    ids = set()
    for c in doc.get("client_versions", []):
        if c["id"] in ids:
            raise SpecError(f"duplicate client version {c['id']}")
        ids.add(c["id"])
        if c["id"] != f"{c['flavour']}/{c['date']}":
            raise SpecError(f"client id {c['id']} does not match its flavour/date")
    names = set()
    for p in doc["packets"]:
        n = p["name"]
        if n in names:
            raise SpecError(f"duplicate packet {n}")
        names.add(n)
        if p["direction"] not in DIRS:
            raise SpecError(f"{n}: bad direction")
        if p.get("gate", "in_world") not in GATES:
            raise SpecError(f"{n}: bad gate")
        for v in p["versions"]:
            matches(v.get("applies", ""), "main", 0)
            if not 0 <= int(v["opcode"]) <= 0xFFFF:
                raise SpecError(f"{n}: opcode out of range")
            ln = v["length"]
            if ("fixed" in ln) == ("variable" in ln):
                raise SpecError(f"{n}: length must be fixed xor variable")
            size = ln.get("fixed")
            if "variable" in ln and ln["variable"].get("header", 4) < 4:
                raise SpecError(f"{n}: variable header < 4")
            fields = v.get("fields", [])
            if fields != "unknown" and not isinstance(fields, list):
                raise SpecError(f"{n}: fields must be a list or the word 'unknown'")
            if fields == "unknown":
                fields = []
            var = ln.get("variable") or {}
            rep = var.get("repeat")
            header = var.get("header", 4)
            for f in fields:
                if f["type"] not in TYPES:
                    raise SpecError(f"{n}.{f['name']}: bad type")
                tail = f["type"] in ("string", "raw") and not f.get("width")
                width = f.get("width") or TYPE_SIZE.get(f["type"], 0)
                total = width * f.get("array_len", 1)
                if f.get("row"):
                    if rep is None:
                        raise SpecError(f"{n}.{f['name']}: row field without a repeat group")
                    if f["offset"] + total > rep["stride"]:
                        raise SpecError(f"{n}.{f['name']}: row field runs past the stride")
                    continue
                if tail:
                    if size is not None:
                        raise SpecError(f"{n}.{f['name']}: tail field in a fixed-length packet")
                    if f["offset"] != header:
                        raise SpecError(f"{n}.{f['name']}: tail must start at the header size")
                    continue
                if size is not None and f["offset"] >= size:
                    raise SpecError(f"{n}.{f['name']}: offset beyond fixed length")
                if size is not None and f["offset"] + total > size:
                    raise SpecError(f"{n}.{f['name']}: field runs past the fixed length")
                if size is None and f["offset"] + total > header:
                    raise SpecError(f"{n}.{f['name']}: field runs past the header")


def resolve(doc, client):
    flavour, date = client.split("/")
    date = int(date)
    out, seen = [], {}
    for p in doc["packets"]:
        hits = [v for v in p["versions"] if matches(v.get("applies", ""), flavour, date)]
        if len(hits) > 1:
            raise SpecError(f"{p['name']}: ambiguous layouts on {client}")
        if not hits:
            continue
        key = (p["direction"], int(hits[0]["opcode"]))
        if key in seen:
            raise SpecError(f"opcode 0x{key[1]:04x} claimed by {seen[key]} and {p['name']} on {client}")
        seen[key] = p["name"]
        out.append((p, hits[0]))
    return out


# ---------------------------------------------------------------------------
# emitters
# ---------------------------------------------------------------------------

def _ident(client):
    return re.sub(r"[^a-z0-9]", "_", client.lower())


def emit_frames(resolved, client):
    lines = ["// GENERATED by tools/codegen/netproto_codegen.py. DO NOT EDIT.",
             f"// client: {client}", "#pragma once", '#include "netproto/framing.hpp"',
             "namespace netproto::generated {",
             f"inline FrameTable frames_{_ident(client)}() {{",
             "  FrameTable t;"]
    for p, v in resolved:
        if p["direction"] != "cz":
            continue
        gate = {"pre_session": "PreSession", "pre_map_load": "PreMapLoad", "in_world": "InWorld"}[p.get("gate", "in_world")]
        ln = v["length"]
        if "fixed" in ln:
            rule = f"LengthRule{{LengthKind::Fixed, {ln['fixed']}, 2, 4, std::nullopt}}"
        else:
            var = ln["variable"]
            rule = f"LengthRule{{LengthKind::Variable, 0, {var.get('prefix_offset', 2)}, {var.get('header', 4)}, std::nullopt}}"
        lines.append(f'  t[0x{int(v["opcode"]):04x}] = FrameEntry{{"{p["name"]}", {rule}, Phase::{gate}}};')
    lines += ["  return t;", "}", "}  // namespace netproto::generated", ""]
    return "\n".join(lines)


def emit_catalog(doc, stem):
    """The whole spec (every packet, every version) as C++ row tables (netproto/catalog_tables.hpp)."""
    gate_of = {"pre_session": 0, "pre_map_load": 1, "in_world": 2}
    dir_of = {"cz": 0, "zc": 1, "inter": 2, "any": 3}

    def q(x):
        return '"' + str(x).replace("\\", "\\\\").replace('"', '\\"') + '"'

    frows, prows, offs = ['  {"", 0, 0, 0, 1, 0, 0, 0, ""},  // keeps the array non-empty (index 0 is never used)'], [], [0]
    for p in doc["packets"]:
        for v in p["versions"]:
            fields = v.get("fields", [])
            known = fields != "unknown"
            if not known:
                fields = []
            first = len(frows)
            for f in fields:
                if f["type"] not in CPP_TYPES:
                    raise SpecError(f"{p['name']}.{f['name']}: type {f['type']} has no C++ FieldType")
                frows.append(f'  {{{q(f["name"])}, {f["offset"]}, {CPP_TYPES[f["type"]]}, {f.get("width", 0)}, {f.get("array_len", 1)}, '
                             f'{1 if f.get("row") else 0}, {f.get("bit_shift", 0)}, {f.get("bit_width", 0)}, {q(f.get("present_when", ""))}}},')
            o_first = len(offs)
            offs.extend(int(o) for o in v.get("handler_offsets", []))
            ln = v["length"]
            if "fixed" in ln:
                geo = f"0, {1 if known else 0}, {ln['fixed']}, 2, 4, 0, 0"
            else:
                var = ln["variable"]
                rep = var.get("repeat")
                geo = (f"1, {1 if known else 0}, 0, {var.get('prefix_offset', 2)}, {var.get('header', 4)}, "
                       f"{rep['offset'] if rep else 0}, {rep['stride'] if rep else 0}")
            prows.append(f'  {{{q(p["name"])}, {dir_of[p["direction"]]}, {gate_of[p.get("gate", "in_world")]}, {q(p.get("category", ""))}, '
                         f'{q(v.get("applies", ""))}, {q(v.get("source", ""))}, {int(v["opcode"])}, {geo}, '
                         f'{first}, {len(fields)}, {o_first}, {len(v.get("handler_offsets", []))}}},')
    L = ["// GENERATED by tools/codegen/netproto_codegen.py. DO NOT EDIT.",
         f"// source: proto/{stem}.yaml", "#pragma once", '#include "netproto/catalog_tables.hpp"',
         "namespace netproto::generated {",
         f"inline PacketCatalog {stem}_catalog() {{",
         "  static const FieldRow kFields[] = {"]
    L += frows
    L += ["  };", "  static const PacketRow kPackets[] = {"] + prows + ["  };",
          "  static const std::uint16_t kOffsets[] = {" + ", ".join(str(o) for o in offs) + "};",
          "  return build_catalog(kPackets, kFields, kOffsets);", "}", "}  // namespace netproto::generated", ""]
    return "\n".join(L)


# ---------------------------------------------------------------------------
# --verify-legacy
# ---------------------------------------------------------------------------

def find_legacy_root(explicit, proto):
    cands = [explicit, os.environ.get("RATHENA_LEGACY_ROOT")]
    p = Path(proto).resolve()
    cands += list(p.parents)
    for c in cands:
        if c and (Path(c) / "src/map/clif_packetdb.hpp").is_file():
            return Path(c)
    return None


def _norm_field(f):
    return {k: f[k] for k in ("name", "offset", "type", "width", "array_len") if k in f}


def verify_legacy(doc, proto_path, legacy_root, log=print):
    """Return a list of error strings (empty = the spec agrees with the legacy headers)."""
    import extract_packets as ep  # noqa: E402  (tools/legacy)

    errors = []
    root = find_legacy_root(legacy_root, proto_path)
    if root is None:
        return ["cannot find the legacy tree (need src/map/clif_packetdb.hpp): pass --legacy-root or set RATHENA_LEGACY_ROOT"]

    proto_dir = Path(proto_path).resolve().parent
    tables = {}  # (flavour, date) -> spec dict

    def table(flavour, date):
        if (flavour, date) not in tables:
            tables[(flavour, date)] = ep.build_spec(root, date, flavour)
        return tables[(flavour, date)]

    # 1. the checked-in legacy tables equal a fresh evaluation
    dates = sorted({c["date"] for c in doc["client_versions"]})
    for date in dates:
        fresh = table("auto", date)
        legacy_file = proto_dir / f"legacy_{date}.yaml"
        if not legacy_file.is_file():
            errors.append(f"missing {legacy_file.name}: run tools/legacy/extract_packets.py --packetver {date}")
            continue
        want = ep.render(fresh)
        have = legacy_file.read_text(encoding="utf-8")
        if want != have:
            diff = list(difflib.unified_diff(have.splitlines(), want.splitlines(), "checked-in", "fresh", n=0, lineterm=""))
            errors.append(f"{legacy_file.name} differs from the legacy evaluation ({len(diff)} diff lines); first: "
                          + " | ".join(diff[2:8]))
        else:
            log(f"ok: {legacy_file.name} equals the fresh legacy evaluation "
                f"({fresh['counts']['live_opcodes']} opcodes, flavour {fresh['flavour']})")

    # 2. every version entry agrees with the legacy table of each verified target it applies to
    targets = [(c["flavour"], c["date"]) for c in doc["client_versions"]]
    checked = 0
    for p in doc["packets"]:
        for v in p["versions"]:
            applied = [(fl, dt) for fl, dt in targets if matches(v.get("applies", ""), fl, dt)]
            if not applied:
                errors.append(f"{p['name']} 0x{v['opcode']:04x}: applies to no verified client target (unverified entry)")
                continue
            for fl, dt in applied:
                t = table(fl, dt)
                ent = {e["opcode"]: e for e in t["entries"]}.get(v["opcode"])
                kind = "packet_db"
                if ent is None:
                    ent = {e["opcode"]: e for e in t["struct_only"]}.get(v["opcode"])
                    kind = "struct_only"
                where = f"{p['name']} {fl}/{dt} 0x{v['opcode']:04x}"
                if ent is None:
                    errors.append(f"{where}: opcode is not in the legacy table")
                    continue
                checked += 1
                ln = v["length"]
                if kind == "packet_db":
                    if "fixed" in ln and ent["len"] != ln["fixed"]:
                        errors.append(f"{where}: length fixed {ln['fixed']} but legacy has {ent['len']}")
                    if "variable" in ln:
                        if ent["len"] != -1:
                            errors.append(f"{where}: declared variable but legacy length is {ent['len']}")
                        if ent.get("length_prefix_offset") != ln["variable"].get("prefix_offset", 2):
                            errors.append(f"{where}: length prefix offset differs from legacy")
                    if ent.get("handler") != p.get("legacy_handler"):
                        errors.append(f"{where}: legacy handler {ent.get('handler')!r} != legacy_handler {p.get('legacy_handler')!r}")
                    if v.get("handler_offsets", []) != ent["pos"]:
                        errors.append(f"{where}: handler_offsets {v.get('handler_offsets', [])} != legacy pos {ent['pos']}")
                    want_alias = sorted(o for o, e in {e["opcode"]: e for e in t["entries"]}.items()
                                        if p.get("legacy_handler") and e.get("handler") == p["legacy_handler"] and o != v["opcode"])
                    if sorted(v.get("also_accepted_by_legacy", [])) != want_alias:
                        errors.append(f"{where}: also_accepted_by_legacy {[hex(x) for x in sorted(v.get('also_accepted_by_legacy', []))]}"
                                      f" != legacy {[hex(x) for x in want_alias]}")
                else:
                    if "fixed" not in ln or ln["fixed"] != ent.get("size"):
                        errors.append(f"{where}: length {ln} != legacy struct size {ent.get('size')}")
                    if p.get("legacy_handler"):
                        errors.append(f"{where}: legacy_handler set but the legacy entry is a struct-only server packet")
                # fields
                lf = ent["fields"]
                yf = v.get("fields", "unknown")
                if yf == "unknown":
                    if lf != "unknown" and kind == "packet_db":
                        errors.append(f"{where}: legacy has typed struct fields; spec says unknown")
                    if kind == "struct_only":
                        errors.append(f"{where}: struct-only packet must carry its struct fields")
                else:
                    if not isinstance(lf, list):
                        errors.append(f"{where}: spec lists fields but legacy fields are {lf!r}")
                    else:
                        lf2 = [f for f in lf if not (f["offset"] == 0 and f.get("type") in ("i16", "u16"))]
                        if ln.get("variable") is not None:
                            lf2 = [f for f in lf2 if f["offset"] != 2 or f.get("type") not in ("i16", "u16")]
                        if [_norm_field(f) for f in yf] != lf2:
                            errors.append(f"{where}: fields differ from the legacy struct {ent.get('struct')}")
    log(f"checked {checked} (packet, version, target) combinations against the legacy tables")
    return errors


SOURCES = {"typed-struct", "common-struct", "handler-derived", "handler", "length-only"}


def verify_catalog(doc, proto_path, legacy_root, overlay_path, log=print):
    """Return a list of error strings (empty = the derived catalog is current and consistent)."""
    import derive_catalog as dc  # noqa: E402  (tools/legacy)

    errors = []
    root = find_legacy_root(legacy_root, proto_path)
    if root is None:
        return ["cannot find the legacy tree (need src/map/clif_packetdb.hpp): pass --legacy-root or set RATHENA_LEGACY_ROOT"]
    date = doc["packetver"]
    cat = dc.build(root, date, Path(overlay_path))
    want = dc.render(cat)
    have = Path(proto_path).read_text(encoding="utf-8")
    if want != have:
        diff = list(difflib.unified_diff(have.splitlines(), want.splitlines(), "checked-in", "fresh", n=0, lineterm=""))
        errors.append(f"{Path(proto_path).name} differs from the fresh derivation ({len(diff)} diff lines); first: "
                      + " | ".join(diff[2:8]))
    else:
        log(f"ok: {Path(proto_path).name} equals the fresh derivation ({len(doc['packets'])} packets)")
    # structural invariants (independent of the derivation code)
    names, claimed = set(), {}
    for p in doc["packets"]:
        n = p["name"]
        if n in names:
            errors.append(f"duplicate packet name {n}")
        names.add(n)
        if len(p["versions"]) != 1:
            errors.append(f"{n}: expected one version entry")
        for v in p["versions"]:
            key = (p["direction"], int(v["opcode"]))
            if key in claimed:
                errors.append(f"{n}: opcode 0x{key[1]:04x} already claimed by {claimed[key]} ({key[0]})")
            claimed[key] = n
            if v.get("source") not in SOURCES:
                errors.append(f"{n}: missing or unknown source {v.get('source')!r}")
            f = v.get("fields")
            if (f == "unknown") != (v.get("source") in ("handler", "length-only")):
                errors.append(f"{n}: source {v.get('source')} disagrees with fields {'unknown' if f == 'unknown' else 'known'}")
            if isinstance(f, list):
                seen = set()
                for x in f:
                    k = (bool(x.get("row")), x["name"])
                    if k in seen:
                        errors.append(f"{n}: duplicate field {x['name']}")
                    seen.add(k)
    # the curated overlay is a consistent subset
    import yaml
    ov = yaml.safe_load(Path(overlay_path).read_text())
    by_name = {p["name"]: p for p in doc["packets"]}
    for p in ov["packets"]:
        for v in p["versions"]:
            if not matches(v.get("applies", ""), doc["flavour"], date):
                continue
            c = by_name.get(p["name"])
            if c is None:
                errors.append(f"overlay packet {p['name']} is missing from the catalog")
                continue
            cv = c["versions"][0]
            if int(cv["opcode"]) != int(v["opcode"]):
                errors.append(f"overlay {p['name']}: opcode {v['opcode']:#06x} != catalog {cv['opcode']:#06x}")
            if cv["length"] != v["length"] and not (
                    "variable" in cv["length"] and "variable" in v["length"]
                    and cv["length"]["variable"].get("prefix_offset", 2) == v["length"]["variable"].get("prefix_offset", 2)):
                errors.append(f"overlay {p['name']}: length {v['length']} != catalog {cv['length']}")
            if c["direction"] != p["direction"] or c.get("gate", "in_world") != p.get("gate", "in_world"):
                errors.append(f"overlay {p['name']}: direction/gate differ from the catalog")
    log(f"checked {len(doc['packets'])} catalog packets and the {len(ov['packets'])} overlay packets")
    return errors


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--proto", required=True)
    ap.add_argument("--client")
    ap.add_argument("--out")
    ap.add_argument("--check", action="store_true", help="validate and resolve only")
    ap.add_argument("--emit-catalog", action="store_true", help="emit <stem>_catalog() for the whole spec")
    ap.add_argument("--verify-legacy", action="store_true", help="fail if the spec differs from the legacy headers")
    ap.add_argument("--verify-catalog", action="store_true", help="fail if the derived catalog is stale or inconsistent")
    ap.add_argument("--overlay", help="curated overlay for --verify-catalog (default: client_packets.yaml next to --proto)")
    ap.add_argument("--legacy-root", help="repository root containing src/ (default: found above --proto)")
    a = ap.parse_args(argv)
    doc = yaml.safe_load(Path(a.proto).read_text())
    try:
        validate(doc)
        resolved = resolve(doc, a.client) if a.client else None
        if a.client is None and not (a.verify_legacy or a.emit_catalog or a.verify_catalog):
            raise SpecError("--client is required")
    except SpecError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    if a.verify_catalog:
        overlay = a.overlay or str(Path(a.proto).resolve().parent / "client_packets.yaml")
        try:
            errs = verify_catalog(doc, a.proto, a.legacy_root, overlay)
        except Exception as e:
            print(f"error: catalog derivation failed: {e}", file=sys.stderr)
            return 2
        for e in errs:
            print(f"verify-catalog: {e}", file=sys.stderr)
        if errs:
            print(f"verify-catalog FAILED ({len(errs)} problem(s))", file=sys.stderr)
            return 1
        print("verify-catalog OK")
        return 0
    if a.verify_legacy:
        try:
            errs = verify_legacy(doc, a.proto, a.legacy_root)
        except Exception as e:  # CppError / ExtractError from the evaluator
            print(f"error: legacy evaluation failed: {e}", file=sys.stderr)
            return 2
        for e in errs:
            print(f"verify-legacy: {e}", file=sys.stderr)
        if errs:
            print(f"verify-legacy FAILED ({len(errs)} problem(s))", file=sys.stderr)
            return 1
        print("verify-legacy OK")
        return 0
    if a.check:
        return 0
    if not a.out:
        print("error: --out is required", file=sys.stderr)
        return 1
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    try:
        if a.emit_catalog:
            stem = Path(a.proto).stem
            (out / f"catalog_{stem}.hpp").write_text(emit_catalog(doc, stem))
        if a.client:
            (out / f"frames_{a.client.replace('/', '_')}.hpp").write_text(emit_frames(resolved, a.client))
    except SpecError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
