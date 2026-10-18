#!/usr/bin/env python3
"""Replaces the synthetic ids of luau/consts.luau by the values the real rAthena script VM has.

  python3 -I tools/merge_consts.py --dump <legacy_consts.tsv> [--consts luau/consts.luau]

tooling/m0/legacy_interp/prep.py reads the script constants (export_constant(...)) from src/map/script_constants.hpp and cannot see the value of enum and
#define constants, so it numbers those from 100000. The replay harness (tooling/m0/replay, RA_REPLAY_DUMP_CONSTS) makes the built legacy map-server write every
constant it registered as `NAME<TAB>value`; this tool merges that dump into the table (same rule as run_replay.py merge_constants: the VM's value wins, names the
VM has and the table lacks are added). Item AegisNames (ids from db/*/item_db*.yml) are not in the dump and keep their value."""
import argparse, re

ap = argparse.ArgumentParser()
ap.add_argument('--dump', required=True)
ap.add_argument('--consts', default='luau/consts.luau')
args = ap.parse_args()
txt = open(args.consts).read()
head, _, tail = txt.partition('},params={')
cons = {}
for m in re.finditer(r'^\["([^"]+)"\]=(-?\d+),$', head, re.M): cons[m.group(1)] = int(m.group(2))
changed = added = 0
for line in open(args.dump, encoding='latin-1'):
    if '\t' not in line: continue
    k, v = line.rstrip('\n').split('\t'); k = k.lower(); v = int(v)
    if k in cons:
        if cons[k] != v: changed += 1
    else: added += 1
    cons[k] = v
with open(args.consts, 'w') as f:
    f.write('return {consts={\n' + ''.join('["%s"]=%d,\n' % (k, v) for k, v in sorted(cons.items())) + '},params={' + tail)
print('constants: %d in the dump; %d synthetic ids replaced, %d added; %d total' % (sum(1 for _ in open(args.dump, encoding='latin-1')), changed, added, len(cons)))
