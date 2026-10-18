#!/usr/bin/env python3
"""Classify every failing case of a replay run by the ROOT (first) divergence into one of five classes:

  interpreter bug         legacy_interp computes something the legacy VM does not (control flow, evaluation, a command it implements wrongly)
  missing world state     the value/behaviour depends on game data or server state the harness does not provide (rules in WORLD below)
  command coverage        legacy_interp has no implementation of the command (soft stub '!U')
  harness artifact        the difference comes from the harness (hook or trace format), not from either VM's script semantics
  documented deviation    a difference we decided to keep (DEVIATIONS below; each has a reason, see replay/README.md)

  python3 -I attribute.py --out <build>/out --name <report-name> [--title TEXT]      (name '' = the default 'results.json')

Writes <out>/attribution[-<name>].txt and prints it. The tables are deliberately short and explicit: a divergence is a bug
unless a rule says otherwise."""
import os, re, sys, json, argparse, collections

ap = argparse.ArgumentParser()
ap.add_argument('--out', required=True)
ap.add_argument('--name', default='')
ap.add_argument('--title', default='')
ap.add_argument('--examples', type=int, default=3)
args = ap.parse_args()

tag = '-' + args.name if args.name else ''
res = [r for r in json.load(open(os.path.join(args.out, 'results%s.json' % tag)))['cases'] if not r.get('skipped')]

# (kind, command) pairs attributed to world state: the legacy value comes from data/state the Luau side does not model.
# command-level entries match every kind that carries the command; (kind, '*') matches a state key class.
WORLD = {
    # dynamic server state
    'mobcount', 'getmapusers', 'getareausers', 'agitcheck', 'agitcheck2', 'agitcheck3', 'getnpcid', 'checkvending', 'checkchatting',
    'getpartymember', 'getguildmember', 'getmapguildusers',
    # game data tables (db/*.yml) without a provider
    'getiteminfo', 'getmonsterinfo', 'getskilllv', 'jobname', 'getrefine', 'getbrokenid', 'isequipped', 'getequipcardcnt',
    'getgroupitem', 'getpetinfo', 'gethominfo', 'getmercinfo', 'checkcell', 'getmapflag', 'getunitdata', 'getskillname',
}
# documented deviations: (kind, command or '*', short reason)
DEVIATIONS = [
]

def classify(r):
    root = r['root']
    k, cmd = root['kind'], root['cmd']
    if k == 'missing_command':
        return 'command coverage'
    if k in ('state_value', 'state_missing', 'state_extra') and (re.match(r"^'", str(cmd)) or cmd == '$@mobid'):
        return 'missing world state'   # instance variables need a running instance; $@mobid holds runtime object ids of spawned monsters
    for dk, dc, why in DEVIATIONS:
        if k == dk and (dc == '*' or dc == cmd):
            return 'documented deviation'
    if k in ('ret_value', 'ret_format', 'arg_value', 'arg_format', 'legacy_failure', 'luau_failure') and cmd in WORLD:
        return 'missing world state'
    return 'interpreter bug'

rows = collections.defaultdict(list)
for r in res:
    if r['strict_pass']:
        continue
    rows[classify(r)].append(r)

n = len(res)
sp = sum(1 for r in res if r['strict_pass'])
out = []
P = out.append
P('%sATTRIBUTION of failing cases (root divergence): %d cases, %d strict passes, %d failing' % (args.title + ': ' if args.title else '', n, sp, n - sp))
for cls in ('interpreter bug', 'missing world state', 'command coverage', 'harness artifact', 'documented deviation'):
    lst = rows.get(cls, [])
    P('  %-22s %4d cases, %3d NPCs' % (cls, len(lst), len(set(r['npc'] for r in lst))))
P('')
for cls in ('interpreter bug', 'missing world state', 'command coverage', 'harness artifact', 'documented deviation'):
    lst = rows.get(cls, [])
    if not lst:
        continue
    P('== %s (%d)' % (cls, len(lst)))
    by = collections.defaultdict(list)
    for r in lst:
        by[(r['root']['kind'], r['root']['cmd'])].append(r)
    for (k, cmd), g in sorted(by.items(), key=lambda x: -len(x[1])):
        P('  %3d  %-16s %-22s e.g. %s' % (len(g), k, cmd, ' '.join(x['id'] for x in g[:6])))
    P('  examples:')
    seen = set()
    shown = 0
    for r in lst:
        key = (r['npc'], r['root']['cmd'])
        if key in seen:
            continue
        seen.add(key)
        P('    [%s] %s:%s NPC "%s"' % (r['id'], r['file'], r['line'], r['npc']))
        if r['root'].get('legacy'): P('        legacy: %s' % r['root']['legacy'][:220])
        if r['root'].get('luau'): P('        luau  : %s' % r['root']['luau'][:220])
        if not r['root'].get('legacy') and not r['root'].get('luau'): P('        %s' % r['root']['detail'][:220])
        shown += 1
        if shown >= args.examples:
            break
    P('')
txt = '\n'.join(out) + '\n'
open(os.path.join(args.out, 'attribution%s.txt' % tag), 'w').write(txt)
print(txt)
