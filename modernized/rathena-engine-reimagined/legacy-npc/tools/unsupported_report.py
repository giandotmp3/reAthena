#!/usr/bin/env python3
"""Per-NPC unsupported-command report over a load set of the legacy corpus.

  python3 -I tools/unsupported_report.py --repo <rathena checkout> --luau <luau cli> [--set re|pre] [--out report.tsv] [--top 25]

Prints a table  file <TAB> line <TAB> npc <TAB> unsupported commands <TAB> gated commands  for every NPC that uses a command the
compatibility layer does not provide (those NPCs are disabled at run time, the others are untouched), followed by the commands that
disable the most NPCs. Runs the unit's own Luau report (luau/report.luau) in the Luau CLI over generated data modules."""
import argparse, os, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
UNIT = os.path.dirname(HERE)
ap = argparse.ArgumentParser()
ap.add_argument('--repo', default=os.path.abspath(os.path.join(UNIT, '..', '..', '..')))
ap.add_argument('--luau', required=True)
ap.add_argument('--set', choices=['re', 'pre'], default='re')
ap.add_argument('--out', default='')
ap.add_argument('--top', type=int, default=25)
args = ap.parse_args()

work = tempfile.mkdtemp(prefix='lnpc-report-', dir=os.environ.get('LNPC_SCRATCH') or None)
try:
    subprocess.run([sys.executable, '-I', os.path.join(HERE, 'gen_corpus_modules.py'), '--repo', args.repo, '--set', args.set, '--out', work],
                   check=True, stdout=subprocess.DEVNULL)
    os.makedirs(os.path.join(work, 'pkg'))
    for f in os.listdir(os.path.join(UNIT, 'luau')):  # (the Luau CLI does not follow a symlinked directory in require paths)
        if f.endswith('.luau'): shutil.copy(os.path.join(UNIT, 'luau', f), os.path.join(work, 'pkg', f))
    shutil.copy(os.path.join(HERE, 'corpus_bench.luau'), work)
    p = subprocess.run([args.luau, 'corpus_bench.luau', '-a', 'report'], cwd=work, capture_output=True, timeout=7200)  # bytes: NPC names are latin-1
    out = p.stdout.decode('latin-1')
    if p.returncode != 0:
        sys.stderr.write(p.stderr.decode('latin-1')[-3000:]); sys.exit(1)
    lines = out.splitlines()
    try:
        a, b = lines.index('@report-begin'), lines.index('@report-end')
    except ValueError:
        sys.stderr.write(out[-3000:]); sys.exit(1)
    table = lines[a + 1:b]
    top = [l.split('\t')[1:] for l in lines if l.startswith('@top\t')][:args.top]
    bench = [l for l in lines if l.startswith('@bench')]
    text = '\n'.join(table) + '\n'
    if args.out: open(args.out, 'w', encoding='latin-1').write(text)
    else: sys.stdout.buffer.write(text.encode('latin-1'))
    sys.stderr.write('\nunsupported commands by number of NPCs disabled (top %d):\n' % len(top))
    for name, n in top: sys.stderr.write('  %-28s %s\n' % (name, n))
    if bench: sys.stderr.write(bench[0] + '\n')
finally:
    shutil.rmtree(work, ignore_errors=True)
