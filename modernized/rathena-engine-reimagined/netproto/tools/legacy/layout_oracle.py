#!/usr/bin/env python3
"""Golden byte vectors for every typed legacy struct, produced by the REAL compiler.

For each packet of the derived catalog whose layout comes from a legacy struct (typed-struct /
common-struct) this tool writes a small C++ program against the unmodified legacy headers
(src/map/packets.hpp, src/common/packets.hpp), fills an instance member by member with deterministic
pseudo-random values (strings, arrays, nested structs, bit fields, flexible arrays; positions through the
legacy WBUFPOS / WBUFPOS2 routines), compiles it with g++ and prints the raw bytes of the object. The
expected field values (what netproto must decode from those bytes) are written next to them.

  python3 tools/legacy/layout_oracle.py --legacy-root <repo> --out tests/golden/typed_re_20211103.txt

The legacy tree is read-only; the harness lives in --work-dir. Format of the output (one packet):
  P <NAME> <opcode> <size> <rows>
  B <hex bytes>
  V <field> <kind> <value...>            kinds: i int | s hex-or-'-' | r hex-or-'-' | a n v.. | p3 x y d |
  R <row> <field> <kind> <value...>             p6 x0 y0 x1 y1 sx0 sy0 | f float-bits-hex
  E
"""
from __future__ import annotations

import argparse
import random
import subprocess
import sys
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent / "codegen"))
import derive_catalog as dc  # noqa: E402

BITS = {"u8": 8, "i8": 8, "u16": 16, "i16": 16, "u32": 32, "i32": 32, "u64": 64, "i64": 64}
SIGNED = {"i8", "i16", "i32", "i64"}
ROWS = 2
TAIL_TEXT_MAX = 9

# Copies of the legacy routines (src/map/clif.cpp, rAthena, GPL-3.0-or-later; see LICENCE in the legacy tree).
LEGACY_POS = r'''
#include <type_traits>
#define NP_SET(lv, v) lv = static_cast<std::remove_reference_t<decltype(lv)>>(v)

typedef unsigned char uint8;
static inline void WBUFPOS(uint8* p, unsigned short pos, short x, short y, unsigned char dir) {
	p += pos;
	p[0] = (uint8)(x>>2);
	p[1] = (uint8)((x<<6) | ((y>>4)&0x3f));
	p[2] = (uint8)((y<<4) | (dir&0xf));
}
static inline void WBUFPOS2(uint8* p, unsigned short pos, short x0, short y0, short x1, short y1, uint8 sx0, uint8 sy0) {
	p += pos;
	p[0] = (uint8)(x0>>2);
	p[1] = (uint8)((x0<<6) | ((y0>>4)&0x3f));
	p[2] = (uint8)((y0<<4) | ((x1>>6)&0x0f));
	p[3] = (uint8)((x1<<2) | ((y1>>8)&0x03));
	p[4] = (uint8)y1;
	p[5] = (uint8)((sx0<<4) | (sy0&0x0f));
}
'''


def signed(v, bits):
    v &= (1 << bits) - 1
    return v - (1 << bits) if v >> (bits - 1) else v


class Gen:
    def __init__(self, pkt):
        self.p = pkt
        self.v = pkt["versions"][0]
        self.meta = pkt["_meta"]
        self.rng = random.Random(zlib.crc32(pkt["name"].encode()))
        self.code = []
        self.vals = []   # (row or None, field, kind, text)

    def num(self, ty):
        bits = BITS[ty]
        raw = self.rng.getrandbits(bits)
        if self.rng.random() < 0.15:
            raw = self.rng.choice([0, 1, (1 << bits) - 1, 1 << (bits - 1)])
        return raw

    def lit(self, raw, ty):
        bits = BITS[ty]
        if ty in SIGNED:
            return f"{signed(raw, bits)}LL"
        return f"0x{raw:x}ULL"

    def assign(self, base, f, row):
        name, ty = f["name"], f["type"]
        path = base + name
        if f.get("bit_width"):
            raw = self.rng.getrandbits(f["bit_width"])
            self.code.append(f"  NP_SET({path}, {raw});")
            self.vals.append((row, name, "i", str(raw)))
        elif ty in BITS:
            n = f.get("array_len", 1)
            if n == 1:
                raw = self.rng.getrandbits(1) if f.get("_bool") else self.num(ty)
                self.code.append(f"  NP_SET({path}, {self.lit(raw, ty)});")
                self.vals.append((row, name, "i", str(signed(raw, 64) if ty == "u64" else (signed(raw, BITS[ty]) if ty in SIGNED else raw))))
            elif BITS[ty] == 8:
                bs = [self.rng.getrandbits(8) for _ in range(n)]
                for i, b in enumerate(bs):
                    self.code.append(f"  {path}[{i}] = {b};")
                self.vals.append((row, name, "r", bytes(bs).hex()))
            else:
                vs = [self.num(ty) for _ in range(n)]
                for i, raw in enumerate(vs):
                    self.code.append(f"  NP_SET({path}[{i}], {self.lit(raw, ty)});")
                self.vals.append((row, name, "a", f"{n} " + " ".join(str(signed(r, 64) if ty == 'u64' else (signed(r, BITS[ty]) if ty in SIGNED else r)) for r in vs)))
        elif ty == "f32":
            x = self.rng.randint(-4000, 4000) / 8.0
            import struct
            bits = struct.unpack("<I", struct.pack("<f", x))[0]
            self.code.append(f"  {path} = {x!r}f;")
            self.vals.append((row, name, "f", f"{bits:08x}"))
        elif ty == "string":
            w = f["width"]
            n = self.rng.randint(0, min(w - 1, 12))
            s = "".join(self.rng.choice("abcdefghijklmnopqrstuvwxyz0123456789_ -") for _ in range(n))
            if s:
                self.code.append(f'  memcpy({path}, "{s}", {len(s)});')
            self.vals.append((row, name, "s", s.encode().hex() or "-"))
        elif ty == "pos3":
            x, y, d = self.rng.randrange(1024), self.rng.randrange(1024), self.rng.randrange(16)
            self.code.append(f"  WBUFPOS((uint8*){path}, 0, {x}, {y}, {d});")
            self.vals.append((row, name, "p3", f"{x} {y} {d}"))
        elif ty == "pos6":
            a = [self.rng.randrange(1024) for _ in range(4)] + [self.rng.randrange(16), self.rng.randrange(16)]
            self.code.append(f"  WBUFPOS2((uint8*){path}, 0, {a[0]}, {a[1]}, {a[2]}, {a[3]}, {a[4]}, {a[5]});")
            self.vals.append((row, name, "p6", " ".join(map(str, a))))
        else:
            raise SystemExit(f"{self.p['name']}.{name}: oracle cannot assign type {ty}")

    def generate(self, ident):
        m, v = self.meta, self.v
        S = m["struct"]
        fields = v["fields"]
        head = [f for f in fields if not f.get("row") and not (f["type"] in ("string", "raw") and not f.get("width"))]
        row = [f for f in fields if f.get("row")]
        tail = [f for f in fields if f["type"] in ("string", "raw") and not f.get("width") and not f.get("row")]
        ln = v["length"]
        variable = "variable" in ln
        size = m["size"] if not variable else ln["variable"]["header"]
        rows = 0
        tail_bytes = b""
        if variable:
            rep = ln["variable"].get("repeat")
            if rep:
                rows = ROWS
                size = rep["offset"] + rows * rep["stride"]
            elif tail:
                t = tail[0]
                if t["type"] == "string":
                    n = self.rng.randint(0, TAIL_TEXT_MAX)
                    txt = "".join(self.rng.choice("abcdefghijklmnopqrstuvwxyz ") for _ in range(n))
                    tail_bytes = txt.encode() + b"\0"
                    self.vals.append((None, t["name"], "s", txt.encode().hex() or "-"))
                else:
                    n = self.rng.randint(0, TAIL_TEXT_MAX)
                    tail_bytes = bytes(self.rng.getrandbits(8) for _ in range(n))
                    self.vals.append((None, t["name"], "r", tail_bytes.hex() or "-"))
                size += len(tail_bytes)
        c = self.code
        c.append(f"static void pk_{ident}() {{")
        c.append(f"  static uint8_t buf[{size + 16}] __attribute__((aligned(8)));")
        c.append(f"  memset(buf, 0, sizeof buf);")
        c.append(f"  {S}* p = reinterpret_cast<{S}*>(buf);")
        c.append(f"  p->{m['type_member']} = {m['header_const']};")
        if variable:
            c.append(f"  p->{m['length_member']} = {size};")
        for f in head:
            self.assign("p->", f, None)
        if tail_bytes:
            init = ", ".join("0x%02x" % b for b in tail_bytes)
            c.append(f"  {{ static const uint8_t tb[] = {{{init}}}; memcpy(reinterpret_cast<uint8_t*>(p->{m['flex']}), tb, sizeof tb); }}")
        flex = m["flex"]
        for i in range(rows):
            for f in row:
                base = f"p->{flex}[{i}]" + ("" if f["name"] == flex else ".")
                if f["name"] == flex:
                    g = dict(f)
                    g["name"] = ""
                    self.assign(base, g, i)
                    self.vals[-1] = (i, flex, self.vals[-1][2], self.vals[-1][3])
                else:
                    self.assign(base, f, i)
        c.append(f'  printf("S {self.p["name"]} %zu\\n", sizeof({S}));')
        for f in head:
            if f.get("bit_width") or any(ch in f["name"] for ch in ".[") or f["type"] == "pos6" and False:
                continue
            c.append(f'  printf("O {self.p["name"]} {f["name"]} %zu\\n", offsetof({S}, {f["name"]}));')
        c.append(f'  printf("G {self.p["name"]} {size} ");')
        c.append(f"  for (size_t i = 0; i < {size}; ++i) printf(\"%02x\", buf[i]);")
        c.append('  printf("\\n");')
        c.append("}")
        return rows, size


def build_harness(cat, scope, ident_map):
    gens = []
    for p in cat["packets"]:
        if "_meta" in p and p["_meta"]["scope"] == scope:
            gens.append(p)
    body, calls = [], []
    out_meta = {}
    for i, p in enumerate(gens):
        g = Gen(p)
        rows, size = g.generate(i)
        body += g.code
        calls.append(f"  pk_{i}();")
        out_meta[p["name"]] = (p, g.vals, rows, size)
    inc = "#include <map/packets.hpp>\n#include <map/clif.hpp>\n" if scope == "map" else "#include <common/packets.hpp>\n"
    src = ("#include <cstdio>\n#include <cstdint>\n#include <cstring>\n#include <cstddef>\n" + inc + LEGACY_POS.replace("typedef unsigned char uint8;\n", "") +
           "\n".join(body) + "\nint main() {\n" + "\n".join(calls) + "\n  return 0;\n}\n")
    return src, out_meta


def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, **kw)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--legacy-root", required=True)
    ap.add_argument("--packetver", type=int, default=20211103)
    ap.add_argument("--overlay", default=str(HERE.parent.parent / "proto/client_packets.yaml"))
    ap.add_argument("--out")
    ap.add_argument("--check", metavar="FILE", help="exit 1 unless FILE equals what the compiled legacy structs produce now")
    ap.add_argument("--work-dir", required=True)
    ap.add_argument("--cxx", default="g++")
    a = ap.parse_args(argv)
    root = Path(a.legacy_root).resolve()
    work = Path(a.work_dir).resolve()
    work.mkdir(parents=True, exist_ok=True)
    cat = dc.build(root, a.packetver, Path(a.overlay))
    r3 = root / "3rdparty/rapidyaml"
    flags = ["-std=c++20", "-w", f"-DPACKETVER={a.packetver}", f"-I{root / 'src'}", f"-I{r3}/src", f"-I{r3}/ext/c4core/src",
             f"-I{r3}/ext/c4core/src/c4/ext", f"-I{root / '3rdparty/libconfig'}", "-O0"]
    result, sizes, offsets = {}, {}, {}
    for scope in ("map", "common"):
        src, meta = build_harness(cat, scope, None)
        (work / f"oracle_{scope}.cpp").write_text(src)
        exe = work / f"oracle_{scope}.bin"
        cc = run([a.cxx, *flags, str(work / f"oracle_{scope}.cpp"), "-o", str(exe)])
        if cc.returncode:
            print(cc.stderr[-6000:], file=sys.stderr)
            return 3
        out = run([str(exe)])
        if out.returncode:
            print(f"harness {scope} failed rc={out.returncode}", file=sys.stderr)
            return 3
        for ln in out.stdout.splitlines():
            t = ln.split()
            if t[0] == "G":
                result[t[1]] = (meta[t[1]], int(t[2]), t[3])
            elif t[0] == "S":
                sizes[t[1]] = int(t[2])
            elif t[0] == "O":
                offsets[(t[1], t[2])] = int(t[3])
    # sizeof / offsetof from the compiler against the catalog's geometry
    problems = []
    n_sizes = n_offsets = 0
    for p in cat["packets"]:
        if p["name"] not in sizes:
            continue
        v = p["versions"][0]
        ln = v["length"]
        want = ln["fixed"] if "fixed" in ln else ln["variable"]["header"]
        if "variable" in ln and ln["variable"].get("repeat"):
            # sizeof of a struct with a max-size array of rows (itemlist) = header + count*stride
            want = None
        n_sizes += 1
        if want is not None and not (sizes[p["name"]] == want or v.get("legacy_db_length") is not None) and not (
                "variable" in ln and sizes[p["name"]] >= want):
            problems.append(f"sizeof({p['_meta']['struct']}) = {sizes[p['name']]}, catalog says {want}")
        for f in v["fields"]:
            key = (p["name"], f["name"])
            if key in offsets and not f.get("row"):
                n_offsets += 1
                if offsets[key] != f["offset"]:
                    problems.append(f"offsetof({p['_meta']['struct']}, {f['name']}) = {offsets[key]}, catalog says {f['offset']}")
    if problems:
        print("\n".join(problems[:30]), file=sys.stderr)
        return 3
    print(f"sizeof compared for {n_sizes} structs, offsetof for {n_offsets} members: 0 disagreements")
    lines = ["# GENERATED by tools/legacy/layout_oracle.py: bytes written by the compiled legacy structs (g++ on",
             "# src/map/packets.hpp and src/common/packets.hpp, PACKETVER %d). DO NOT EDIT." % a.packetver]
    for p in cat["packets"]:
        if p["name"] not in result:
            continue
        (pk, vals, rows, _), size, hexs = result[p["name"]]
        lines.append(f"P {p['name']} {p['versions'][0]['opcode']} {size} {rows}")
        lines.append(f"B {hexs}")
        for row, name, kind, text in vals:
            if row is None:
                lines.append(f"V {name} {kind} {text}")
            else:
                lines.append(f"R {row} {name} {kind} {text}")
        lines.append("E")
    text = "\n".join(lines) + "\n"
    if a.out:
        Path(a.out).write_text(text)
    print(f"{len(result)} packets compiled and run" + (f"; wrote {a.out}" if a.out else ""))
    if a.check and Path(a.check).read_text() != text:
        print(f"error: {a.check} differs from what the compiled legacy structs produce", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
