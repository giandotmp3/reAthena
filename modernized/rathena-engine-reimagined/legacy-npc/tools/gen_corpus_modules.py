#!/usr/bin/env python3
"""Wraps the NPC files of one load set as Luau data modules so that the Luau CLI (which has no io library) can read them.

  python3 -I tools/gen_corpus_modules.py --repo <rathena checkout> --set re|pre --out <dir>

Walks npc/<re|pre-re>/scripts_main.conf (npc:/import: lines, as the legacy server does) and writes
  <dir>/data/f0000.luau ...   `return {[==[ <file text, CRLF normalised> ]==]}`
  <dir>/manifest.luau         { files = { {path=, mod=, bytes=}, ... }, order = {1,2,...} }   (load order of the set)
Used by memory_test.py and unsupported_report.py. Reads the repository, writes only below --out."""
import argparse, os, re, sys

ap = argparse.ArgumentParser()
ap.add_argument('--repo', required=True)
ap.add_argument('--set', choices=['re', 'pre'], required=True)
ap.add_argument('--out', required=True)
args = ap.parse_args()

def walk(conf, out, depth=0):
    if depth > 32: raise SystemExit('import loop at ' + conf)
    with open(os.path.join(args.repo, conf), 'rb') as f:
        for raw in f.read().decode('latin-1').splitlines():
            line = raw.split('//', 1)[0].strip()
            m = re.match(r'^(npc|import|delnpc)\s*:\s*(\S.*)$', line)
            if not m: continue
            k, v = m.group(1), m.group(2).strip()
            if k == 'npc': out.append(v)
            elif k == 'import': walk(v, out, depth + 1)

conf = 'npc/re/scripts_main.conf' if args.set == 're' else 'npc/pre-re/scripts_main.conf'
files = []
walk(conf, files)
os.makedirs(os.path.join(args.out, 'data'), exist_ok=True)
man = ['return {\nfiles = {\n']
n = 0
for i, f in enumerate(files):
    p = os.path.join(args.repo, f)
    if not os.path.exists(p): continue
    data = open(p, 'rb').read()
    data = data.replace(b'\r\n', b'\n').replace(b'\r', b'\n')
    lvl = 3
    while (b']' + b'=' * lvl + b']') in data: lvl += 1
    with open(os.path.join(args.out, 'data', 'f%04d.luau' % i), 'wb') as o:
        o.write(b'return {[' + b'=' * lvl + b'[\n' + data + b']' + b'=' * lvl + b']}\n')
    man.append('{path="%s",mod="f%04d",bytes=%d},\n' % (f, i, len(data)))
    n += 1
man.append('}}\n')
open(os.path.join(args.out, 'manifest.luau'), 'w').write(''.join(man))
print('%s set: %d files written to %s' % (args.set, n, args.out))
