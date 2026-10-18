#!/usr/bin/env python3
"""Glue: walk the npc conf import graph, emit data/*.luau modules (raw file text),
manifest.luau (load sets), builtins.luau (BUILDIN_DEF arg specs), consts.luau."""
import os, re, sys, glob
REPO = os.environ.get('RATHENA_REPO', '/home/user/reAthena')
OUT = os.path.dirname(os.path.abspath(__file__)) + '/data'
os.makedirs(OUT, exist_ok=True)

def walk(conf, out, seen_depth=0):
    with open(os.path.join(REPO, conf), 'rb') as f:
        for raw in f.read().decode('latin-1').splitlines():
            line = raw.split('//', 1)[0].strip()
            m = re.match(r'^(npc|import|delnpc)\s*:\s*(\S.*)$', line)
            if not m: continue
            k, v = m.group(1), m.group(2).strip()
            if k == 'npc': out.append(v)
            elif k == 'import': walk(v, out)
            # delnpc ignored (counted nowhere)

sets = {}
for name, conf in (('re', 'npc/re/scripts_main.conf'), ('pre', 'npc/pre-re/scripts_main.conf')):
    lst = []; walk(conf, lst); sets[name] = lst
allfiles = []
for s in sets.values():
    for f in s:
        if f not in allfiles: allfiles.append(f)
ids = {}
man = ['return {\nfiles = {\n']
bad = 0
for i, f in enumerate(allfiles):
    p = os.path.join(REPO, f)
    if not os.path.exists(p):
        man.append('{path=%r,mod=nil,missing=true},\n' % f); continue
    data = open(p, 'rb').read()
    if data.startswith(b'\xef\xbb\xbf'): bad += 1
    data = data.replace(b'\r\n', b'\n').replace(b'\r', b'\n')
    lvl = 3
    while (b']' + b'=' * lvl + b']') in data: lvl += 1
    with open('%s/f%04d.luau' % (OUT, i), 'wb') as o:
        o.write(b'return {[' + b'=' * lvl + b'[\n' + data + b']' + b'=' * lvl + b']}\n')
    ids[f] = i
    man.append('{path=%r,mod="f%04d",bytes=%d},\n' % (f, i, len(data)))
man.append('},\nsets = {\n')
for n, lst in sets.items():
    man.append('%s = {%s},\n' % (n, ','.join(str(allfiles.index(f) + 1) for f in lst)))
man.append('}}\n')
open(os.path.dirname(OUT) + '/manifest.luau', 'w').write(''.join(man).replace("'", '"'))
print({k: len(v) for k, v in sets.items()}, 'union', len(allfiles), 'bom', bad)

# builtins
src = open(REPO + '/src/map/script.cpp', encoding='latin-1').read()
b = {}
for m in re.finditer(r'BUILDIN_DEF(?:_DEPRECATED)?\(\s*(\w+)\s*,\s*"([^"]*)"', src):
    b[m.group(1).lower()] = m.group(2)
for m in re.finditer(r'BUILDIN_DEF2(?:_DEPRECATED)?\(\s*(\w+)\s*,\s*"([^"]*)"\s*,\s*"([^"]*)"', src):
    b[m.group(2).lower()] = m.group(3)
# the ones the define lines themselves match (macro defs) are harmless
lines = ['return {\n'] + ['["%s"]="%s",\n' % (k, v) for k, v in sorted(b.items())] + ['}\n']
open(os.path.dirname(OUT) + '/builtins.luau', 'w').write(''.join(lines))
print('builtins', len(b))

# constants
h = open(REPO + '/src/map/script_constants.hpp', encoding='latin-1').read()
consts = {}
idx = 100000
for m in re.finditer(r'export_(?:constant|deprecated_constant)\(\s*(\w+)\s*\)', h):
    consts[m.group(1).lower()] = idx; idx += 1
for m in re.finditer(r'export_constant_npc\(\s*JT_(\w+)\s*\)', h):
    consts[m.group(1).lower()] = idx; idx += 1
for m in re.finditer(r'export_(?:constant2|deprecated_constant2)\(\s*"(\w+)"\s*,\s*([^)]*)\)', h):
    v = m.group(2).strip()
    consts[m.group(1).lower()] = int(v, 0) if re.fullmatch(r'-?(0x[0-9a-fA-F]+|\d+)', v) else idx
    idx += 1
params = {}
for m in re.finditer(r'export_parameter\(\s*"(\w+)"\s*,\s*(\w+)\)', h):
    params[m.group(1).lower()] = m.group(2)
# item AegisName -> Id (both rulesets) ; mob AegisName
def yaml_ids(pat):
    n = 0
    for fn in glob.glob(REPO + pat):
        cur = None
        for l in open(fn, encoding='latin-1'):
            mm = re.match(r'\s*-?\s*Id:\s*(\d+)', l)
            if mm: cur = int(mm.group(1)); continue
            mm = re.match(r'\s*AegisName:\s*(\S+)', l)
            if mm and cur is not None:
                consts.setdefault(mm.group(1).lower(), cur); n += 1
    return n
ni = yaml_ids('/db/re/item_db*.yml') + yaml_ids('/db/pre-re/item_db*.yml')
lines = ['return {consts={\n'] + ['["%s"]=%d,\n' % (k, v) for k, v in sorted(consts.items())] + ['},params={\n']
lines += ['["%s"]=true,\n' % k for k in sorted(params)] + ['}}\n']
open(os.path.dirname(OUT) + '/consts.luau', 'w').write(''.join(lines))
print('consts', len(consts), 'params', len(params), 'item aegis', ni)
