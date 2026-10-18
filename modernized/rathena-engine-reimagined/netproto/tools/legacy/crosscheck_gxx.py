#!/usr/bin/env python3
"""Cross-check extract_packets.py against a real compiler.

The legacy headers are plain macros, so a real preprocessor can evaluate them:

  1. `g++ -E` over a tiny wrapper that redefines packetdb_addpacket() to a
     recording call, then `g++` compiles and runs it. The compiler evaluates every
     #if, HEADER_* constant, enum and sizeof() with no help from us.
  2. A probe generated from the extracted spec compares sizeof()/offsetof() of every
     laid-out struct and every HEADER_* id.
  3. The two evaluations are compared record by record (opcode, length, handler,
     offsets, source line) and, for the final table, opcode by opcode.

Any disagreement is a bug in the Python evaluation. Exit 0 only when they agree.
The legacy tree is read-only; everything is written under --work-dir.
"""
from __future__ import annotations

import argparse
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import extract_packets as ep  # noqa: E402

WRAPPER = r'''
#include <cstdio>
#include <cstddef>
#include <initializer_list>
#include <map/packets.hpp>
#include <map/clif.hpp>
static const char* np_file = "";
static void np_rec(int line, int cmd, int len, const char* fn, std::initializer_list<int> pos) {
  std::printf("R %s %d %d %d %s", np_file, line, cmd, len, fn);
  for (int p : pos) { std::printf(" %d", p); if (p == 0) break; }
  std::printf("\n");
}
#define packetdb_addpacket(cmd, len, func, ...) np_rec(__LINE__, (int)(cmd), (int)(len), #func, {__VA_ARGS__})
int main() {
  np_file = "map/clif_packetdb.hpp";
#include <map/clif_packetdb.hpp>
  np_file = "map/clif_shuffle.hpp";
#include <map/clif_shuffle.hpp>
#undef packetdb_addpacket
#include "np_probe.inc"
  return 0;
}
'''

# the wrapper's `packet`/`parseable_packet` come from clif_packetdb.hpp itself


def run(cmd, **kw):
    r = subprocess.run(cmd, capture_output=True, text=True, **kw)
    return r


def prepare_include_root(legacy_root: Path, work: Path, flavour_request: str, derived_by_config: str):
    """Real tree for a derived flavour; a patched COPY of src/ when the flavour is forced."""
    src = legacy_root / "src"
    if flavour_request in ("auto", derived_by_config):
        return src
    if flavour_request != "main":
        raise SystemExit("only a forced 'main' flavour has an oracle (copy with the RE auto-select disabled)")
    dst = work / "src_main"
    if dst.exists():
        shutil.rmtree(dst)
    shutil.copytree(src, dst, ignore=shutil.ignore_patterns("*.cpp", "*.o"))
    cfg = dst / "config/packets.hpp"
    t = cfg.read_text()
    new = t.replace("#if ( PACKETVER > 20151104 && PACKETVER < 20180704 ) || ( PACKETVER >= 20200902 && PACKETVER <= 20211118 )",
                    "#if 0 /* oracle copy: RE auto-select disabled */", 1)
    if new == t:
        raise SystemExit("could not patch the oracle copy of config/packets.hpp")
    cfg.write_text(new)
    return dst


def probe_inc(spec) -> str:
    """C++ that prints sizeof/offsetof for each laid-out struct and each HEADER id."""
    lines = []
    seen = set()

    def struct_probe(sname, fields, repeat=None):
        if sname in seen:
            return
        seen.add(sname)
        lines.append(f'std::printf("S {sname} %zu\\n", sizeof(struct {sname}));')
        for f in fields:
            lines.append(f'std::printf("O {sname} {f["name"]} %zu\\n", offsetof(struct {sname}, {f["name"]}));')
        if repeat:
            lines.append(f'std::printf("O {sname} {repeat["name"]} %zu\\n", offsetof(struct {sname}, {repeat["name"]}));')

    for e in spec["entries"]:
        if isinstance(e["fields"], list):
            struct_probe(e["struct"], e["fields"], e.get("repeat"))
            lines.append(f'std::printf("H {e["name"]} %d\\n", (int)HEADER_{e["name"]});')
    for e in spec["struct_only"]:
        if isinstance(e["fields"], list):
            struct_probe(e["struct"], e["fields"], e.get("repeat"))
            lines.append(f'std::printf("H {e["name"]} %d\\n", (int)HEADER_{e["name"]});')
    for sname, s in spec["structs"].items():
        if spec["_model"].structs[sname].synthetic:
            continue   # an anonymous member struct has no C++ name to take sizeof of
        struct_probe(sname, s["fields"])
    return "\n".join(lines) + "\n"


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--legacy-root", required=True)
    ap.add_argument("--packetver", type=int, default=20211103)
    ap.add_argument("--flavour", default="auto")
    ap.add_argument("--work-dir", required=True, help="scratch directory for the wrapper and binary")
    ap.add_argument("--cxx", default="g++")
    ap.add_argument("--report", help="write the JSON report here")
    a = ap.parse_args(argv)
    root = Path(a.legacy_root).resolve()
    work = Path(a.work_dir).resolve()
    work.mkdir(parents=True, exist_ok=True)

    spec = ep.build_spec(root, a.packetver, a.flavour)
    derived_cfg = ep.build_environment(root / "src", a.packetver, "auto")[1]
    inc_root = prepare_include_root(root, work, a.flavour, derived_cfg)

    (work / "np_probe.inc").write_text(probe_inc(spec))
    (work / "np_wrapper.cpp").write_text(WRAPPER)
    r3 = root / "3rdparty/rapidyaml"
    flags = ["-std=c++20", "-w", f"-DPACKETVER={a.packetver}", f"-I{work}", f"-I{inc_root}",
             f"-I{r3}/src", f"-I{r3}/ext/c4core/src", f"-I{r3}/ext/c4core/src/c4/ext"]

    # 1. g++ -E: the preprocessed text of the wrapper (independent evaluation of every #if)
    pre = run([a.cxx, "-E", "-P", *flags, str(work / "np_wrapper.cpp")])
    if pre.returncode:
        print(pre.stderr[-3000:], file=sys.stderr)
        return 3
    (work / "np_wrapper.ii").write_text(pre.stdout)
    e_calls = len(re.findall(r"\bnp_rec\(\s*\d+\s*,", pre.stdout)) - 0  # the definition has `np_rec(int line`
    # 2. compile + run
    exe = work / "np_wrapper.bin"
    cc = run([a.cxx, *flags, str(work / "np_wrapper.cpp"), "-o", str(exe)])
    if cc.returncode:
        print(cc.stderr[-4000:], file=sys.stderr)
        return 3
    out = run([str(exe)]).stdout.splitlines()

    recs, sizes, offs, hdrs = [], {}, {}, {}
    for ln in out:
        p = ln.split()
        if p[0] == "R":
            recs.append({"src": f"{p[1].replace('map/', 'map/')}:{p[2]}", "cmd": int(p[3]), "len": int(p[4]),
                         "handler": None if p[5] == "nullptr" else p[5], "pos": [int(x) for x in p[6:] if int(x) != 0]})
        elif p[0] == "S":
            sizes[p[1]] = int(p[2])
        elif p[0] == "O":
            offs[(p[1], p[2])] = int(p[3])
        elif p[0] == "H":
            hdrs[p[1]] = int(p[2])

    problems = []
    raw = spec["_raw"]
    # the recorded src for oracle lines is "<file>:<line>" with file as in clif_packetdb/shuffle
    if len(raw) != len(recs):
        problems.append(f"live definition count: python {len(raw)} vs compiler {len(recs)}")
    if len(recs) != e_calls:
        problems.append(f"g++ -E sees {e_calls} recording calls, compiled run printed {len(recs)}")
    for i, (r, o) in enumerate(zip(raw, recs)):
        mine = {"src": r["src"], "cmd": r["cmd"], "len": r["len"], "handler": r["handler"],
                "pos": r["pos"]}
        if mine != o:
            problems.append(f"record {i}: python {mine} vs compiler {o}")
            if len(problems) > 20:
                break
    # final table
    final = {}
    for o in recs:
        if 0 < o["cmd"] <= ep.MAX_PACKET_DB:
            final[o["cmd"]] = o
    mine_final = {e["opcode"]: e for e in spec["entries"]}
    if set(final) != set(mine_final):
        problems.append(f"final opcode sets differ: only python {sorted(set(mine_final) - set(final))[:10]}, "
                        f"only compiler {sorted(set(final) - set(mine_final))[:10]}")
    for c, o in final.items():
        e = mine_final.get(c)
        if e and (e["len"], e.get("handler"), e["pos"]) != (o["len"], o["handler"], o["pos"]):
            problems.append(f"final 0x{c:04x}: python {(e['len'], e.get('handler'), e['pos'])} vs compiler {(o['len'], o['handler'], o['pos'])}")

    # structs
    model = spec["_model"]
    checked_structs = checked_offsets = checked_headers = 0
    for sname, sz in sizes.items():
        st = model.structs[sname]
        checked_structs += 1
        if st.size != sz:
            problems.append(f"sizeof({sname}): python {st.size} vs compiler {sz}")
        for m in st.members:
            key = (sname, m.name)
            if key in offs:
                checked_offsets += 1
                if offs[key] != m.offset:
                    problems.append(f"offsetof({sname}.{m.name}): python {m.offset} vs compiler {offs[key]}")
    for nme, v in hdrs.items():
        checked_headers += 1
        if model.headers[nme] != v:
            problems.append(f"HEADER_{nme}: python {model.headers[nme]} vs compiler {v}")
    # struct-size mismatches reported by the extractor must be real
    for c, ln, sname, sz in spec["mismatches"]:
        if sname in sizes and sizes[sname] != sz:
            problems.append(f"mismatch entry {sname} disagrees with compiler")

    report = {
        "flavour": spec["flavour"], "packetver": a.packetver, "hypothetical": spec["hypothetical"],
        "live_definitions_python": len(raw), "live_definitions_compiler": len(recs),
        "gxx_E_recording_calls": e_calls,
        "final_opcodes_python": len(mine_final), "final_opcodes_compiler": len(final),
        "structs_compared": checked_structs, "offsets_compared": checked_offsets,
        "headers_compared": checked_headers, "disagreements": problems,
    }
    if a.report:
        Path(a.report).write_text(json.dumps(report, indent=1, sort_keys=True) + "\n")
    print(json.dumps(report, indent=1, sort_keys=True))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
