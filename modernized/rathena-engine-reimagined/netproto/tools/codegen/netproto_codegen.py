#!/usr/bin/env python3
"""netproto codegen: proto/*.yaml -> validated model -> generated C++ tables.

Implemented: validation, the frame-table emitter, the catalog emitter (the whole
spec as a C++ PacketCatalog) and `--verify-legacy`. Field codecs (decode/encode),
the sim-client bindings and fuzz-target emission are pending (CAP-E02).
Generated files go under generated/ and are never edited by hand.

Usage:
  netproto_codegen.py --proto proto/client_packets.yaml --client re/20211103 --out generated/
  netproto_codegen.py --proto proto/client_packets.yaml --emit-catalog --out generated/
  netproto_codegen.py --proto proto/client_packets.yaml --verify-legacy [--legacy-root REPO]

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
DIRS = {"cz": "ClientToServer", "zc": "ServerToClient", "inter": "Inter"}
# u64/f32 appear in legacy structs but have no FieldType in the C++ model yet
TYPES = {"u8", "u16", "u32", "u64", "i8", "i16", "i32", "i64", "f32", "string", "raw", "pos3", "pos6"}
CPP_TYPES = {"u8": "U8", "u16": "U16", "u32": "U32", "i8": "I8", "i16": "I16", "i32": "I32", "i64": "I64",
             "string": "FixedString", "raw": "Raw", "pos3": "Pos3", "pos6": "Pos6"}
TYPE_SIZE = {"u8": 1, "i8": 1, "u16": 2, "i16": 2, "u32": 4, "i32": 4, "f32": 4, "u64": 8, "i64": 8}
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
            for f in fields:
                if f["type"] not in TYPES:
                    raise SpecError(f"{n}.{f['name']}: bad type")
                if size is not None and f["offset"] >= size:
                    raise SpecError(f"{n}.{f['name']}: offset beyond fixed length")
                width = f.get("width") or TYPE_SIZE.get(f["type"], 0)
                if size is not None and f["offset"] + width * f.get("array_len", 1) > size:
                    raise SpecError(f"{n}.{f['name']}: field runs past the fixed length")


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
    """The whole spec (every packet, every version) as a C++ PacketCatalog builder."""
    gate_of = {"pre_session": "PreSession", "pre_map_load": "PreMapLoad", "in_world": "InWorld"}
    L = ["// GENERATED by tools/codegen/netproto_codegen.py. DO NOT EDIT.",
         f"// source: proto/{stem}.yaml", "#pragma once", '#include "netproto/spec.hpp"',
         "namespace netproto::generated {",
         f"inline PacketCatalog {stem}_catalog() {{",
         "  PacketCatalog c;"]
    for p in doc["packets"]:
        L.append("  {")
        L.append(f'    PacketDef d; d.name = "{p["name"]}"; d.direction = Direction::{DIRS[p["direction"]]};')
        L.append(f'    d.category = "{p.get("category", "")}"; d.gate = Phase::{gate_of[p.get("gate", "in_world")]};')
        for v in p["versions"]:
            L.append("    {")
            L.append(f'      PacketVersion v; v.applies = *VersionPredicate::parse("{v.get("applies", "")}");')
            L.append(f'      v.opcode = 0x{int(v["opcode"]):04x};')
            ln = v["length"]
            if "fixed" in ln:
                L.append(f'      v.length.kind = LengthKind::Fixed; v.length.fixed_size = {ln["fixed"]};')
            else:
                var = ln["variable"]
                L.append(f'      v.length.kind = LengthKind::Variable; v.length.prefix_offset = {var.get("prefix_offset", 2)}; '
                         f'v.length.header_size = {var.get("header", 4)};')
            fields = v.get("fields", [])
            if fields == "unknown":
                L.append("      v.fields_known = false;")
                fields = []
            for off in v.get("handler_offsets", []):
                L.append(f"      v.handler_offsets.push_back({off});")
            for f in fields:
                if f["type"] not in CPP_TYPES:
                    raise SpecError(f"{p['name']}.{f['name']}: type {f['type']} has no C++ FieldType yet")
                L.append(f'      {{ FieldSpec f; f.name = "{f["name"]}"; f.offset = {f["offset"]}; '
                         f'f.type = FieldType::{CPP_TYPES[f["type"]]}; f.width = {f.get("width", 0)}; '
                         f'f.array_len = {f.get("array_len", 1)}; v.fields.push_back(std::move(f)); }}')
            L.append("      d.versions.push_back(std::move(v));")
            L.append("    }")
        L.append("    c.add(std::move(d));")
        L.append("  }")
    L += ["  return c;", "}", "}  // namespace netproto::generated", ""]
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


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--proto", required=True)
    ap.add_argument("--client")
    ap.add_argument("--out")
    ap.add_argument("--check", action="store_true", help="validate and resolve only")
    ap.add_argument("--emit-catalog", action="store_true", help="emit <stem>_catalog() for the whole spec")
    ap.add_argument("--verify-legacy", action="store_true", help="fail if the spec differs from the legacy headers")
    ap.add_argument("--legacy-root", help="repository root containing src/ (default: found above --proto)")
    a = ap.parse_args(argv)
    doc = yaml.safe_load(Path(a.proto).read_text())
    try:
        validate(doc)
        resolved = resolve(doc, a.client) if a.client else None
        if a.client is None and not (a.verify_legacy or a.emit_catalog):
            raise SpecError("--client is required")
    except SpecError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
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
