#!/usr/bin/env python3
"""Size/time test of the lazy loader over the renewal load set (835 files, ~720k lines) against the eager prototype pipeline.

  python3 -I tests/memory_test.py --repo <rathena checkout> --luau <luau cli> [--set re|pre] [--session 200]
  python3 -I tests/memory_test.py --from-env       # LNPC_LUAU_CLI, LNPC_REPO (default: this checkout); exit 77 (skip) when not set

Each mode runs in its own Luau process (tools/corpus_bench.luau): eager (parse + lower + keep every body, the M0 prototype), lazy (index
only), session (lazy + N random NPCs used), as a delta of collectgarbage("count") over a state where all file texts are resident.
Asserts: lazy index <= 20% of the eager programs, the session <= 35%, and the index of the whole set takes less than 60 s."""
import argparse, os, re, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
UNIT = os.path.dirname(HERE)

ap = argparse.ArgumentParser()
ap.add_argument('--repo', default='')
ap.add_argument('--luau', default='')
ap.add_argument('--set', default='re', choices=['re', 'pre'])
ap.add_argument('--session', type=int, default=200)
ap.add_argument('--from-env', action='store_true')
ap.add_argument('--keep', action='store_true')
args = ap.parse_args()
if args.from_env:
    args.luau = args.luau or os.environ.get('LNPC_LUAU_CLI', '')
    args.repo = args.repo or os.environ.get('LNPC_REPO', os.path.abspath(os.path.join(UNIT, '..', '..', '..')))
    if not args.luau or not os.path.exists(args.luau) or not os.path.isdir(os.path.join(args.repo, 'npc')):
        print('SKIP: set LNPC_LUAU_CLI (and optionally LNPC_REPO)'); sys.exit(77)

work = tempfile.mkdtemp(prefix='lnpc-bench-', dir=os.environ.get('LNPC_SCRATCH') or None)
try:
    subprocess.run([sys.executable, '-I', os.path.join(UNIT, 'tools', 'gen_corpus_modules.py'), '--repo', args.repo, '--set', args.set, '--out', work], check=True)
    os.makedirs(os.path.join(work, 'pkg'))
    for f in os.listdir(os.path.join(UNIT, 'luau')):  # (the Luau CLI does not follow a symlinked directory in require paths)
        if f.endswith('.luau'): shutil.copy(os.path.join(UNIT, 'luau', f), os.path.join(work, 'pkg', f))
    shutil.copy(os.path.join(UNIT, 'tools', 'corpus_bench.luau'), work)
    res = {}
    for mode in ('eager', 'lazy', 'session'):
        cmd = [args.luau, 'corpus_bench.luau', '-a', mode] + ([str(args.session)] if mode == 'session' else [])
        p = subprocess.run(cmd, cwd=work, capture_output=True, text=True, timeout=3600)
        line = [l for l in p.stdout.splitlines() if l.startswith('@bench')]
        if p.returncode != 0 or not line:
            print('%s failed:\n%s\n%s' % (mode, p.stdout[-2000:], p.stderr[-2000:])); sys.exit(1)
        res[mode] = dict(kv.split('=', 1) for kv in line[0].split()[1:])
        res[mode] = {k: (float(v) if re.fullmatch(r'-?[\d.]+', v) else v) for k, v in res[mode].items()}
    p = subprocess.run([args.luau, 'corpus_bench.luau', '-a', 'labels'], cwd=work, capture_output=True, text=True, timeout=3600)
    line = [l for l in p.stdout.splitlines() if l.startswith('@bench')]
    if p.returncode != 0 or not line:
        print('labels failed:\n%s\n%s' % (p.stdout[-2000:], p.stderr[-2000:])); sys.exit(1)
    lab = {k: v for k, v in (kv.split('=', 1) for kv in line[0].split()[1:])}
    e, l, s = res['eager'], res['lazy'], res['session']
    print('set=%s text=%.1f MB (resident in every mode, not part of the deltas)' % (args.set, e['textMB']))
    print('%-24s %10s %10s' % ('mode', 'delta MB', 'time s'))
    print('%-24s %10.1f %10.2f   (%d bodies, %d failed, as the M0 prototype)' % ('eager (all lowered)', e['delta'], e['time'], e['bodies'], e['failed']))
    print('%-24s %10.1f %10.2f   (%d NPC entries, %d functions)' % ('lazy index', l['delta'], l['indexTime'], l['npcs'], l['functions']))
    print('%-24s %10.1f %10.2f   (%d NPCs used, %d parses)' % ('lazy + session', s['delta'], s['sessionTime'], s['sessionNpcs'], s['parsed']))
    print('lazy index / eager = %.1f%%, session / eager = %.1f%%' % (100 * l['delta'] / e['delta'], 100 * s['delta'] / e['delta']))
    print('event labels: text scan vs lowered labels: %s script NPCs checked, %s mismatches' % (lab['checked'].split('.')[0], lab['mismatches'].split('.')[0]))
    for l2 in p.stdout.splitlines():
        if l2.startswith('@labels-sample') and l2.strip() != '@labels-sample': print('  ' + l2)
    bad = []
    if float(lab['mismatches']) != 0: bad.append('the label scan disagrees with the parser for %s NPCs' % lab['mismatches'])
    if l['delta'] > 0.20 * e['delta']: bad.append('lazy index above 20%% of eager (%.1f vs %.1f MB)' % (l['delta'], e['delta']))
    if s['delta'] > 0.35 * e['delta']: bad.append('session above 35%% of eager (%.1f vs %.1f MB)' % (s['delta'], e['delta']))
    if l['indexTime'] > 60: bad.append('index took %.1f s' % l['indexTime'])
    if bad: print('FAILED: ' + '; '.join(bad)); sys.exit(1)
    print('memory test passed')
finally:
    if not args.keep: shutil.rmtree(work, ignore_errors=True)
