#!/usr/bin/env python3
"""The C++ YAML-subset reader against PyYAML on every proto file (run by ctest as `yaml_differential`).

  python3 -I tests/test_yaml_differential.py <netproto_yaml_dump> <proto-dir>

netproto_yaml_dump prints canonical JSON per file; yaml.safe_load must give the same document. Also a
set of small documents covering the quoting, flow and block forms the reader promises.
"""
import json
import subprocess
import sys
import tempfile
from pathlib import Path

import yaml

DOCS = [
    "a: 1\nb: [1, 2, {c: d}]\ne:\n  - x\n  - y: 1\n    z: [ ]\n",
    "k: \"quoted: with colon # and hash\"\nq: 'single ''quoted'''\nn: null\nt: true\nh: 0xFF\nneg: -12\n",
    "top:\n- 1\n- 2\nnext: { a: 1, b: [x, y] }\n",
    "- name: a\n  v: 1\n- name: b\n  v: 2\n",
    "s: \"esc \\\\ \\\" \\n \\t end\"\n",
]


def main():
    exe, proto = sys.argv[1], Path(sys.argv[2])
    files = sorted(proto.glob("*.yaml"))
    if not files:
        print("no proto files", file=sys.stderr)
        return 1
    tmp = Path(tempfile.mkdtemp())
    for i, d in enumerate(DOCS):
        (tmp / f"doc{i}.yaml").write_text(d)
    files += sorted(tmp.glob("*.yaml"))
    out = subprocess.run([exe, *map(str, files)], capture_output=True, text=True)
    if out.returncode:
        print(out.stderr, file=sys.stderr)
        return 1
    lines = out.stdout.splitlines()
    if len(lines) != len(files):
        print(f"expected {len(files)} lines, got {len(lines)}", file=sys.stderr)
        return 1
    bad = 0
    for f, line in zip(files, lines):
        want = yaml.safe_load(f.read_text())
        got = json.loads(line)
        if want != got:
            bad += 1
            print(f"DIFFERENT: {f.name}", file=sys.stderr)
    print(f"{len(files)} documents compared with PyYAML, {bad} differences")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
