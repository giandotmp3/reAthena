#!/usr/bin/env python3
"""Run the record/replay conformance corpus on BOTH sides and compare.

  python3 -I run_replay.py --build /tmp/.../replay-build --luau /path/to/luau [--jobs 4] [--only c01,c02] [--skip-legacy] [--skip-luau]

Needs (see build_legacy.sh): <build>/rA/map-server built with patch/legacy-replay-hook.patch, and <build>/lua
(created here from ../legacy_interp + prep.py data modules). Output: <build>/out/{legacy,luau}/<case>.trace and report files.
"""
import os, re, sys, json, glob, shutil, subprocess, argparse, time, concurrent.futures as cf

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
ap.add_argument('--set', default='re', choices=['re', 'pre'], help='load set: run only the corpus entries of this set (renewal / pre-renewal server build in --build)')
ap.add_argument('--prefix', default='', help='case id prefix (default: n for corpus.json, w for corpus_wide.json)')
ap.add_argument('--report-name', default='report', help='base name of the report files in <build>/out')
ap.add_argument('--interp', default='', help='directory with the legacy_interp sources to test (default: ../legacy_interp)')
ap.add_argument('--synthetic', action='store_true', help='run the synthetic conformance NPCs of corpus/synthetic.txt instead of a corpus (installed temporarily into the build COPY for the legacy side)')
ap.add_argument('--synthetic-file', default='', help='with --synthetic: use this NPC file instead of corpus/synthetic.txt')
ap.add_argument('--no-world', action='store_true', help='do not feed dumped legacy-VM world data (item names) to the Luau side')
ap.add_argument('--no-seed', action='store_true', help='do not seed the Luau start state from the legacy VM (run OnInit on the Luau side instead)')
ap.add_argument('--no-real-constants', action='store_true', help='keep the synthetic constant ids of prep.py (baseline)')
args = ap.parse_args()

B = os.path.abspath(args.build)
RA = os.path.join(B, 'rA')
LUA = os.path.join(B, 'lua')
OUT = os.path.join(B, 'out')
LI = os.path.abspath(args.interp) if args.interp else os.path.join(HERE, '..', 'legacy_interp')

# ---- case list ---------------------------------------------------------------------------------------
SYN = os.path.abspath(args.synthetic_file) if args.synthetic_file else os.path.join(HERE, 'corpus', 'synthetic.txt')
if args.synthetic:
    corpus = [dict(name=m.group(1), file='synthetic', line=0, category='synthetic', set=args.set)
              for m in re.finditer(r'^-\tscript\t([^\t]+)\t-1,\{', open(SYN, encoding='latin-1').read(), re.M)]
    args.prefix = args.prefix or 'y'
else:
    corpus = json.load(open(args.corpus))['chosen']
PREFIX = args.prefix or ('w' if 'wide' in os.path.basename(args.corpus) else 'h' if 'hunt' in os.path.basename(args.corpus) else 'n')
RICH = dict(zeny=1000000, baselevel=99, joblevel=50)
POOR = dict(zeny=0, baselevel=1, joblevel=1)
VARIANTS = [  # (suffix, player profile, answer list)
    ('a', POOR, []),                                   # every menu = first option, inputs = 1 / "test"
    ('b', RICH, ['99'] * 60),                          # every menu = last option (clamped)
    ('c', RICH, ['2', '1', '3', '2', '2', '1'] * 10),  # mixed
]
def header_pos(path, line):
    """x,y of the NPC header at <path>:<line> (the legacy hook puts the synthetic player on the NPC)"""
    try:
        text = open(os.path.join(args.repo, path), 'rb').read().decode('latin-1').replace('\r\n', '\n').split('\n')
        m = re.match(r'^([\w@.\-]+),(\d+),(\d+),\d+\t', text[line - 1])
        if m: return m.group(1), int(m.group(2)), int(m.group(3))
    except Exception:
        pass
    return None, 150, 150
if args.synthetic: VARIANTS = VARIANTS + [('d', RICH, ['0'] * 60)]   # every menu/select cancelled
cases = []
for i, n in enumerate(corpus, 1):
    if n.get('set', 're') != args.set: continue
    mp, x, y = (None, 150, 150) if args.synthetic else header_pos(n['file'], n['line'])
    for suf, prof, ans in VARIANTS:
        cases.append(dict(id=('%s%02d%s' if PREFIX in ('n', 'y', 's') else '%s%03d%s') % (PREFIX, i, suf), synth=args.synthetic, npc=n['name'], path=n['file'],
                          category=n['category'], line=n['line'], set=args.set, x=x, y=y,
                          player=prof, answers=ans, cap=60, label=n.get('label')))
if args.only:
    want = set(args.only.split(','))
    cases = [c for c in cases if c['id'] in want or c['id'][:-1] in want]

for d in ('legacy', 'luau'):
    os.makedirs(os.path.join(OUT, d), exist_ok=True)
json.dump(cases, open(os.path.join(OUT, 'cases.json'), 'w'), indent=1)

# ---- Luau side ----------------------------------------------------------------------------------------
def lua_val(v):
    if isinstance(v, bool): return 'true' if v else 'false'
    if isinstance(v, (int, float)): return str(v)
    if v is None: return 'nil'
    if isinstance(v, str): return lua_str(v.encode('latin-1', 'replace'))
    if isinstance(v, list): return '{' + ','.join(lua_val(x) for x in v) + '}'
    if isinstance(v, dict): return '{' + ','.join('[%s]=%s' % (lua_val(k), lua_val(x)) for k, x in v.items()) + '}'
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

def item_ids():
    """(Id, AegisName) of every item_db entry of the load set's db directory, in file order"""
    db = os.path.join(args.repo, 'db', 're' if args.set == 're' else 'pre-re')
    out = []
    for fn in sorted(os.listdir(db)):
        if not (fn.startswith('item_db') and fn.endswith('.yml')): continue
        cur = None
        for line in open(os.path.join(db, fn), 'rb').read().decode('latin-1').splitlines():
            m = re.match(r'^  - Id: (\d+)\s*$', line)
            if m: cur = [int(m.group(1)), None]; out.append(cur); continue
            m = re.match(r'^    AegisName: (\S+)\s*$', line)
            if m and cur is not None: cur[1] = m.group(1)
    return [tuple(x) for x in out]

def dump_world():
    """Feed the Luau side REAL data from the legacy VM: a probe NPC (generated, installed only in the build COPY for this one run) calls
    getitemname() for every item id / AegisName of the load set's item DB; the hook records each call and return like any other trace."""
    out = os.path.join(OUT, 'world_itemnames.trace')
    if os.path.exists(out) and os.path.getsize(out) > 0: return out
    db = os.path.join(args.repo, 'db', 're' if args.set == 're' else 'pre-re')
    calls = []
    for iid, aegis in item_ids():
        calls.append('getitemname(%d);' % iid)
        if aegis: calls.append('getitemname("%s");' % aegis)
    for fn in sorted(glob.glob(os.path.join(args.repo, 'conf', 'battle', '*.conf'))):   # battle config names (getbattleflag)
        for line in open(fn, 'rb').read().decode('latin-1').splitlines():
            m = re.match(r'^([A-Za-z_0-9.]+)\s*:', line)
            if m: calls.append('getbattleflag("%s");' % m.group(1))
    cdb = os.path.join(db, 'castle_db.yml')
    if os.path.exists(cdb):                                                              # castle maps (getcastledata)
        for line in open(cdb, 'rb').read().decode('latin-1').splitlines():
            m = re.match(r'^\s+Map: (\S+)', line)
            if m: calls.append('getcastledata("%s",1);' % m.group(1))
    calls.append('getcastledata("replay_no_such_map",1);')
    seen_maps = []
    for fn in ('maps_athena.conf',):                                                     # maps the server loads (warp, mobcount)
        for line in open(os.path.join(args.repo, 'conf', fn), 'rb').read().decode('latin-1').splitlines():
            m = re.match(r'^map:\s*(\S+)', line)
            if m: seen_maps.append(m.group(1))
    for mp in list(dict.fromkeys(seen_maps)) + ['replay_no_such_map']:
        calls.append('mobcount("%s","replay_probe::x");' % mp)
    return run_probe(calls, out)

def run_probe(calls, out):
    """run a generated floating NPC made of `calls` (script statements) on the legacy VM; the hook records every call and return into `out`"""
    probe = os.path.join(RA, 'npc', 'replay_probe.txt')
    conf = os.path.join(RA, 'npc', 're' if args.set == 're' else 'pre-re', 'scripts_main.conf')
    conf_txt = open(conf, 'rb').read()
    open(probe, 'w').write('-\tscript\treplay_probe\t-1,{\n' + '\n'.join(calls) + '\nend;\n}\n')
    open(conf, 'ab').write(b'\nnpc: npc/replay_probe.txt\n')
    run = os.path.join(OUT, 'run', 'probe'); shutil.rmtree(run, ignore_errors=True); os.makedirs(os.path.join(run, 'save'))
    open(os.path.join(run, 'inter.conf'), 'w').write('import: conf/inter_athena.conf\ndb_backend: sqlite\nsqlite_directory: %s\n' % os.path.join(run, 'save'))
    open(os.path.join(run, 'map.conf'), 'w').write('import: conf/map_athena.conf\nmap_port: 5399\n')
    open(os.path.join(run, 'answers.txt'), 'w').write('')
    open(os.path.join(run, 'player.cfg'), 'w').write('name=Tester\n')
    try:
        subprocess.run(['bash', '-c', 'exec -a "%s/replay-map-server" ./map-server --run-once --map-config %s --inter-config %s' % (
            RA, os.path.join(run, 'map.conf'), os.path.join(run, 'inter.conf'))], cwd=RA, capture_output=True, timeout=args.timeout * 4,
            env=dict(os.environ, RA_REPLAY_NPC='replay_probe', RA_REPLAY_ANSWERS=os.path.join(run, 'answers.txt'),
                     RA_REPLAY_PLAYER=os.path.join(run, 'player.cfg'), RA_REPLAY_TRACE=out, RA_REPLAY_CAP='5'))
    finally:
        open(conf, 'wb').write(conf_txt)
        if os.path.exists(probe): os.remove(probe)
        shutil.rmtree(run, ignore_errors=True)
    return out

def shop_names():
    """names of the shop-type NPCs of the load set (callshop): declaration headers of the files in the conf import graph"""
    files = []
    def walk(conf):
        for raw in open(os.path.join(args.repo, conf), 'rb').read().decode('latin-1').splitlines():
            m = re.match(r'^(npc|import)\s*:\s*(\S.*)$', raw.split('//', 1)[0].strip())
            if not m: continue
            if m.group(1) == 'npc': files.append(m.group(2).strip())
            else: walk(m.group(2).strip())
    walk('npc/re/scripts_main.conf' if args.set == 're' else 'npc/pre-re/scripts_main.conf')
    out = set()
    def barters(path, seen):
        # npc/barters.yml and its Footer Imports (Mode: Renewal / Prerenewal); a barter shop is an NPC of type BARTER named by `- Name:`
        full = os.path.join(args.repo, path)
        if path in seen or not os.path.exists(full): return
        seen.add(path)
        mode, cur = None, None
        for line in open(full, 'rb').read().decode('latin-1').splitlines():
            m = re.match(r'^\s*- Path:\s*(\S+)', line)
            if m: cur = [m.group(1), None]; imports.append(cur); continue
            m = re.match(r'^\s+Mode:\s*(\S+)', line)
            if m and cur: cur[1] = m.group(1)
            m = re.match(r'^\s*- Name:\s*(\S+)', line)
            if m: out.add(m.group(1))
        for pth, md in [x for x in imports if x not in done]:
            done.append([pth, md])
            if md is None or (md == 'Renewal') == (args.set == 're'): barters(pth, seen)
    imports, done = [], []
    barters('npc/barters.yml', set())
    for f in dict.fromkeys(files):
        p = os.path.join(args.repo, f)
        if not os.path.exists(p): continue
        text = open(p, 'rb').read().decode('latin-1').replace('\r\n', '\n')
        text = re.sub(r'/\*.*?\*/', lambda m: re.sub(r'[^\n]', ' ', m.group(0)), text, flags=re.S)
        for m in re.finditer(r'^[^\t\n/][^\t\n]*\t(?:shop|cashshop|itemshop|pointshop|marketshop|barter)\t([^\t\n]+)\t', text, re.M):
            out.add(m.group(1))
    return out

LOOKUP_CACHE = os.path.join(OUT, 'world_lookup.json')
def load_lookup():
    try: return json.load(open(LOOKUP_CACHE))
    except Exception: return {}

def probe_lookups(keys):
    """demand-driven world data: ask the legacy VM the exact static-data queries (getiteminfo(id,type), ...) the Luau side missed"""
    out = os.path.join(OUT, 'world_lookup.trace')
    if os.path.exists(out): os.remove(out)
    keys = [k for k in keys if '<' not in k]          # a query with an unformattable argument cannot be asked (and would break the probe script)
    if not keys: return 0
    run_probe([k + ';' for k in keys], out)
    cache = load_lookup()
    got = 0
    for line in open(out, 'rb').read().decode('latin-1').split('\n'):
        m = re.match(r'^(.*?)( -> (.*?))?( !F)?$', line)
        if not m or not re.match(r'^[a-z_0-9]+\(', line): continue
        key, ret = m.group(1), m.group(3)
        if ret is None: val = 0
        else:
            sv = unq(ret)
            val = sv.decode('latin-1') if sv is not None else (int(ret) if re.fullmatch(r'-?\d+', ret) else 0)
        cache[key] = val; got += 1
    json.dump(cache, open(LOOKUP_CACHE, 'w'), indent=0)
    return got

def write_world(path):
    """world_data.luau: data recorded from the legacy VM (item names, battle config, castle maps) + shop NPC names of the load set"""
    names, bflag, castle, maps = {}, {}, {}, {}
    for line in open(path, 'rb').read().decode('latin-1').split('\n'):
        m = re.match(r'^getitemname\((\d+|"[^"]*")\) -> ("(?:[^"\\]|\\.)*")$', line)
        if m:
            k = m.group(1); v = unq(m.group(2))
            if v is not None: names[int(k) if k[0] != '"' else k[1:-1]] = v
            continue
        m = re.match(r'^getbattleflag\("([^"]*)"\) -> (-?\d+)$', line)
        if m: bflag[m.group(1).lower()] = int(m.group(2)); continue
        m = re.match(r'^getcastledata\("([^"]*)",1\)( -> -?\d+)?( !F)?$', line)
        if m: castle[m.group(1)] = not m.group(3); continue
        m = re.match(r'^mobcount\("([^"]*)","replay_probe::x"\) -> (-?\d+)$', line)
        if m: maps[m.group(1)] = m.group(2) != '-1'
    shops = shop_names()
    cache = load_lookup()
    with open(os.path.join(LUA, 'world_data.luau'), 'wb') as f:
        f.write(b'return {itemname={\n')
        for k, v in names.items():
            f.write(((('[%d]=' % k) if isinstance(k, int) else ('[%s]=' % json.dumps(k))) + lua_str(v) + ',\n').encode('latin-1'))
        f.write(b'},aegis={\n')
        for iid, aegis in item_ids():
            if aegis: f.write(('[%s]=%d,\n' % (json.dumps(aegis), iid)).encode('latin-1'))
        f.write(b'},battleflag={\n')
        for k, v in bflag.items(): f.write(('[%s]=%d,\n' % (json.dumps(k), v)).encode('latin-1'))
        f.write(b'},castle={\n')
        for k, v in castle.items(): f.write(('[%s]=%s,\n' % (json.dumps(k), 'true' if v else 'false')).encode('latin-1'))
        f.write(b'},lookup={\n')
        for k, v in cache.items(): f.write(('[%s]=%s,\n' % (lua_str(k.encode('latin-1', 'replace')), lua_str(v.encode('latin-1', 'replace')) if isinstance(v, str) else repr(v))).encode('latin-1'))
        f.write(b'},map={\n')
        for k, v in maps.items(): f.write(('[%s]=%s,\n' % (json.dumps(k), 'true' if v else 'false')).encode('latin-1'))
        f.write(b'},shop={\n')
        for k in sorted(shops): f.write(('[%s]=true,\n' % lua_str(k.encode('latin-1', 'replace'))).encode('latin-1'))
        f.write(b'}}\n')
    return len(names), len(bflag), sum(1 for v in castle.values() if v), sum(1 for v in maps.values() if v), len(shops)

def run_luau():
    if not os.path.isdir(os.path.join(LUA, 'data')):
        os.makedirs(LUA, exist_ok=True)
        shutil.copy(os.path.join(LI, 'prep.py'), LUA)
        for f in ('consts.luau', 'manifest.luau', 'builtins.luau'):
            if os.path.exists(os.path.join(LI, f)): shutil.copy(os.path.join(LI, f), LUA)
        subprocess.run([sys.executable, '-I', 'prep.py'], cwd=LUA, check=True, stdout=subprocess.DEVNULL,
                       env=dict(os.environ, RATHENA_REPO=args.repo))
    # always re-sync the interpreter sources under test (the data modules, consts, manifest, builtins are generated by prep.py)
    for f in os.listdir(LI):
        if f.endswith('.luau') and f not in ('consts.luau', 'manifest.luau', 'builtins.luau'):
            shutil.copy(os.path.join(LI, f), LUA)
    if not args.no_real_constants and os.path.isdir(RA):
        merge_constants(dump_legacy_constants())
    elif os.path.exists(os.path.join(LUA, 'consts.synthetic.luau')):
        shutil.copy(os.path.join(LUA, 'consts.synthetic.luau'), os.path.join(LUA, 'consts.luau'))
    for f in os.listdir(os.path.join(HERE, 'luau')):
        if f.endswith('.luau') and f != 'cases.luau':
            shutil.copy(os.path.join(HERE, 'luau', f), LUA)
    dump = dump_world() if (not args.no_world and os.path.isdir(RA)) else None
    if args.synthetic:
        txt = open(SYN, 'rb').read().decode('latin-1').replace('\r\n', '\n')
        lvl = 3
        while ']' + '=' * lvl + ']' in txt: lvl += 1
        open(os.path.join(LUA, 'synthetic_src.luau'), 'wb').write(('return {[' + '=' * lvl + '[\n' + txt + ']' + '=' * lvl + ']}\n').encode('latin-1'))
    with open(os.path.join(LUA, 'cases.luau'), 'w') as f:
        f.write('return {\n' + ',\n'.join('{id=%s,path=%s,npc=%s,answers=%s,player=%s,cap=%d,set=%s,x=%d,y=%d%s%s%s}' % (
            lua_val(c['id']), lua_val(c['path']), lua_val(c['npc']), lua_val(c['answers']), lua_val(c['player']), c['cap'], lua_val(c['set']), c['x'], c['y'],
            (',label=%s' % lua_val(c['label'])) if c.get('label') else '',
            ',synth=true' if c.get('synth') else '',
            (lambda i: (',init=%s' % i) if i and not args.no_seed else '')(load_init(c['id']))) for c in cases) + '\n}\n')
    for attempt in range(3):
        if os.path.exists(os.path.join(LUA, 'world_data.luau')): os.remove(os.path.join(LUA, 'world_data.luau'))
        if dump:
            st = write_world(dump)
            if attempt == 0:
                print('world data: %d item names, %d battle flags, %d castles, %d maps (from the legacy VM), %d shop NPC names (from the NPC files), %d recorded lookups' % (st + (len(load_lookup()),)))
        t = time.time()
        p = subprocess.run([args.luau, 'run_cases.luau'], cwd=LUA, capture_output=True, timeout=3600)
        txt = p.stdout.decode('latin-1')
        cur, buf = None, []
        misses = []
        for line in txt.split('\n'):
            if line.startswith('=== CASE '):
                cur, buf = line[9:].strip(), []
            elif line.startswith('=== END ') and cur:
                open(os.path.join(OUT, 'luau', cur + '.trace'), 'wb').write(('\n'.join(buf) + '\n').encode('latin-1'))
                cur = None
            elif line.startswith('### miss '):
                misses.append(line[9:])
            elif cur is not None:
                buf.append(line)
        open(os.path.join(OUT, 'luau.stderr'), 'wb').write(p.stderr)
        print('luau side: %d cases in %.1fs (rc=%d)%s' % (len(cases), time.time() - t, p.returncode, ('; %d static-data queries without a recorded answer' % len(misses)) if misses else ''))
        new = [k for k in misses if k not in load_lookup() and '<' not in k]
        if not dump or not new: break
        print('  asking the legacy VM %d static-data queries (e.g. %s)' % (len(new), new[0]))
        probe_lookups(new)

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
    env = dict(os.environ, RA_REPLAY_NPC=os.fsdecode(c['npc'].encode('latin-1', 'replace')),  # raw bytes of the name (NPC files are not UTF-8)
                RA_REPLAY_ANSWERS=os.path.join(run, 'answers.txt'),
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
    if not args.synthetic: return run_legacy_all()
    conf = os.path.join(RA, 'npc', 're' if args.set == 're' else 'pre-re', 'scripts_main.conf')
    conf_txt = open(conf, 'rb').read()
    inst = os.path.join(RA, 'npc', 'replay_synth.txt')
    shutil.copy(SYN, inst)
    open(conf, 'ab').write(b'\nnpc: npc/replay_synth.txt\n')
    try: return run_legacy_all()
    finally:
        open(conf, 'wb').write(conf_txt)
        if os.path.exists(inst): os.remove(inst)

def run_legacy_all():
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
if not args.skip_luau: run_luau()
rc = subprocess.run([sys.executable, '-I', os.path.join(HERE, 'compare_traces.py'), '--cases', os.path.join(OUT, 'cases.json'),
                     '--legacy', os.path.join(OUT, 'legacy'), '--luau', os.path.join(OUT, 'luau'), '--out', OUT, '--tag=' + ('' if args.report_name == 'report' else '-' + args.report_name)]).returncode
sys.exit(rc)
