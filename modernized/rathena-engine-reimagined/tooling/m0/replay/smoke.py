#!/usr/bin/env python3
"""Regression smoke test of legacy_interp against the real legacy VM (record/replay harness).

Re-runs a FIXED subset of 10 NPCs (30 cases: 3 answer/player variants each) on both sides and FAILS (exit 1) when the strict-pass
count drops below the recorded baseline (smoke_baseline.json), or when a case that passed at record time no longer passes.
Strict = traces (ordered host calls with evaluated args and returns) and end state identical, the same comparator as the full runs.

  python3 -I smoke.py --build <build-dir> --luau <luau-cli> [--build-pre <pre-build-dir>] [--jobs 4]
  python3 -I smoke.py ... --record        # (re)write smoke_baseline.json from the current result (only after a deliberate review)

The subset (corpus/smoke.json): 5 real renewal NPCs, 1 real pre-renewal NPC, 4 synthetic NPCs of corpus/synthetic.txt (Syn_switch,
Syn_expr, Syn_args, Syn_num). <build-dir> / <pre-build-dir> are build_legacy.sh outputs (the second one built with RA_PRERENEWAL=1);
without --build-pre the pre-renewal part is skipped and compared against its own baseline entry only if present.
Runs in about a minute on 4 cores (legacy VM start-up dominates: ~1 s per case)."""
import os, sys, json, time, argparse, subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
ap = argparse.ArgumentParser()
ap.add_argument('--build', required=True, help='renewal build dir (build_legacy.sh output)')
ap.add_argument('--build-pre', default='', help='pre-renewal build dir (RA_PRERENEWAL=1 build_legacy.sh output); optional')
ap.add_argument('--luau', required=True)
ap.add_argument('--jobs', type=int, default=4)
ap.add_argument('--interp', default='', help='legacy_interp sources to test (default: ../legacy_interp); e.g. an older copy to prove the test fails')
ap.add_argument('--record', action='store_true')
ap.add_argument('--baseline', default=os.path.join(HERE, 'smoke_baseline.json'))
args = ap.parse_args()

SMOKE = json.load(open(os.path.join(HERE, 'corpus', 'smoke.json')))
parts = []  # (key, build, run_replay args)
real = os.path.join(HERE, 'corpus', 'smoke.json')
parts.append(('re', args.build, ['--corpus', real, '--set', 're', '--prefix', 's']))
if args.build_pre:
    parts.append(('pre', args.build_pre, ['--corpus', real, '--set', 'pre', '--prefix', 's']))
parts.append(('synthetic', args.build, ['--synthetic', '--set', 're', '--only', ','.join(SMOKE['synthetic_ids'])]))

t0 = time.time()
result = {}
for key, build, extra in parts:
    cmd = [sys.executable, '-I', os.path.join(HERE, 'run_replay.py'), '--build', build, '--luau', args.luau, '--jobs', str(args.jobs),
           '--report-name', 'smoke-' + key] + (['--interp', args.interp] if args.interp else []) + extra
    p = subprocess.run(cmd, capture_output=True, text=True)
    res = os.path.join(build, 'out', 'results-smoke-%s.json' % key)
    if not os.path.exists(res):
        print('smoke part %s: run_replay failed\n%s\n%s' % (key, p.stdout[-1500:], p.stderr[-1500:]))
        sys.exit(2)
    cases = json.load(open(res))['cases']
    result[key] = dict(total=len(cases), strict=sum(1 for c in cases if c['strict_pass']),
                       passing=sorted(c['id'] for c in cases if c['strict_pass']),
                       failing=sorted(c['id'] for c in cases if not c['strict_pass']))
dt = time.time() - t0

if args.record:
    json.dump(dict(note='baseline of smoke.py: strict-pass counts per part; regenerate with smoke.py --record after a reviewed change',
                   parts=result), open(args.baseline, 'w'), indent=1)
    print('recorded baseline:', {k: '%d/%d' % (v['strict'], v['total']) for k, v in result.items()}, '(%.0fs)' % dt)
    sys.exit(0)

base = json.load(open(args.baseline))['parts']
bad = 0
for key, r in result.items():
    b = base.get(key)
    if b is None:
        print('%-10s %d/%d strict (no baseline entry)' % (key, r['strict'], r['total'])); continue
    lost = sorted(set(b['passing']) - set(r['passing']))
    ok = r['strict'] >= b['strict'] and not lost
    print('%-10s %d/%d strict (baseline %d/%d)%s  %s' % (key, r['strict'], r['total'], b['strict'], b['total'],
          ' regressed: ' + ','.join(lost) if lost else '', 'ok' if ok else 'FAIL'))
    bad += not ok
for key in base:
    if key not in result: print('%-10s skipped (baseline %d/%d)' % (key, base[key]['strict'], base[key]['total']))
print('smoke %s in %.0fs' % ('FAILED' if bad else 'passed', dt))
sys.exit(1 if bad else 0)
