#!/usr/bin/env python3
"""Run the record/replay conformance corpus on BOTH sides and compare.

  python3 -I run_replay.py --build /tmp/.../replay-build --luau /path/to/luau [--jobs 4] [--only c01,c02] [--skip-legacy] [--skip-luau]

Needs (see build_legacy.sh): <build>/rA/map-server built with patch/legacy-replay-hook.patch, and <build>/lua
(created here from ../legacy_interp + prep.py data modules). Output: <build>/out/{legacy,luau}/<case>.trace and report files.
"""
import os, re, sys, json, shutil, subprocess, argparse, time, concurrent.futures as cf

HERE = os.path.dirname(os.path.abspath(__file__))
ap = argparse.ArgumentParser()
ap.add_argument('--build', required=True)
ap.add_argument('--luau', required=True)
ap.add_argument('--repo', default=os.environ.get('RATHENA_REPO', '/home/user/reAthena'))
ap.add_argument('--jobs', type=int, default=4)
ap.add_argument('--only', default='')
ap.add_argument('--skip-legacy', action='store_true')
ap.add_argument('--skip-luau', action='store_true')
ap.add_argument('--corpus', default=os.path.join(HERE, 'corpus', 'corpus.json'))
ap.add_argument('--timeout', type=int, default=180)
ap.add_argument('--no-seed', action='store_true', help='do not seed the Luau start state from the legacy VM (run OnInit on the Luau side instead)')
ap.add_argument('--no-real-constants', action='store_true', help='keep the synthetic constant ids of prep.py (baseline)')
args = ap.parse_args()

B = os.path.abspath(args.build)
RA = os.path.join(B, 'rA')
LUA = os.path.join(B, 'lua')
OUT = os.path.join(B, 'out')
LI = os.path.join(HERE, '..', 'legacy_interp')

# ---- case list ---------------------------------------------------------------------------------------
corpus = json.load(open(args.corpus))['chosen']
RICH = dict(zeny=1000000, baselevel=99, joblevel=50)
POOR = dict(zeny=0, baselevel=1, joblevel=1)
VARIANTS = [  # (suffix, player profile, answer list)
    ('a', POOR, []),                                   # every menu = first option, inputs = 1 / "test"
    ('b', RICH, ['99'] * 60),                          # every menu = last option (clamped)
    ('c', RICH, ['2', '1', '3', '2', '2', '1'] * 10),  # mixed
]
cases = []
for i, n in enumerate(corpus, 1):
    for suf, prof, ans in VARIANTS:
        cases.append(dict(id='n%02d%s' % (i, suf), npc=n['name'], path=n['file'], category=n['category'], line=n['line'],
                          player=prof, answers=ans, cap=60, label=n.get('label')))
if args.only:
    want = set(args.only.split(','))
    cases = [c for c in cases if c['id'] in want or c['id'][:3] in want]

for d in ('legacy', 'luau', 'luau-nosc'):
    os.makedirs(os.path.join(OUT, d), exist_ok=True)
json.dump(cases, open(os.path.join(OUT, 'cases.json'), 'w'), indent=1)

# ---- Luau side ----------------------------------------------------------------------------------------
def lua_val(v):
    if isinstance(v, bool): return 'true' if v else 'false'
    if isinstance(v, (int, float)): return str(v)
    if v is None: return 'nil'
    if isinstance(v, str): return json.dumps(v)
    if isinstance(v, list): return '{' + ','.join(lua_val(x) for x in v) + '}'
    if isinstance(v, dict): return '{' + ','.join('[%s]=%s' % (json.dumps(k), lua_val(x)) for k, x in v.items()) + '}'
    raise TypeError(v)

def lua_str(b):
    out = ['"']
    for c in b:
        if c in (34, 92): out.append('\\' + chr(c))
        elif 32 <= c < 127: out.append(chr(c))
        else: out.append('\\%03d' % c)
    return ''.join(out) + '"'

def unq(tok):
    if tok.startswith('"') and tok.endswith('"') and len(tok) >= 2:
        b = bytearray(); t = tok[1:-1].encode('latin-1'); i = 0
        while i < len(t):
            if t[i] == 92 and i + 1 < len(t):
                n = t[i + 1]
                if n == ord('x') and i + 3 < len(t) + 0:
                    b.append(int(t[i + 2:i + 4], 16)); i += 4; continue
                b.append({ord('n'): 10, ord('r'): 13, ord('t'): 9}.get(n, n)); i += 2; continue
            b.append(t[i]); i += 1
        return bytes(b)
    return None

def load_init(cid):
    """start-state snapshot written by the legacy hook: 'key<TAB>value' -> Luau literal pairs"""
    path = os.path.join(OUT, 'legacy', cid + '.trace.init')
    if not os.path.exists(path): return None
    items = []
    for line in open(path, 'rb').read().decode('latin-1').split('\n'):
        if '\t' not in line: continue
        k, v = line.split('\t', 1)
        sv = unq(v)
        items.append('{%s,%s}' % (lua_str(k.encode('latin-1')), lua_str(sv) if sv is not None else v))
    return '{' + ','.join(items) + '}'

def dump_legacy_constants():
    tsv = os.path.join(OUT, 'legacy_consts.tsv')
    if not os.path.exists(tsv):
        run = os.path.join(OUT, 'run', 'consts'); shutil.rmtree(run, ignore_errors=True); os.makedirs(os.path.join(run, 'save'))
        open(os.path.join(run, 'inter.conf'), 'w').write('import: conf/inter_athena.conf\ndb_backend: sqlite\nsqlite_directory: %s\n' % os.path.join(run, 'save'))
        open(os.path.join(run, 'map.conf'), 'w').write('import: conf/map_athena.conf\nmap_port: 5399\n')
        subprocess.run(['bash', '-c', 'exec -a "%s/replay-map-server" ./map-server --run-once --map-config %s --inter-config %s' % (
            RA, os.path.join(run, 'map.conf'), os.path.join(run, 'inter.conf'))], cwd=RA, capture_output=True, timeout=args.timeout,
            env=dict(os.environ, RA_REPLAY_DUMP_CONSTS=tsv))
    return tsv

def merge_constants(tsv):
    """legacy_interp's prep.py synthesises ids for enum-valued constants; replace them by the values the real VM has."""
    synth = os.path.join(LUA, 'consts.synthetic.luau')
    if not os.path.exists(synth): shutil.copy(os.path.join(LUA, 'consts.luau'), synth)
    txt = open(synth).read()
    head, _, tail = txt.partition('},params={')
    cons = {}
    for m in re.finditer(r'^\["([^"]+)"\]=(-?\d+),$', head, re.M): cons[m.group(1)] = int(m.group(2))
    changed = added = 0
    for line in open(tsv, encoding='latin-1'):
        if '\t' not in line: continue
        k, v = line.rstrip('\n').split('\t'); k = k.lower(); v = int(v)
        if k in cons:
            if cons[k] != v: changed += 1
        else: added += 1
        cons[k] = v
    with open(os.path.join(LUA, 'consts.luau'), 'w') as f:
        f.write('return {consts={\n' + ''.join('["%s"]=%d,\n' % (k, v) for k, v in sorted(cons.items())) + '},params={' + tail)
    print('constants: %d from the real VM; %d synthetic ids replaced, %d added' % (sum(1 for _ in open(tsv, encoding='latin-1')), changed, added))

def run_luau(mode):
    if not os.path.isdir(os.path.join(LUA, 'data')):
        os.makedirs(LUA, exist_ok=True)
        for f in os.listdir(LI):
            if f.endswith('.luau') or f == 'prep.py':
                shutil.copy(os.path.join(LI, f), LUA)
        subprocess.run([sys.executable, '-I', 'prep.py'], cwd=LUA, check=True, stdout=subprocess.DEVNULL,
                       env=dict(os.environ, RATHENA_REPO=args.repo))
    if not args.no_real_constants and os.path.isdir(RA):
        merge_constants(dump_legacy_constants())
    elif os.path.exists(os.path.join(LUA, 'consts.synthetic.luau')):
        shutil.copy(os.path.join(LUA, 'consts.synthetic.luau'), os.path.join(LUA, 'consts.luau'))
    for f in os.listdir(os.path.join(HERE, 'luau')):
        if f.endswith('.luau') and f != 'cases.luau':
            shutil.copy(os.path.join(HERE, 'luau', f), LUA)
    with open(os.path.join(LUA, 'cases.luau'), 'w') as f:
        f.write('return {\n' + ',\n'.join('{id=%s,path=%s,npc=%s,answers=%s,player=%s,cap=%d,mode=%s%s%s}' % (
            lua_val(c['id']), lua_val(c['path']), lua_val(c['npc']), lua_val(c['answers']), lua_val(c['player']), c['cap'], lua_val(mode),
            (',label=%s' % lua_val(c['label'])) if c.get('label') else '',
            (lambda i: (',init=%s' % i) if i and not args.no_seed else '')(load_init(c['id']))) for c in cases) + '\n}\n')
    t = time.time()
    p = subprocess.run([args.luau, 'run_cases.luau'], cwd=LUA, capture_output=True, timeout=3600)
    txt = p.stdout.decode('latin-1')
    cur, buf = None, []
    for line in txt.split('\n'):
        if line.startswith('=== CASE '):
            cur, buf = line[9:].strip(), []
        elif line.startswith('=== END ') and cur:
            open(os.path.join(OUT, 'luau' if mode == 'asis' else 'luau-' + mode, cur + '.trace'), 'wb').write(('\n'.join(buf) + '\n').encode('latin-1'))
            cur = None
        elif cur is not None:
            buf.append(line)
    open(os.path.join(OUT, 'luau-%s.stderr' % mode), 'wb').write(p.stderr)
    print('luau side [%s]: %d cases in %.1fs (rc=%d)' % (mode, len(cases), time.time() - t, p.returncode))

# ---- legacy side -------------------------------------------------------------------------------------
def run_legacy_case(args_):
    slot, c = args_
    run = os.path.join(OUT, 'run', c['id'])
    shutil.rmtree(run, ignore_errors=True)
    os.makedirs(os.path.join(run, 'save'))
    open(os.path.join(run, 'inter.conf'), 'w').write('import: conf/inter_athena.conf\ndb_backend: sqlite\nsqlite_directory: %s\n' % os.path.join(run, 'save'))
    open(os.path.join(run, 'map.conf'), 'w').write('import: conf/map_athena.conf\nmap_port: %d\n' % (5400 + slot))
    open(os.path.join(run, 'answers.txt'), 'w').write('\n'.join(c['answers']) + '\n')
    pl = dict(c['player']); pl.setdefault('name', 'Tester')
    open(os.path.join(run, 'player.cfg'), 'w').write(''.join('%s=%s\n' % kv for kv in pl.items()))
    trace = os.path.join(OUT, 'legacy', c['id'] + '.trace')
    if os.path.exists(trace): os.remove(trace)
    env = dict(os.environ, RA_REPLAY_NPC=c['npc'], RA_REPLAY_ANSWERS=os.path.join(run, 'answers.txt'),
               RA_REPLAY_PLAYER=os.path.join(run, 'player.cfg'), RA_REPLAY_TRACE=trace, RA_REPLAY_CAP=str(c['cap']))
    if c.get('label'): env['RA_REPLAY_LABEL'] = c['label']
    cmd = ['bash', '-c', 'exec -a "%s/replay-map-server" ./map-server --run-once --map-config %s --inter-config %s' % (
        RA, os.path.join(run, 'map.conf'), os.path.join(run, 'inter.conf'))]
    try:
        p = subprocess.run(cmd, cwd=RA, env=env, capture_output=True, timeout=args.timeout)
        rc = p.returncode
        log = p.stdout + p.stderr
    except subprocess.TimeoutExpired as e:
        rc, log = -9, (e.stdout or b'') + (e.stderr or b'')
    open(os.path.join(run, 'log.txt'), 'wb').write(log)
    if not os.path.exists(trace):
        open(trace, 'w').write('#end legacy-no-trace rc=%d\n' % rc)
    elif rc != 0:
        open(trace, 'a').write('#legacy-exit rc=%d\n' % rc)
    shutil.rmtree(os.path.join(run, 'save'), ignore_errors=True)
    return c['id'], rc

def run_legacy():
    t = time.time()
    jobs = [(i % args.jobs, c) for i, c in enumerate(cases)]
    # one worker per slot (distinct port): run slots in parallel, cases of a slot sequentially
    by_slot = {}
    for s, c in jobs: by_slot.setdefault(s, []).append((s, c))
    def worker(lst): return [run_legacy_case(j) for j in lst]
    bad = []
    with cf.ThreadPoolExecutor(max_workers=args.jobs) as ex:
        for res in ex.map(worker, by_slot.values()):
            bad += [r for r in res if r[1] != 0]
    print('legacy side: %d cases in %.1fs, non-zero exits: %s' % (len(cases), time.time() - t, bad))

if not args.skip_legacy: run_legacy()
rc = 0
for mode in ('asis', 'nosc'):
    if not args.skip_luau: run_luau(mode)
    tag = '' if mode == 'asis' else '-nosc'
    print('\n######## legacy_interp mode: %s ########' % ('as is' if mode == 'asis' else 'operands of && || ?: all evaluated (legacy semantics)'))
    rc |= subprocess.run([sys.executable, '-I', os.path.join(HERE, 'compare_traces.py'), '--cases', os.path.join(OUT, 'cases.json'),
                          '--legacy', os.path.join(OUT, 'legacy'), '--luau', os.path.join(OUT, 'luau' + tag), '--out', OUT, '--tag=' + tag]).returncode
sys.exit(rc)
