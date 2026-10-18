#!/usr/bin/env python3
"""Pick the conformance corpus: NPC scripts of the renewal load set (npc/re/scripts_main.conf import graph),
by category, deterministically. Writes corpus/corpus.json (the case list the runners read).

  python3 -I select_corpus.py [--repo /home/user/reAthena]

Categories (target counts): simple dialogue, callsub/goto-heavy, switch/menu, array/string heavy,
plus the five M0 worst-case files (one or more NPCs each)."""
import os, re, sys, json, argparse

ap = argparse.ArgumentParser()
ap.add_argument('--repo', default=os.environ.get('RATHENA_REPO', '/home/user/reAthena'))
ap.add_argument('--out', default=os.path.join(os.path.dirname(os.path.abspath(__file__)), 'corpus', 'corpus.json'))
args = ap.parse_args()
REPO = args.repo

def walk(conf, out):
    with open(os.path.join(REPO, conf), 'rb') as f:
        for raw in f.read().decode('latin-1').splitlines():
            line = raw.split('//', 1)[0].strip()
            m = re.match(r'^(npc|import)\s*:\s*(\S.*)$', line)
            if not m: continue
            if m.group(1) == 'npc': out.append(m.group(2).strip())
            else: walk(m.group(2).strip(), out)

files = []
walk('npc/re/scripts_main.conf', files)
files = [f for f in dict.fromkeys(files) if f.endswith('.txt')]

HEAD = re.compile(r'^(?:([\w@.\-]+),(\d+),(\d+),(\d+)|-)\tscript\t([^\t]+)\t([^\t,]+),\{', re.M)

def body_end(text, i):
    """index after the matching '}' for the '{' at text[i-1]; string/comment aware"""
    depth, n, in_s = 1, len(text), False
    while i < n:
        c = text[i]
        if in_s:
            if c == '\\': i += 1
            elif c == '"': in_s = False
        else:
            if c == '"': in_s = True
            elif c == '/' and text[i+1:i+2] == '/':
                j = text.find('\n', i); i = n if j < 0 else j
                continue
            elif c == '{': depth += 1
            elif c == '}':
                depth -= 1
                if depth == 0: return i + 1
        i += 1
    return None

STRFN = re.compile(r'\b(setarray|explode|implode|substr|charat|strlen|replacestr|countstr|strpos|getarraysize|copyarray|deletearray|sprintf|upperstring|lowerstring|cleararray|array_\w+|getd|setd)\b')
npcs = []
for f in files:
    p = os.path.join(REPO, f)
    if not os.path.exists(p): continue
    text = open(p, 'rb').read().decode('latin-1').replace('\r\n', '\n')
    for m in HEAD.finditer(text):
        name = m.group(5)
        if '::' in name: continue
        end = body_end(text, m.end())
        if end is None: continue
        body = text[m.end():end]
        line = text.count('\n', 0, m.start()) + 1
        n_lines = body.count('\n') + 1
        npcs.append(dict(file=f, name=name, line=line, lines=n_lines, map=m.group(1) or '-',
            callsub=len(re.findall(r'\bcallsub\b', body)), goto=len(re.findall(r'\bgoto\b', body)),
            switch=len(re.findall(r'\bswitch\s*\(', body)), select=len(re.findall(r'\b(select|prompt)\s*\(', body)) + len(re.findall(r'\bmenu\b', body)),
            strfn=len(STRFN.findall(body)), mes=len(re.findall(r'\bmes\b', body)), callfunc=len(re.findall(r'\bcallfunc\b', body)),
            labels=len(re.findall(r'^\s*[A-Za-z_]\w*:\s*$', body, re.M)),
            onlabels=len(re.findall(r'^\s*On\w+:', body, re.M)),
            other=len(re.findall(r'\b(monster|areamonster|donpcevent|warp|waitingroom|sleep|addtimer|initnpctimer|getguild\w*|\w*castle\w*|instance_\w+)\b', body))))

# names must be unique over the whole load set (npc_name2id resolves by exname)
from collections import Counter
cnt = Counter(n['name'] for n in npcs)
npcs = [n for n in npcs if cnt[n['name']] == 1]

chosen, used_files = [], set()
def take(cat, cands, k, per_file=1):
    got = 0
    for n in cands:
        if got >= k: break
        if sum(1 for c in chosen if c['file'] == n['file']) >= per_file: continue
        if any(c['name'] == n['name'] for c in chosen): continue
        chosen.append(dict(n, category=cat)); got += 1
    return got

key = lambda n: (n['file'], n['line'])
# five M0 worst-case files first (most control-flow-dense NPC of each, then more NPCs from prontera)
WORST = ['npc/re/merchants/enchan_mal.txt', 'npc/re/guild/invest_main.txt', 'npc/re/quests/quests_16_1.txt', 'npc/guild2/agit_main_se.txt', 'npc/cities/prontera.txt']
for wf in WORST:
    c = [n for n in npcs if n['file'] == wf and n['mes'] > 0]
    c.sort(key=lambda n: (-(n['callsub'] * 3 + n['goto'] * 3 + n['switch'] + n['select'] + n['labels']), n['lines']))
    take('worst:' + os.path.basename(wf), c, 2 if wf.endswith('prontera.txt') else 1, per_file=2)
simple = [n for n in npcs if n['lines'] <= 40 and n['mes'] >= 3 and n['callsub'] == 0 and n['goto'] == 0 and n['switch'] == 0 and n['callfunc'] == 0 and n['other'] == 0 and n['strfn'] == 0]
simple.sort(key=lambda n: (n['file'] in used_files, -n['mes'], key(n)))
step = max(1, len(simple) // 40)
take('simple-dialogue', simple[::step], 10)
flow = [n for n in npcs if (n['callsub'] + n['goto']) >= 3 and n['lines'] <= 500 and n['other'] <= 3]
flow.sort(key=lambda n: (-(n['callsub'] + n['goto'] * 2), key(n)))
take('callsub-goto', flow, 10)
sw = [n for n in npcs if n['switch'] >= 1 and n['select'] >= 1 and 25 <= n['lines'] <= 400 and n['other'] <= 3 and n['strfn'] <= 2]
sw.sort(key=lambda n: (-(n['switch'] * 2 + n['select']), key(n)))
take('switch-menu', sw[::max(1, len(sw)//60)], 10)
st = [n for n in npcs if n['strfn'] >= 4 and n['lines'] <= 400 and n['other'] <= 3]
st.sort(key=lambda n: (-n['strfn'], key(n)))
take('array-string', st, 10)

json.dump(dict(repo_files=len(files), npcs_scanned=len(npcs), chosen=chosen), open(args.out, 'w'), indent=1)
from collections import defaultdict
by = defaultdict(int)
for c in chosen: by[c['category']] += 1
print(len(chosen), 'NPCs from', len(set(c['file'] for c in chosen)), 'files;', dict(by))
