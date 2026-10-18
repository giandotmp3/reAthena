#!/usr/bin/env python3
"""Attribute every failing case to a cause: interpreter bug | missing world state | command coverage | unattributed.

Uses the two Luau modes: a case that passes once the && || ?: operands are all evaluated (mode nosc) was failing because of that
interpreter semantic difference alone. For the remaining cases the first divergence of the nosc run is attributed by rule.

  python3 -I attribute.py --out <build>/out [--repo /home/user/reAthena]
"""
import os, re, sys, json, argparse, collections
ap = argparse.ArgumentParser()
ap.add_argument('--out', required=True)
ap.add_argument('--repo', default=os.environ.get('RATHENA_REPO', '/home/user/reAthena'))
args = ap.parse_args()

# commands whose legacy_interp implementation is a constant stub / absent world model (items, equipment, mobs, shops, positions, castles)
WORLD_STUBS = set('''getitemname getiteminfo getequipid getequipname getequipisequiped getequipisenableref getequipisidentify getequiprefinerycnt
getequipweaponlv getequippercentrefinery mobcount callshop getmapxy getcastledata instance_npcname instance_mapname instance_id
countitem countitem2 checkweight checkweight2 getmonsterinfo gettime getgmlevel getskilllv isequipped getpartnerid checkquest
logmes strnpcinfo strcharinfo getcharid getnpcid agitcheck agitcheck2 getguildname getguildmaster getpartyname readparam rand'''.split())

def switch_prelude(path, npc):
    """does the NPC have statements between `switch (...) {` and its first case? (legacy executes them, legacy_interp skips them)"""
    try:
        text = open(os.path.join(args.repo, path), 'rb').read().decode('latin-1')
    except OSError:
        return False
    i = text.find('\tscript\t' + npc + '\t')
    if i < 0: return False
    j = text.find('\n}\n', i)
    body = text[i:j if j > 0 else len(text)]
    return re.search(r'switch\s*\([^\n]*\)\s*\{[ \t]*\n(?:[ \t]*\n)*[ \t]*(?!case\b|default\b|//|\})[A-Za-z_]', body) is not None

cases = {c['id']: c for c in json.load(open(os.path.join(args.out, 'results.json')))['cases']}
nosc = {c['id']: c for c in json.load(open(os.path.join(args.out, 'results-nosc.json')))['cases']}
rows = collections.defaultdict(list)
for cid, c in cases.items():
    n = nosc[cid]
    if c['strict_pass']: cat = 'PASS'
    elif n['strict_pass']: cat = 'interpreter: && || ?: short-circuit (legacy evaluates every operand)'
    else:
        r = n['root'] or {}
        k, cmd = r.get('kind'), r.get('cmd')
        if k == 'missing_command': cat = 'coverage: command not implemented in legacy_interp (%s)' % cmd
        elif k == 'legacy_failure': cat = 'world: legacy command failed, needs real state (%s)' % cmd
        elif k == 'state_extra' and str(cmd).startswith("'"): cat = "world: instance variable ' needs a running instance"
        elif k == 'missing_in_luau' and switch_prelude(c['file'], c['npc']): cat = 'interpreter: statements between `switch (...) {` and the first `case` are skipped'
        elif k in ('ret_value', 'ret_format', 'arg_value', 'arg_format') and cmd in WORLD_STUBS: cat = 'world: no model / constant stub in harness (%s)' % cmd
        else: cat = 'unattributed (%s %s)' % (k, cmd)
    rows[cat].append(cid)
out = []
tot = len(cases)
out.append('ATTRIBUTION of %d cases (NPC x variant)' % tot)
for cat, ids in sorted(rows.items(), key=lambda x: (-len(x[1]), x[0])):
    out.append('  %4d  %-95s %s' % (len(ids), cat, ' '.join(ids[:8]) + (' ...' if len(ids) > 8 else '')))
groups = collections.Counter()
for cat, ids in rows.items():
    g = cat.split(':')[0] if ':' in cat else cat.split(' ')[0]
    groups[g] += len(ids)
out.append('')
out.append('by group: ' + ', '.join('%s=%d' % kv for kv in groups.most_common()))
interp_fail = sum(len(v) for k, v in rows.items() if k.startswith('interpreter'))
world_fail = sum(len(v) for k, v in rows.items() if k.startswith('world') or k.startswith('coverage'))
out.append('failing cases explained by interpreter semantics: %d; by missing world state / coverage: %d; unattributed: %d' % (
    interp_fail, world_fail, sum(len(v) for k, v in rows.items() if k.startswith('unattributed'))))
npcs = collections.defaultdict(set)
for cat, ids in rows.items():
    for i in ids: npcs[cat].add(cases[i]['npc'])
txt = '\n'.join(out) + '\n'
open(os.path.join(args.out, 'attribution.txt'), 'w').write(txt)
print(txt)
