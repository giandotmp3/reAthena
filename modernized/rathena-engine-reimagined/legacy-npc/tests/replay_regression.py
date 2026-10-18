#!/usr/bin/env python3
"""Replay regression for the ported interpreter (task 7): holds legacy-npc/luau to the same strict-pass baseline as the prototype.

Runs tooling/m0/replay/smoke.py (not modified) with --interp pointing at an assembled copy of THIS unit's sources:
  <unit>/luau/*.luau            the interpreter under test (flat modules: parser, lower, interp, commands, ...)
  <unit>/tests/replay_glue/*    corpus.luau + npcrun.luau, the harness-side helpers the replay's Luau driver requires
smoke.py compares the ordered host calls (command, evaluated arguments, return value, failure flag) and the end state (variables,
params, inventory) of the real rAthena script VM against this interpreter on the same NPCs with the same scripted answers.

  python3 -I tests/replay_regression.py --build <replay build> --build-pre <pre-renewal build> --luau <luau cli> [--jobs 4]
  python3 -I tests/replay_regression.py ... --corpus wide|hunt|synthetic   # the larger corpora (minutes), strict counts compared to the
                                                                          # recorded results in tooling/m0/replay/README.md

Exit 0 only when renewal 15/15, pre-renewal 3/3 and synthetic 16/16 (the recorded smoke baseline) hold; with --build-pre omitted the
pre-renewal part is skipped and reported as skipped, never as passed."""
import argparse, json, os, re, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
UNIT = os.path.dirname(HERE)
REPLAY = os.path.abspath(os.path.join(UNIT, '..', 'tooling', 'm0', 'replay'))
BASELINE = {'re': (15, 15), 'pre': (3, 3), 'synthetic': (16, 16)}

ap = argparse.ArgumentParser()
ap.add_argument('--build', default='', help='renewal replay build (tooling/m0/replay/build_legacy.sh output)')
ap.add_argument('--build-pre', default='', help='pre-renewal replay build; optional')
ap.add_argument('--luau', default='')
ap.add_argument('--from-env', action='store_true', help='take LNPC_REPLAY_BUILD, LNPC_REPLAY_BUILD_PRE, LNPC_LUAU_CLI from the environment; exit 77 (ctest SKIP) when unset')
ap.add_argument('--jobs', type=int, default=4)
ap.add_argument('--keep', action='store_true', help='keep the assembled interpreter directory')
ap.add_argument('--corpus', choices=['smoke', 'wide', 'hunt', 'synthetic'], default='smoke')
args = ap.parse_args()
if args.from_env:
    args.build = args.build or os.environ.get('LNPC_REPLAY_BUILD', '')
    args.build_pre = args.build_pre or os.environ.get('LNPC_REPLAY_BUILD_PRE', '')
    args.luau = args.luau or os.environ.get('LNPC_LUAU_CLI', '')
    if not args.build or not args.luau:
        print('SKIP: set LNPC_REPLAY_BUILD (and LNPC_REPLAY_BUILD_PRE) and LNPC_LUAU_CLI to run the replay regression'); sys.exit(77)
if not args.build or not args.luau:
    ap.error('--build and --luau are required')

work = tempfile.mkdtemp(prefix='lnpc-interp-', dir=os.environ.get('LNPC_SCRATCH') or None)
try:
    for f in os.listdir(os.path.join(UNIT, 'luau')):
        if f.endswith('.luau'):
            shutil.copy(os.path.join(UNIT, 'luau', f), work)
    for f in os.listdir(os.path.join(HERE, 'replay_glue')):
        if f.endswith('.luau'):
            shutil.copy(os.path.join(HERE, 'replay_glue', f), work)
    if args.corpus == 'smoke':
        cmd = [sys.executable, '-I', os.path.join(REPLAY, 'smoke.py'), '--build', args.build, '--luau', args.luau,
               '--jobs', str(args.jobs), '--interp', work]
        if args.build_pre: cmd += ['--build-pre', args.build_pre]
        p = subprocess.run(cmd, capture_output=True, text=True)
        sys.stdout.write(p.stdout)
        if p.returncode != 0 and p.stderr: sys.stderr.write(p.stderr[-2000:])
        got = {}
        for m in re.finditer(r'^(re|pre|synthetic)\s+(\d+)/(\d+) strict', p.stdout, re.M):
            got[m.group(1)] = (int(m.group(2)), int(m.group(3)))
        bad = []
        for k, want in BASELINE.items():
            if k not in got:
                if k == 'pre' and not args.build_pre: print('pre-renewal: SKIPPED (no --build-pre)'); continue
                bad.append('%s: no result' % k); continue
            if got[k] != want: bad.append('%s: %d/%d, baseline %d/%d' % ((k,) + got[k] + want))
        print('replay regression (%s): %s' % ('smoke', 'FAILED: ' + '; '.join(bad) if bad or p.returncode else
              ', '.join('%s %d/%d' % (k, *got[k]) for k in got)))
        sys.exit(1 if bad or p.returncode else 0)
    else:
        part = {'wide': ['--corpus', os.path.join(REPLAY, 'corpus', 'corpus_wide.json')],
                'hunt': ['--corpus', os.path.join(REPLAY, 'corpus', 'corpus_hunt.json')],
                'synthetic': ['--synthetic']}[args.corpus]
        total = {}
        rc = 0
        for setname, build in (('re', args.build), ('pre', args.build_pre)):
            if not build: continue
            cmd = [sys.executable, '-I', os.path.join(REPLAY, 'run_replay.py'), '--build', build, '--luau', args.luau, '--jobs', str(args.jobs),
                   '--interp', work, '--set', setname, '--report-name', 'lnpc-%s-%s' % (args.corpus, setname)] + part
            p = subprocess.run(cmd, capture_output=True, text=True)
            res = os.path.join(build, 'out', 'results-lnpc-%s-%s.json' % (args.corpus, setname))
            if not os.path.exists(res):
                print('%s %s: run_replay failed\n%s\n%s' % (args.corpus, setname, p.stdout[-1500:], p.stderr[-1500:])); rc = 2; continue
            cases = json.load(open(res))['cases']
            strict = sum(1 for c in cases if c['strict_pass'])
            print('%s %-3s %d/%d strict' % (args.corpus, setname, strict, len(cases)))
            total[setname] = (strict, len(cases))
        sys.exit(rc)
finally:
    if not args.keep: shutil.rmtree(work, ignore_errors=True)
